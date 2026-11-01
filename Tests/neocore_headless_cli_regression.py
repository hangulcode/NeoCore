import json
import pathlib
import subprocess
import sys
import tempfile


console = pathlib.Path(sys.argv[1]).resolve()
exporter = pathlib.Path(sys.argv[2]).resolve()
checks = 0


def require(condition, label):
    global checks
    checks += 1
    if not condition:
        raise AssertionError(label)


def run(*arguments):
    return subprocess.run([str(console), *map(str, arguments)], text=True, encoding="utf-8", errors="replace", capture_output=True, timeout=20)


def lsp(schema, path, text, implementation=None):
    messages = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}},
        {"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {"textDocument": {"uri": path.as_uri(), "languageId": "neocore", "version": 1, "text": text}}},
        {"jsonrpc": "2.0", "id": 2, "method": "shutdown", "params": {}},
        {"jsonrpc": "2.0", "method": "exit", "params": {}},
    ]
    payload = b""
    for message in messages:
        body = json.dumps(message).encode("utf-8")
        payload += f"Content-Length: {len(body)}\r\n\r\n".encode("ascii") + body
    arguments = [str(console), "--host", str(schema), "--lsp"]
    if implementation:
        arguments += ["--host-impl", str(implementation)]
    result = subprocess.run(arguments, input=payload, capture_output=True, timeout=20)
    require(result.returncode == 0, "host-aware LSP starts: " + result.stderr.decode("utf-8", errors="replace"))
    output = result.stdout
    diagnostics = []
    while output:
        header, output = output.split(b"\r\n\r\n", 1)
        length = int(header.split(b":", 1)[1])
        message = json.loads(output[:length])
        output = output[length:]
        if message.get("method") == "textDocument/publishDiagnostics":
            diagnostics.extend(message["params"]["diagnostics"])
    return diagnostics


implementation = '''import system;
map<string,int> store={};
map<int,Vector3> positions={};
map<int,fn(int)->void> callbacks={};
UserData data=Headless.New_UserData();
Resource resource=Headless.New_Resource();
GameObject node=Headless.New_GameObject();
void Log(Services self,string message){print(message);}
UserData Data(Services self){return data;}
Resource Resources(Services self){return resource;}
void SetInt(UserData self,string key,int value){store[key]=value;Services.Log("saved");}
int GetInt(UserData self,string key,int fallback){return store.get(key,fallback);}
string LoadText(Resource self,string path){return Headless.ReadText(path);}
int Id(GameObject self){return Headless.Id_GameObject(self);}
Vector3 Position(GameObject self){return positions.get(Headless.Id_GameObject(self),Vector3());}
void SetPosition(GameObject self,Vector3 value){positions[Headless.Id_GameObject(self)]=value;}
GameObject Find(GameObject self,string rid){return node;}
void Register(GameObject self,string event,fn(int)->void callback){callbacks[Headless.Id_GameObject(self)]=callback;}
public void Fire(GameObject self,int value){fn(int)->void callback=callbacks.get(Headless.Id_GameObject(self),null);if(callback!=null)callback(value);}
public GameObject MakeNode(){return Headless.New_GameObject();}
public void Destroy(GameObject value){Headless.Destroy_GameObject(value);}
public HeadlessApi Api(){return Headless;}
Headless.Bind("Services.Log",Log);
Headless.Bind("Services.UserData",Data);
Headless.Bind("Services.Resource",Resources);
Headless.Bind("Services.UserData.SetInt",SetInt);
Headless.Bind("UserData.GetInt",GetInt);
Headless.Bind("Resource.LoadText",LoadText);
Headless.Bind("GameObject.Id.get",Id);
Headless.Bind("GameObject.Position.get",Position);
Headless.Bind("GameObject.Position.set",SetPosition);
Headless.Bind("GameObject.FindChildByRID",Find);
Headless.Bind("GameObject.AddEvent.OnClick",Register);
Headless.SetGlobal_this(node);
'''

application = '''import system;
import fake;
int clicks=0;
void Check(bool value){if(!value){print("FAILED");int zero=0;print(1/zero);}}
void OnClick(int value){clicks+=value;}
fn(int)->void Callback(int base){return fn(int value)->void{clicks+=base+value;};}
Services.Log("mock log works");
Services.UserData.SetInt("score",KEY_TEST);
Check(Services.UserData.GetInt("score",0)==17);
Check(Services.UserData.GetInt("absent",9)==9);
struct Data{string name;int value;};
Data loaded=system.jsonDecode<Data>(Services.Resource.LoadText("data.json"));
Check(loaded.name=="테스트"&&loaded.value==7);
GameObject child=this.FindChildByRID("child");
child.Position.x=3;
Check(child.Position.x==3&&child.Id==this.Id);
child.AddEvent("OnClick",OnClick);
fake.Fire(child,5);
Check(clicks==5);
GameObject other=fake.MakeNode();
other.AddEvent("OnClick",Callback(3));
fake.Fire(other,7);
Check(clicks==15&&other.Id!=child.Id);
Check(Services.Resource.LoadText("missing.json")==null);
print("HEADLESS_OK");
'''

