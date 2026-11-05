import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--console", required=True)
    options = parser.parse_args()
    console = Path(options.console).resolve()
    checks = 0
    with tempfile.TemporaryDirectory(prefix="neocore-module-") as temporary:
        directory = Path(temporary) / "unicode-\uac80\uc99d"
        directory.mkdir()
        (directory / "nested").mkdir()

        def write(name, source):
            path = directory / name
            path.write_text(source, encoding="utf-8-sig")
            return path

        def run(path, extra=(), mode="--file"):
            return subprocess.run([str(console), "--debug", mode, str(path), *extra],
                                  capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=15)

        write("state.nc", "public int value=1;value+=1;")
        second_name = "STATE" if os.name == "nt" else "state"
        write("root.nc", "import state as first;first.value=7;import " + second_name + " as second;"
              "if(second.value!=7){int zero=0;print(1/zero);}print(\"module identity ok\");")
        result = run(directory / "nested" / ".." / "root.nc")
        assert result.returncode == 0 and "module identity ok" in result.stdout, result.stdout + result.stderr
        checks += 1

        self_import = write("self.nc", "import self;public int Read(){return 1;}")
        result = run(directory / "nested" / ".." / self_import.name)
        assert result.returncode != 0 and "cyclic module import" in result.stderr, result.stdout + result.stderr
        assert "self.nc:1:" in result.stderr, result.stderr
        checks += 1

        write("wrong.nc", "// module diagnostic\npublic int Read(){return \"wrong\";}")
        diagnostic_root = write("diagnostic.nc", "import wrong;print(wrong.Read());")
        result = run(diagnostic_root)
        assert result.returncode != 0 and "wrong.nc:2:" in result.stderr, result.stdout + result.stderr
        checks += 1

        write("secret.nc", 'int state=1;state+=1;const int LIMIT=5;struct Hidden{int value;};'
              'int Add(int x){return state+LIMIT+x;}public int Read(){return state;}print("PRIVATE_INIT");')
        forms = (("Add", "print(s.Add(3));"), ("state", "print(s.state);"),
                 ("LIMIT", "print(s.LIMIT);"), ("Hidden", "s.Hidden h=s.Hidden(4);print(h.value);"))
        for symbol, body in forms:
            entry = write("private_entry.nc", "import secret as s;\n" + body)
            for mode in ("--file", "--check"):
                result = run(entry, mode=mode)
                assert result.returncode == 1 and "module member is not public: " + symbol in result.stderr, result.stdout + result.stderr
                assert "private_entry.nc:2:" in result.stderr, result.stderr
                checks += 1
                result = run(entry, ("--entry-private-access",), mode)
                assert result.returncode == 0, result.stdout + result.stderr
                if mode == "--check":
                    assert "PRIVATE_INIT" not in result.stdout, result.stdout
                checks += 1

        write("reader.nc", "import secret;public int Read(){return secret.Read();}")
        entry = write("private_entry.nc", 'import secret as first;import reader;first.state=7;'
                      'import secret as second;first.Hidden h=first.Hidden(first.LIMIT);'
                      'if(second.state!=7||reader.Read()!=7||first.Add(3)!=15||h.value!=5){int z=0;print(1/z);}'
                      'print("PRIVATE_OK");')
        result = run(directory / "nested" / ".." / entry.name, ("--entry-private-access",))
        assert result.returncode == 0 and "PRIVATE_OK" in result.stdout, result.stdout + result.stderr
        assert result.stdout.count("PRIVATE_INIT") == 1, result.stdout
        checks += 1
        result = run(entry)
        assert result.returncode == 1 and "not public" in result.stderr, result.stdout + result.stderr
        checks += 1

        write("private_error.nc", "int Fail(){\nint zero=0;return 1/zero;\n}")
        entry = write("private_entry.nc", "import private_error as p;print(p.Fail());")
        result = run(entry, ("--entry-private-access",))
        assert result.returncode == 1 and "private_error.nc:2:" in result.stderr, result.stdout + result.stderr
        assert "division by zero" in result.stderr, result.stderr
        checks += 1

        for symbol, body in forms:
            write("intruder.nc", "import secret as s;\nvoid Probe(){" + body + "}")
            entry = write("private_entry.nc", "import intruder;print(1);")
            for mode in ("--file", "--check"):
                result = run(entry, ("--entry-private-access",), mode)
                assert result.returncode == 1 and "module member is not public: " + symbol in result.stderr, result.stdout + result.stderr
                assert "intruder.nc:2:" in result.stderr, result.stderr
                checks += 1

        for body, message in (("s.LIMIT=9;", "cannot modify a const"),
                              ('int n=s.Add("wrong");', "cannot convert"),
                              ("int n=s.missing;", "module has no member")):
            entry = write("private_entry.nc", "import secret as s;" + body)
            result = run(entry, ("--entry-private-access",), "--check")
            assert result.returncode == 1 and message in result.stderr, result.stdout + result.stderr
            checks += 1

        for arguments in ([], ["--run", "hello"], ["--smoke"], ["--dap"], ["--lsp"], ["--emit-backend", str(entry)]):
            result = subprocess.run([str(console), "--entry-private-access", *arguments],
                                    capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=15)
            assert result.returncode == 2 and "--entry-private-access requires --file" in result.stderr, result.stdout + result.stderr
            checks += 1

    print("NeoCore module CLI regression: " + str(checks) + " checks passed")


if __name__ == "__main__":
    main()
