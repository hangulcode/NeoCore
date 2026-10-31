"""콘솔 DAP/LSP의 프로토콜과 실행 수명 회귀 검사."""

import argparse
import json
from pathlib import Path
import queue
import subprocess
import tempfile
import threading
import time


class Session:
    def __init__(self, exe, mode, cwd):
        self.process = subprocess.Popen(
            [str(exe), mode], cwd=cwd, stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.messages = queue.Queue()
        self.history = []
        self.seq = 1
        self.reader = threading.Thread(target=self.read, daemon=True)
        self.reader.start()

    def read(self):
        try:
            while True:
                header = self.process.stdout.readline()
                if not header:
                    self.messages.put(None)
                    return
                assert header.startswith(b"Content-Length:"), header
                size = int(header.split(b":", 1)[1])
                assert 0 < size <= 1024 * 1024, "oversized adapter response"
                assert self.process.stdout.readline() == b"\r\n"
                data = self.process.stdout.read(size)
                assert len(data) == size
                self.messages.put(json.loads(data.decode("utf-8")))
        except BaseException as error:
            self.messages.put(error)

    def raw(self, data, header_name="Content-Length"):
        if isinstance(data, dict):
            data = json.dumps(data, ensure_ascii=True).encode()
        elif isinstance(data, str):
            data = data.encode()
        self.process.stdin.write(f"{header_name}: {len(data)}\r\n\r\n".encode() + data)
        self.process.stdin.flush()

    def request(self, command, arguments=None, header_name="Content-Length"):
        seq = self.seq
        self.seq += 1
        # arguments를 먼저 직렬화해 "seq" 문자열이 최상위 seq를 가리지 못하는지 검사한다.
        self.raw({"arguments": arguments or {}, "seq": seq, "type": "request", "command": command}, header_name)
        return seq

    def wait(self, predicate, timeout=5):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            message = self.messages.get(timeout=max(0.01, end - time.monotonic()))
            assert message is not None, "adapter exited before response"
            if isinstance(message, BaseException):
                raise message
            self.history.append(message)
            if predicate(message):
                return message
        raise AssertionError("adapter response timeout")

    def response(self, seq):
        return self.wait(lambda m: m.get("type") == "response" and m.get("request_seq") == seq)

    def stop(self, reason=None):
        message = self.wait(lambda m: m.get("event") == "stopped")
        if reason:
            assert message["body"]["reason"] == reason, message
        return message

    def close(self):
        if self.process.poll() is None:
            self.process.kill()
        self.process.communicate(timeout=5)

    def exit_cleanly(self):
        assert self.process.wait(timeout=5) == 0


def initialize(session):
    assert session.response(session.request("initialize"))["success"]


def launch(session, script, library, breakpoints=None):
    result = session.response(session.request("launch", {"program": str(script), "libPath": str(library)}))
    assert result["success"], result
    session.wait(lambda m: m.get("event") == "initialized")
    if breakpoints is not None:
        result = session.response(session.request("setBreakpoints", {
            "source": {"path": str(script)}, "breakpoints": breakpoints}))
    return result


def run_tests(exe):
    checks = 0
    with tempfile.TemporaryDirectory(prefix="neo-debug-") as directory:
        root = Path(directory)
        library = root / "Lib"
        library.mkdir()
        unicode_dir = root / "한글-😀"
        unicode_dir.mkdir()
        active = []

        def start(mode="--dap"):
            value = Session(exe, mode, root)
            active.append(value)
            return value

        try:
            # 문자열 ID, 미지원 요청, UTF-16 열과 다중 행 시그니처.
            lsp = start("--lsp")
            lsp.raw({"jsonrpc": "2.0", "id": "문자-ID", "method": "initialize", "params": {}}, "content-length")
            assert lsp.wait(lambda m: m.get("id") == "문자-ID")["result"]["capabilities"]["positionEncoding"] == "utf-16"
            lsp.raw({"jsonrpc": "2.0", "id": "unknown", "method": "textDocument/definition", "params": {}})
            assert lsp.wait(lambda m: m.get("id") == "unknown")["error"]["code"] == -32601
            source = 'string s = "한글😀"; math.sq'
            uri = "file:///document.nc"
            lsp.raw({"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {
                "textDocument": {"uri": uri, "text": source}}})
            lsp.raw({"jsonrpc": "2.0", "id": 3, "method": "textDocument/completion", "params": {
                "textDocument": {"uri": uri}, "position": {"line": 0, "character": len(source.encode("utf-16-le")) // 2}}})
            items = lsp.wait(lambda m: m.get("id") == 3)["result"]["items"]
            assert any(item["label"] == "sqrt" for item in items), items
            source = 'math.pow(\n  [1, 2][0] + "a,b".len(), '
            lsp.raw({"jsonrpc": "2.0", "method": "textDocument/didChange", "params": {
                "textDocument": {"uri": uri}, "contentChanges": [{"text": source}]}})
            lsp.raw({"jsonrpc": "2.0", "id": 4, "method": "textDocument/signatureHelp", "params": {
                "textDocument": {"uri": uri}, "position": {"line": 1, "character": len(source.splitlines()[1])}}})
            result = lsp.wait(lambda m: m.get("id") == 4)["result"]
            assert result and result["activeParameter"] == 1, result
            source = 'int Add(int a, float b) { return a; }\nAdd(1, '
            lsp.raw({"jsonrpc": "2.0", "method": "textDocument/didChange", "params": {
                "textDocument": {"uri": uri}, "contentChanges": [{"text": source}]}})
            lsp.raw({"jsonrpc": "2.0", "id": 5, "method": "textDocument/signatureHelp", "params": {
                "textDocument": {"uri": uri}, "position": {"line": 1, "character": 7}}})
            result = lsp.wait(lambda m: m.get("id") == 5)["result"]
            assert result and "int Add(int a, float b)" in result["signatures"][0]["label"], result
            assert result["activeParameter"] == 1, result
            lsp.raw({"jsonrpc": "2.0", "id": 6, "method": "textDocument/hover", "params": {
                "textDocument": {"uri": uri}, "position": {"line": 1, "character": 1}}})
            assert "int Add" in lsp.wait(lambda m: m.get("id") == 6)["result"]["contents"]["value"]
            source = 'import system; system.json'
            lsp.raw({"jsonrpc": "2.0", "method": "textDocument/didChange", "params": {
                "textDocument": {"uri": uri}, "contentChanges": [{"text": source}]}})
            lsp.raw({"jsonrpc": "2.0", "id": "json-complete", "method": "textDocument/completion", "params": {
                "textDocument": {"uri": uri}, "position": {"line": 0, "character": len(source)}}})
            items = lsp.wait(lambda m: m.get("id") == "json-complete")["result"]["items"]
            assert {"jsonEncode", "jsonDecode"}.issubset({item["label"] for item in items}), items
            for request_id, source, parameter, signature in (
                ("json-generic", 'import system; system.jsonDecode<map<string,list<int>>>(', 0, "T system.jsonDecode<T>(string text)"),
                ("json-outer", 'import system; system.jsonEncode(system.jsonDecode<map<string,list<int>>>("{}"), ', 1, "string system.jsonEncode("),
            ):
                lsp.raw({"jsonrpc": "2.0", "method": "textDocument/didChange", "params": {
                    "textDocument": {"uri": uri}, "contentChanges": [{"text": source}]}})
                lsp.raw({"jsonrpc": "2.0", "id": request_id, "method": "textDocument/signatureHelp", "params": {
                    "textDocument": {"uri": uri}, "position": {"line": 0, "character": len(source)}}})
                result = lsp.wait(lambda m: m.get("id") == request_id)["result"]
                assert result and result["activeParameter"] == parameter, result
                assert signature in result["signatures"][0]["label"], result
            source = 'string text="한글😀"; int wrong="bad";'
            lsp.raw({"jsonrpc": "2.0", "method": "textDocument/didChange", "params": {
                "textDocument": {"uri": uri}, "contentChanges": [{"text": source}]}})
            diagnostic = lsp.wait(lambda m: m.get("method") == "textDocument/publishDiagnostics"
                                  and m.get("params", {}).get("diagnostics"))["params"]["diagnostics"][0]
            assert diagnostic["source"] == "NeoCore", diagnostic
            assert diagnostic["range"]["start"]["character"] == len(source[:source.index('"bad"')].encode("utf-16-le")) // 2, diagnostic
            source = 'int value=1;'
            lsp.raw({"jsonrpc": "2.0", "method": "textDocument/didChange", "params": {
                "textDocument": {"uri": uri}, "contentChanges": [{"text": source}]}})
            cleared = lsp.wait(lambda m: m.get("method") == "textDocument/publishDiagnostics"
                               and m.get("params", {}).get("uri") == uri and m["params"].get("diagnostics") == [])
            assert cleared
            lsp.raw({"jsonrpc": "2.0", "method": "exit"})
            lsp.exit_cleanly()
            checks += 11

            # 열린 모듈의 저장 전 변경을 모든 가져오기 루트에 반영하며 진단을 합친다.
            overlay = root / "overlay.nc"
            overlay.write_text("public int Value(){return 1;}\n", encoding="utf-8")
            overlay_uri = overlay.as_uri()
            parent_uris = [(root / ("parent" + str(i) + ".nc")).as_uri() for i in range(2)]
            lsp = start("--lsp")
            lsp.raw({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})
            lsp.wait(lambda m: m.get("id") == 1)
            for parent_uri in parent_uris:
                lsp.raw({"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {
                    "textDocument": {"uri": parent_uri, "text": "import overlay; int value=overlay.Value();"}}})
            lsp.raw({"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {
                "textDocument": {"uri": overlay_uri, "text": "public int Value(){return missing;}"}}})
            lsp.raw({"jsonrpc": "2.0", "id": 2, "method": "textDocument/completion", "params": {}})
            lsp.wait(lambda m: m.get("id") == 2)
            published = [m["params"] for m in lsp.history if m.get("method") == "textDocument/publishDiagnostics"
                         and m.get("params", {}).get("uri") == overlay_uri]
            assert published and published[-1]["diagnostics"], published
            lsp.raw({"jsonrpc": "2.0", "method": "textDocument/didChange", "params": {
                "textDocument": {"uri": overlay_uri}, "contentChanges": [{"text": "public int Value(){return 2;}"}]}})
            lsp.raw({"jsonrpc": "2.0", "id": 3, "method": "textDocument/completion", "params": {}})
            lsp.wait(lambda m: m.get("id") == 3)
            published = [m["params"] for m in lsp.history if m.get("method") == "textDocument/publishDiagnostics"
                         and m.get("params", {}).get("uri") == overlay_uri]
            assert published[-1]["diagnostics"] == [], published
            lsp.raw({"jsonrpc": "2.0", "method": "exit"})
            lsp.exit_cleanly()
            checks += 2

            # 소문자 헤더와 잘못된 JSON을 처리하고 다음 정상 요청도 계속 받는다.
            dap = start()
            assert dap.response(dap.request("initialize", header_name="content-length"))["success"]
            dap.raw('{"seq":999,"type":"request","command":"threads",')
            malformed = dap.response(0)
            assert not malformed["success"] and malformed.get("message"), malformed
            assert dap.response(dap.request("initialize"))["success"]
            dap.response(dap.request("disconnect"))
            dap.exit_cleanly()
            checks += 3

            # 상한보다 큰 길이는 본문이나 EOF를 기다리지 않고 종료해야 한다.
            for mode in ("--dap", "--lsp"):
                oversized = start(mode)
                oversized.process.stdin.write(b"Content-Length: 8388609\r\n\r\n")
                oversized.process.stdin.flush()
                oversized.exit_cleanly()
                checks += 1

            # 중단점 응답 개수, 한글 파일 경로, 순환 변수, 반복 hover의 참조 안정성.
            script = unicode_dir / "main.nc"
            script.write_text('int seq = 42;\nlist<fn()->void> a = [];\na.append(fn()->void { print(a.len()); });\nprint(seq);\n', encoding="utf-8")
            dap = start()
            initialize(dap)
            reply = launch(dap, script, library, [
                {"line": 0}, {"line": -1}, {"line": 4, "logMessage": "unsupported"}, {"line": 4}])
            states = reply["body"]["breakpoints"]
            assert len(states) == 4 and [x["verified"] for x in states] == [False, False, False, True], states
            assert not dap.response(dap.request("continue"))["success"]
            dap.response(dap.request("configurationDone"))
            dap.stop("breakpoint")
            value = dap.response(dap.request("evaluate", {"expression": "seq", "frameId": 0}))
            assert value["body"]["result"] == "42", value
            assert not dap.response(dap.request("scopes", {"frameId": 2147483647}))["success"]
            checks += 1
            refs = []
            for _ in range(20):
                value = dap.response(dap.request("evaluate", {"expression": "a", "frameId": 0}))
                refs.append(value["body"]["variablesReference"])
            assert refs[0] and len(set(refs)) == 1, refs
            children = dap.response(dap.request("variables", {"variablesReference": refs[0]}))
            assert len(children["body"]["variables"]) == 1
            assert children["body"]["variables"][0]["variablesReference"] == 0
            unknown = dap.response(dap.request('unknown"command'))
            assert not unknown["success"] and unknown["command"] == 'unknown"command'
            dap.response(dap.request("disconnect"))
            dap.exit_cleanly()
            checks += 6

            # 모듈의 원본 줄과 정적 지역 이름을 유지하고 구조체 필드를 이름으로 표시한다.
            module = library / "support.nc"
            module.write_text("public int Sum(int n) {\n int value=n+1;\n return value;\n}\n", encoding="utf-8")
            module_main = root / "module-main.nc"
            module_main.write_text("import support;\nint result=support.Sum(4);\nprint(result);\n", encoding="utf-8")
            dap = start()
            initialize(dap)
            launch(dap, module_main, library)
            reply = dap.response(dap.request("setBreakpoints", {
                "source": {"path": str(module)}, "breakpoints": [{"line": 3}]}))
            assert reply["body"]["breakpoints"][0]["verified"], reply
            dap.response(dap.request("configurationDone"))
            dap.stop("breakpoint")
            frames = dap.response(dap.request("stackTrace"))["body"]["stackFrames"]
            assert frames[0]["line"] == 3 and frames[0]["name"] == "Sum", frames
            assert Path(frames[0]["source"]["path"]).resolve() == module.resolve(), frames
            value = dap.response(dap.request("evaluate", {"expression": "value", "frameId": 0}))
            assert value["body"]["result"] == "5" and value["body"]["type"] == "int", value
            dap.response(dap.request("disconnect"))
            dap.exit_cleanly()
            checks += 3

            structure = root / "struct.nc"
            structure.write_text("struct Point { float x; float y; };\nPoint point=Point(1.0,2.0);\nint stop=1;\nprint(stop);\n", encoding="utf-8")
            dap = start()
            initialize(dap)
            launch(dap, structure, library, [{"line": 3}])
            dap.response(dap.request("configurationDone"))
            dap.stop("breakpoint")
            value = dap.response(dap.request("evaluate", {"expression": "point", "frameId": 0}))
            assert value["body"]["type"] == "Point", value
            fields = dap.response(dap.request("variables", {"variablesReference": value["body"]["variablesReference"]}))["body"]["variables"]
            assert [field["name"] for field in fields] == ["x", "y"], fields
            assert all(field["type"] == "float" for field in fields), fields
            dap.response(dap.request("disconnect"))
            dap.exit_cleanly()
            checks += 2

            # 64개 원소가 자기 목록을 가리켜도 hover와 자식 응답은 작은 유한 크기다.
            cyclic = root / "wide-cycle.nc"
            cyclic.write_text("list<fn()->void> wide=[];\n"
                              "for(int i in 0,64) wide.append(fn()->void { print(wide.len()); });\nprint(1);\n", encoding="utf-8")
            dap = start()
            initialize(dap)
            reply = launch(dap, cyclic, library, [{"line": 3}])
            assert reply["body"]["breakpoints"][0]["verified"], reply
            dap.response(dap.request("configurationDone"))
            dap.stop("breakpoint")
            hover = dap.response(dap.request("evaluate", {"expression": "wide", "frameId": 0}))
            reference = hover["body"]["variablesReference"]
            assert hover["success"] and reference and len(json.dumps(hover).encode()) < 4096, hover
            values = dap.response(dap.request("variables", {"variablesReference": reference}))
            assert len(values["body"]["variables"]) == 64 and len(json.dumps(values).encode()) < 32768, values
            assert all(value["variablesReference"] == 0 for value in values["body"]["variables"]), values
            again = dap.response(dap.request("evaluate", {"expression": "wide", "frameId": 0}))
            assert again["body"]["variablesReference"] == reference
            dap.response(dap.request("disconnect"))
            dap.exit_cleanly()
            checks += 3

            # 깊은 중첩은 컴파일 실패로 응답하며 이전 파일의 중단점 정보도 지운다.
            deep = root / "deep.nc"
            deep.write_text("int value=" + "(" * 256 + "0" + ")" * 256 + ";\n", encoding="utf-8")
            dap = start()
            initialize(dap)
            reply = launch(dap, script, library, [{"line": 4}])
            assert reply["body"]["breakpoints"][0]["verified"], reply
            failed = dap.response(dap.request("launch", {"program": str(deep), "libPath": str(library)}))
            assert not failed["success"], failed
            for path, line in ((deep, 1), (script, 4)):
                reply = dap.response(dap.request("setBreakpoints", {
                    "source": {"path": str(path)}, "breakpoints": [{"line": line}, {"line": 10000}]}))
                states = reply["body"]["breakpoints"]
                assert len(states) == 2 and all(not state["verified"] for state in states), reply
            assert dap.response(dap.request("threads"))["success"]
            dap.response(dap.request("disconnect"))
            dap.exit_cleanly()
            checks += 2

            # DAP 컴파일 진단의 열도 UTF-8 바이트가 아닌 UTF-16 문자 단위다.
            bad_unicode = unicode_dir / "invalid.nc"
            bad_source = 'string text="😀"; int value=missing;\n'
            bad_unicode.write_text(bad_source, encoding="utf-8")
            dap = start()
            initialize(dap)
            failed = dap.response(dap.request("launch", {"program": str(bad_unicode)}))
            assert not failed["success"], failed
            diagnostic = next(m["body"] for m in dap.history if m.get("event") == "neoCoreCompileError")
            expected_column = len(bad_source[:bad_source.index("missing")].encode("utf-16-le")) // 2 + 1
            assert diagnostic["line"] == 1 and diagnostic["column"] == expected_column, diagnostic
            dap.response(dap.request("disconnect"))
            dap.exit_cleanly()
            checks += 1

            # 잘못된 날짜 형식은 어댑터 종료 대신 검사 가능한 실행 오류로 남는다.
            bad_date = root / "bad-date.nc"
            bad_date.write_text('import system;\nstring value=system.utc_date("%Q",0);\n', encoding="utf-8")
            dap = start()
            initialize(dap)
            launch(dap, bad_date, library)
            dap.response(dap.request("configurationDone"))
            dap.stop("exception")
            description = dap.response(dap.request("exceptionInfo"))["body"]["description"]
            assert "system.utc_date" in description and "format" in description, description
            assert dap.response(dap.request("threads"))["success"]
            dap.response(dap.request("disconnect"))
            dap.exit_cleanly()
            checks += 2

            # 한 단계 건너뛰기 중 자식 함수 중단점도 지키며, 호출자 프레임에 전역을 표시한다.
            calls = root / "stepping.nc"
            calls.write_text(
                "int shared=42;\n"
                "int shadow=1;\n"
                "int Callee() {\n"
                " int inside=7;\n"
                " return inside+1;\n"
                "}\n"
                "int Caller() {\n"
                " int shadow=99;\n"
                " int result=Callee();\n"
                " return result+shadow;\n"
                "}\n"
                "int answer=Caller();\n"
                "print(answer);\n", encoding="utf-8")
            dap = start()
            initialize(dap)
            reply = launch(dap, calls, library, [{"line": 9}, {"line": 4}])
            assert all(bp["verified"] for bp in reply["body"]["breakpoints"]), reply
            dap.response(dap.request("configurationDone"))
            dap.stop("breakpoint")
            frames = dap.response(dap.request("stackTrace"))["body"]["stackFrames"]
            assert frames[0]["line"] == 9 and "Caller" in frames[0]["name"], frames
            assert dap.response(dap.request("next"))["success"]
            dap.stop("breakpoint")
            frames = dap.response(dap.request("stackTrace"))["body"]["stackFrames"]
            assert frames[0]["line"] == 4 and "Callee" in frames[0]["name"], frames
            caller = next(frame for frame in frames if "Caller" in frame["name"])
            assert caller["id"] != frames[0]["id"], frames
            for expression, expected in (("shared", "42"), ("shadow", "99")):
                value = dap.response(dap.request("evaluate", {
                    "expression": expression, "frameId": caller["id"]}))
                assert value["success"] and value["body"]["result"] == expected, value
            variables = dap.response(dap.request("variables", {
                "variablesReference": 1000 + caller["id"]}))["body"]["variables"]
            assert any(v["name"] == "shared" and v["value"] == "42" for v in variables), variables
            assert dap.response(dap.request("continue"))["success"]
            finished = dap.wait(lambda m: m.get("event") in ("stopped", "terminated"))
            assert finished["event"] == "terminated", finished
            dap.exit_cleanly()
            checks += 6

            # 같은 호출 줄은 복귀 직후 다시 멈추지 않고, 다음 반복에서는 한 번씩 멈춘다.
            repeated = root / "repeated-call.nc"
            repeated.write_text(
                "int total=0;\n"
                "void Add(int n) {\n"
                " total+=n;\n"
                "}\n"
                "for (int i in 0, 3) {\n"
                " Add(i);\n"
                "}\n"
                "print(total);\n", encoding="utf-8")
            dap = start()
            initialize(dap)
            reply = launch(dap, repeated, library, [{"line": 6}])
            assert reply["body"]["breakpoints"][0]["verified"], reply
            dap.response(dap.request("configurationDone"))
            for expected in range(3):
                dap.stop("breakpoint")
                frames = dap.response(dap.request("stackTrace"))["body"]["stackFrames"]
                assert frames[0]["line"] == 6, frames
                value = dap.response(dap.request("evaluate", {"expression": "i", "frameId": frames[0]["id"]}))
                assert value["success"] and value["body"]["result"] == str(expected), value
                assert dap.response(dap.request("continue"))["success"]
                checks += 1
            finished = dap.wait(lambda m: m.get("event") in ("stopped", "terminated"))
            assert finished["event"] == "terminated", finished
            stops = [m for m in dap.history if m.get("event") == "stopped"]
            assert len(stops) == 3, stops
            dap.exit_cleanly()
            checks += 1

            # 코루틴의 yield와 종료 모두 호출자 문장으로 돌아와야 한다.
            coroutine = root / "coroutine-step.nc"
            coroutine.write_text(
                "import coroutine;\n"
                "int progress=0;\n"
                "void Work() {\n"
                " progress+=1;\n"
                " yield;\n"
                " progress+=10;\n"
                "}\n"
                "coroutine<fn()->void> co=coroutine.create(Work);\n"
                "coroutine.resume(co);\n"
                "int afterFirst=progress;\n"
                "coroutine.resume(co);\n"
                "int afterSecond=progress;\n"
                "print(afterFirst+afterSecond);\n", encoding="utf-8")
            for command, break_lines in (("next", (9, 11)), ("stepOut", (4, 6))):
                dap = start()
                initialize(dap)
                reply = launch(dap, coroutine, library, [{"line": line} for line in break_lines])
                assert all(bp["verified"] for bp in reply["body"]["breakpoints"]), reply
                dap.response(dap.request("configurationDone"))
                for break_line, target_line, progress in zip(break_lines, (10, 12), (1, 11)):
                    dap.stop("breakpoint")
                    frames = dap.response(dap.request("stackTrace"))["body"]["stackFrames"]
                    assert frames[0]["line"] == break_line, (command, frames)
                    assert dap.response(dap.request(command))["success"]
                    dap.stop("step")
                    frames = dap.response(dap.request("stackTrace"))["body"]["stackFrames"]
                    assert frames[0]["line"] == target_line and "Work" not in frames[0]["name"], (command, frames)
                    value = dap.response(dap.request("evaluate", {"expression": "progress", "frameId": frames[0]["id"]}))
                    assert value["success"] and value["body"]["result"] == str(progress), (command, value)
                    assert dap.response(dap.request("continue"))["success"]
                    checks += 2
                finished = dap.wait(lambda m: m.get("event") in ("stopped", "terminated"))
                assert finished["event"] == "terminated", (command, finished)
                dap.exit_cleanly()

            # 중첩 함수의 yield는 stepOut 완료가 아니며 실제 반환 뒤 부모 문장에서 멈춘다.
            nested_coroutine = root / "nested-coroutine-step.nc"
            nested_coroutine.write_text(
                "import coroutine;\n"
                "int progress=0;\n"
                "void Inner() {\n"
                " progress+=1;\n"
                " yield;\n"
                " progress+=10;\n"
                " return;\n"
                "}\n"
                "void Work() {\n"
                " Inner();\n"
                " progress+=100;\n"
                "}\n"
                "coroutine<fn()->void> co=coroutine.create(Work);\n"
                "coroutine.resume(co);\n"
                "int afterFirst=progress;\n"
                "coroutine.resume(co);\n"
                "int afterSecond=progress;\n"
                "print(afterSecond);\n", encoding="utf-8")
            dap = start()
            initialize(dap)
            reply = launch(dap, nested_coroutine, library, [{"line": 4}])
            assert reply["body"]["breakpoints"][0]["verified"], reply
            dap.response(dap.request("configurationDone"))
            dap.stop("breakpoint")
            frames = dap.response(dap.request("stackTrace"))["body"]["stackFrames"]
            assert frames[0]["line"] == 4 and "Inner" in frames[0]["name"], frames
            assert dap.response(dap.request("stepOut"))["success"]
            dap.stop("step")
            frames = dap.response(dap.request("stackTrace"))["body"]["stackFrames"]
            assert frames[0]["line"] == 11 and "Work" in frames[0]["name"], frames
            value = dap.response(dap.request("evaluate", {"expression": "progress", "frameId": frames[0]["id"]}))
            assert value["success"] and value["body"]["result"] == "11", value
            assert dap.response(dap.request("continue"))["success"]
            finished = dap.wait(lambda m: m.get("event") in ("stopped", "terminated"))
            assert finished["event"] == "terminated", finished
            dap.exit_cleanly()
            checks += 2

            # 실행 중 pause와 disconnect, 입력 EOF가 같은 스레드의 슬라이스 사이에서 처리된다.
            loop = root / "loop.nc"
            loop.write_text("int count=0;\nwhile (true) { count++; }\n", encoding="utf-8")
            for eof in (False, True):
                dap = start()
                initialize(dap)
                launch(dap, loop, library)
                dap.response(dap.request("configurationDone"))
                if eof:
                    dap.process.stdin.close()
                    dap.process.stdin = None
                else:
                    assert dap.response(dap.request("threads"))["success"]
                    assert dap.response(dap.request("pause"))["success"]
                    dap.stop("pause")
                    dap.response(dap.request("disconnect"))
                dap.exit_cleanly()
                checks += 1

            # 오류 정리 이후에도 제한된 스택·지역값 스냅샷을 읽을 수 있다.
            fault = root / "fault.nc"
            fault.write_text("int Boom() {\n int keep=77;\n int zero=0; return keep / zero;\n}\nBoom();\n", encoding="utf-8")
            dap = start()
            initialize(dap)
            launch(dap, fault, library)
            dap.response(dap.request("configurationDone"))
            dap.stop("exception")
            frames = dap.response(dap.request("stackTrace"))["body"]["stackFrames"]
            assert frames and "Boom" in frames[0]["name"], frames
            variables = dap.response(dap.request("variables", {"variablesReference": 1000}))["body"]["variables"]
            assert any(v["name"] == "keep" and v["value"] == "77" for v in variables), variables
            assert dap.response(dap.request("exceptionInfo"))["body"]["description"]
            dap.response(dap.request("disconnect"))
            dap.exit_cleanly()
            checks += 3

            # 출력 바이트 경계가 UTF-8을 자르지 않고, 출력 이벤트가 다음 정지보다 먼저 온다.
            output = root / "output.nc"
            text = "가😀" * 200
            output.write_text('print("' + text + '");\nint stop=1;\nprint(stop);\n', encoding="utf-8")
            dap = start()
            initialize(dap)
            launch(dap, output, library, [{"line": 2}])
            dap.response(dap.request("configurationDone"))
            dap.stop("breakpoint")
            rendered = "".join(m.get("body", {}).get("output", "") for m in dap.history if m.get("event") == "output")
            assert text in rendered and "\ufffd" not in rendered
            seqs = [m["seq"] for m in dap.history]
            assert seqs == sorted(set(seqs)), seqs
            dap.response(dap.request("disconnect"))
            dap.exit_cleanly()
            checks += 2
        finally:
            for session in active:
                session.close()
    print(f"debug_adapter_regression: {checks} checks, 0 failures")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("console", type=Path)
    run_tests(parser.parse_args().console.resolve())