with tempfile.TemporaryDirectory(prefix="neocore-host-") as temporary:
    root = pathlib.Path(temporary)
    schema = root / "host.json"
    result = subprocess.run([str(exporter), "--write", str(schema)], capture_output=True, text=True, timeout=20)
    require(result.returncode == 0, "export schema: " + result.stdout + result.stderr)
    impl = root / "fake.nc"
    impl.write_text(implementation, encoding="utf-8-sig")
    app = root / "main.nc"
    app.write_text(application, encoding="utf-8-sig")
    (root / "data.json").write_text(json.dumps({"name": "테스트", "value": 7}, ensure_ascii=False), encoding="utf-8-sig")
    base = ["--host", schema, "--host-impl", impl, "--resource-root", root]
    for debug in ([], ["--debug"]):
        result = run(*base, *debug, "--file", app)
        require(result.returncode == 0 and "HEADLESS_OK" in result.stdout, "run typed script host: " + result.stdout + result.stderr)
    result = run(*base, "--emit-backend", app)
    require(result.returncode == 0 and "$neocore_mock_" in result.stdout, "emit direct mock wrappers: " + result.stderr)
    plain = root / "check.nc"
    plain.write_text('Services.UserData.SetInt("x",KEY_TEST);', encoding="utf-8")
    result = run("--check", plain, "--host", schema)
    require(result.returncode == 0, "schema-only checking: " + result.stderr)
    result = run("--file", plain, "--host", schema, "--debug")
    require(result.returncode == 1 and "no host implementation: Services.UserData.get" in result.stderr, "missing implementation traps at first member: " + result.stderr)
    plain.write_text('print(KEY_TEST);', encoding="utf-8")
    result = run("--file", plain, "--host", schema)
    require(result.returncode == 0 and result.stdout.strip() == "17", "non-host execution and imported defines: " + result.stderr)
    forbidden = root / "forbidden.nc"
    forbidden.write_text('Headless.ReadText("data.json");', encoding="utf-8")
    result = run(*base, "--check", forbidden)
    require(result.returncode == 1 and "only available in the implementation module" in result.stderr, "ordinary game modules cannot access console helpers: " + result.stderr)
    forbidden.write_text('import fake;HeadlessApi api=fake.Api();api.ReadText("data.json");', encoding="utf-8")
    result = run(*base, "--check", forbidden)
    require(result.returncode == 1 and "only available in the implementation module" in result.stderr, "helper object aliases cannot bypass module scope: " + result.stderr)
    broken = root / "wrong.nc"
    broken.write_text('void Wrong(UserData self,string key,float value){}\nHeadless.Bind("UserData.SetInt",Wrong);', encoding="utf-8")
    result = run("--host", schema, "--host-impl", broken, "--check", plain)
    require(result.returncode == 1 and "wrong.nc" in result.stderr, "wrong implementation signature rejected: " + result.stderr)
    broken.write_text('Services.Log("too early");int state=5;void Log(Services self,string text){print(state);}Headless.Bind("Services.Log",Log);', encoding="utf-8")
    result = run("--host", schema, "--host-impl", broken, "--check", plain)
    require(result.returncode == 1 and "before its initializer completes" in result.stderr, "mapped calls preserve global initialization ordering: " + result.stderr)
    for registration in ('if(false) Headless.Bind("Services.Log",Log);', 'HeadlessApi api=Headless;api.Bind("Services.Log",Log);'):
        broken.write_text('void Log(Services self,string text){}'+registration, encoding="utf-8")
        result = run("--host", schema, "--host-impl", broken, "--check", plain)
        require(result.returncode == 1 and "standalone Headless.Bind" in result.stderr, "conditional or aliased registrations rejected: " + result.stderr)
    for body in ('sleep(1);', 'Wait();', 'importedCallback();', 'yield;'):
        prefix = 'void Wait(){sleep(1);} fn()->void importedCallback=fn()->void{sleep(1);};'
        broken.write_text(prefix+'void Log(Services self,string text){'+body+'} Headless.Bind("Services.Log",Log);', encoding="utf-8")
        result = run("--host", schema, "--host-impl", broken, "--check", plain)
        require(result.returncode == 1 and "must be synchronous" in result.stderr, "suspending mock rejected: " + result.stderr)
    plain.write_text('import fake;GameObject node=fake.MakeNode();fake.Destroy(node);print(node.Id);', encoding="utf-8")
    result = run(*base, "--file", plain, "--debug")
    require(result.returncode == 1 and "no longer valid" in result.stderr, "deleted fake IDs are validated: " + result.stdout + result.stderr)
    plain.write_text('void A(){int x="bad";float y=false;}\nvoid B(){int z=true;}', encoding="utf-8")
    result = run("--check", plain, "--host", schema)
    require(result.returncode == 1 and result.stderr.count("check.nc:") >= 3, "CLI prints all independent errors: " + result.stderr)
    result = run("--check", plain, "--host", schema, "--max-errors", 2)
    require(result.returncode == 1 and result.stderr.count("check.nc:") == 2, "CLI diagnostic limit: " + result.stderr)
    diagnostics = lsp(schema, plain, 'Services.Log("ok");Services.UserData.SetInt("x",KEY_TEST);')
    require(not diagnostics, "LSP accepts actual host schema and defines: " + repr(diagnostics))
    diagnostics = lsp(schema, plain, 'Services.Log(3);\nServices.UserData.SetInt("x",false);')
    diagnostics_with_impl = lsp(schema, plain, 'Services.Log("ok");', impl)
    require(not diagnostics_with_impl, "LSP imports and validates the host implementation: " + repr(diagnostics_with_impl))
    diagnostics_with_impl = lsp(schema, plain, 'print(1);', broken)
    require(diagnostics_with_impl and any("must be synchronous" in item["message"] for item in diagnostics_with_impl), "LSP reports errors in the implementation module: " + repr(diagnostics_with_impl))
    require(len(diagnostics) == 2, "LSP publishes multiple host type errors: " + repr(diagnostics))

print(f"NeoCore headless CLI: {checks} checks passed")
