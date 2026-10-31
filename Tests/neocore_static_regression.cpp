#include "NeoCore.h"
#include "NeoCoreCompiler.h"
#include <cstdio>
#include <string>
#include <vector>

static int checks = 0;
static int failures = 0;
static void Check(bool condition, const std::string& label)
{
    ++checks;
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label.c_str()); }
}

static void Reject(const std::string& source, const std::string& label)
{
    NeoCore::Error error;
    std::unique_ptr<NeoCore::Program> program = NeoCore::Program::Compile(source, error, label);
    Check(!program && !error.ok(), "reject " + label);
}

static NeoCore::Result Invoke(const std::string& source, const std::string& label, bool debug)
{
    NeoCore::Error error;
    std::unique_ptr<NeoCore::Program> program = NeoCore::Program::Compile(source, error, label, debug);
    Check(program != nullptr, label + " compile: " + error.message);
    if (!program) { NeoCore::Result result; result.error = error; return result; }
    NeoCore::Result result = program->Call("Probe");
    Check(result.ok(), label + " execute: " + result.error.message);
    return result;
}

static void ExpectInt(const std::string& source, std::int64_t expected, const std::string& label, bool debug)
{
    NeoCore::Result result = Invoke(source, label, debug);
    Check(result.ok() && result.value.kind() == NeoCore::Scalar::Kind::Int &&
        result.value.asInt() == expected, label + " result");
}

static void CheckExecution(bool debug)
{
    ExpectInt("export int Probe(){int x; int y=7; bool b; if(b) return 99; return x+y;}",
        7, "default values", debug);
    ExpectInt("export int Probe(){return Fib(10);} int Fib(int n){if(n<2)return n; return Fib(n-1)+Fib(n-2);}",
        55, "forward recursion", debug);
    ExpectInt("export int Probe(){if(Even(8)) return 1; else return 0;} bool Even(int n){if(n==0)return true;return Odd(n-1);} bool Odd(int n){if(n==0)return false;return Even(n-1);}",
        1, "mutual recursion", debug);
    ExpectInt("int count=0; void Tick(){count+=1;return;} export int Probe(){Tick();Tick();return count;}",
        2, "void functions", debug);
    ExpectInt("export int Probe(){int x=0; while(x<5){x+=1;if(x==3)continue;if(x==4)break;}return x;}",
        4, "while control", debug);
    ExpectInt("export int Probe(){int sum=0;for(int i in 0,5){sum+=i;}return sum;}",
        10, "typed range", debug);
    ExpectInt("export int Probe(){int x=2;{int x=8;x+=1;}return x;}",
        2, "block shadowing", debug);
    ExpectInt("int x=1; int Change(){x=5;return 2;} export int Probe(){int y=x+Change();return y*10+x;}",
        35, "left operand snapshot", debug);
    ExpectInt("int x=1; int Change(){x=5;return 2;} int Pair(int a,int b){return a*10+b;} export int Probe(){return Pair(x,Change());}",
        12, "argument snapshot", debug);
    ExpectInt("export int Probe(){int? x; string? s=null; if(x==null && s==null)return 1;return 0;}",
        1, "nullable defaults", debug);
    ExpectInt("export int Probe(){return toint(3.75);}", 3, "explicit narrowing", debug);
    ExpectInt("export int Probe(){string s=\"a\"..\"b\"; if(s==\"ab\")return 1;return 0;}",
        1, "typed strings", debug);
    ExpectInt("export int Probe(){int x=1; x<<=4;return ~x;}", -17, "integer operators", debug);

    NeoCore::Result promoted = Invoke("float Id(float v){return v;} export float Probe(){float x=1; x+=2; return Id(x)+1;}",
        "numeric promotion", debug);
    Check(promoted.ok() && promoted.value.kind() == NeoCore::Scalar::Kind::Float &&
        promoted.value.asFloat() == 4.0, "promotion writes actual float value");
    NeoCore::Result returned = Invoke("export float Probe(){return 3;}", "return promotion", debug);
    Check(returned.ok() && returned.value.kind() == NeoCore::Scalar::Kind::Float &&
        returned.value.asFloat() == 3.0, "return promotion materialized");
    NeoCore::Result literals = Invoke("export float Probe(){return .5+1.+1.e1+.5e1;}", "fractional literal spellings", debug);
    Check(literals.ok() && literals.value.kind() == NeoCore::Scalar::Kind::Float &&
        literals.value.asFloat() == 16.5, "fractional spellings preserve value");

    // 같은 저장소의 별칭 둘과 별칭을 다시 전달하는 호출도 즉시 같은 값을 읽어야 한다.
    ExpectInt("void Change(int& a,int& b){a=4;b+=a;} export int Probe(){int x=1;Change(x,x);return x;}",
        8, "aliased reference arguments", debug);
    ExpectInt("void Add(int& x){x+=1;} void Relay(int& x){Add(x);} export int Probe(){int x=2;int& a=x;int& b=a;b=5;Relay(a);return x;}",
        6, "local and forwarded references", debug);
    ExpectInt("int x=3;void Set(int& a){a=9;} export int Probe(){Set(x);return x;}",
        9, "global reference", debug);
    ExpectInt("void Set(int& a){a=7;} int Local(int a){Set(a);return a;} export int Probe(){int x=3;return Local(x)*10+x;}",
        73, "reference to by-value parameter", debug);
    ExpectInt("int Change(int& a){a=8;return 2;} int Pair(int a,int b){return a*10+b;} export int Probe(){int x=3;return Pair(x,Change(x));}",
        32, "snapshot before reference call", debug);
    ExpectInt("export int Probe(){int x=1;int y=7;int& alias=x;alias=y;y=9;return x*10+y;}",
        79, "reference assignment never rebinds", debug);
    ExpectInt("export int Probe(){int x=1;int& alias=x;{int x=9;alias=x;}return x;}",
        9, "reference with shadowed name", debug);
    ExpectInt("int count=0;bool Bump(int& x){x+=1;return true;} export int Probe(){bool a=false&&Bump(count);bool b=true||Bump(count);return count;}",
        0, "short circuit with references", debug);
    NeoCore::Result refFloat = Invoke("void Add(float& x){x+=0.5;} export float Probe(){float x=1;float& r=x;Add(r);return x;}",
        "float cell promotion", debug);
    Check(refFloat.ok() && refFloat.value.kind() == NeoCore::Scalar::Kind::Float &&
        refFloat.value.asFloat() == 1.5, "float cell retains promoted storage");
    NeoCore::Result refString = Invoke("void Set(string& s){s=\"new\";} export string Probe(){string s=\"old\";Set(s);return s;}",
        "string reference", debug);
    Check(refString.ok() && refString.value.kind() == NeoCore::Scalar::Kind::String &&
        refString.value.asString() == "new", "string reference updates caller");
}

int main()
{
    const struct { const char* source; const char* label; } invalid[] = {
        {"var x=1;", "no inferred declaration"},
        {"fun F(){return 1;}", "no fun syntax"},
        {"int32 x=1;", "no int32 scalar"},
        {"float32 x=1.0;", "no float32 scalar"},
        {"any x=1;", "no any"},
        {"int F(float32 x){return 1;}", "no float32 parameter"},
        {"int x=1.5;", "no implicit narrowing"},
        {"int x=true;", "bool is not int"},
        {"float x=false;", "bool is not float"},
        {"string x;", "nonnullable string initializer"},
        {"int x=null;", "nonnull int"},
        {"int? x=1; int y=x;", "no nullable unwrap"},
        {"int? x=1; int y=x+1;", "no nullable arithmetic"},
        {"void x;", "no void variable"},
        {"int F(){int x=1;}", "nonvoid missing return"},
        {"int F(bool b){if(b)return 1;}", "partial return"},
        {"void F(){return 1;}", "void return value"},
        {"int F(){return;}", "nonvoid bare return"},
        {"int F(){return 1.2;}", "return narrowing"},
        {"void F(){} int x=F();", "void expression"},
        {"int F(int x){return x;} int x=F();", "missing argument"},
        {"int F(int x){return x;} int x=F(1,2);", "excess argument"},
        {"int F(int x){return x;} int x=F(1.2);", "argument narrowing"},
        {"int F(int x){return x;} int F(float x){return 1;}", "no overload"},
        {"int F();", "no prototype"},
        {"if(1){}", "boolean condition"},
        {"int x=3.0%2.0;", "integer remainder"},
        {"int x=y; int y=1;", "declaration order"},
        {"int F(){return x;} int x=1;", "global declaration order"},
        {"int x=unknown(1);", "unknown function"},
        {"int x=1; x.nope();", "no dynamic member"},
        {"int x=[1,2];", "no unchecked list"},
        {"import unknown_module;", "no unchecked import"},
        {"break;", "break outside loop"},
        {"continue;", "continue outside loop"},
        {"return;", "return outside function"},
        {"int x=1; int x=2;", "duplicate variable"},
        {"{int x=1;} x=2;", "scope escape"},
        {"const int X=1; X=2;", "const assignment"},
        {"const bool X=1<2;", "const comparison explicitly deferred"},
        {"const string X=\"a\"..\"b\";", "const string operation explicitly deferred"},
        {"int x=9223372036854775808;", "positive decimal overflow"},
        {"int x=-9223372036854775809;", "negative decimal overflow"},
        {"int x=0x10000000000000000;", "hexadecimal overflow"},
        {"int& r;", "reference requires binding"},
        {"int& r=1;", "no reference temporary"},
        {"int x=1;float& r=x;", "no reference conversion"},
        {"void F(int& r){} F(1);", "no temporary reference argument"},
        {"int& F(){int x=1;return x;}", "no reference return"},
        {"const int x=1;int& r=x;", "no mutable const reference"},
        {"int x=0;int* p=x;", "no pointer"}
    };
    for (const auto& item : invalid) Reject(item.source, item.label);
    CheckExecution(false);
    CheckExecution(true);

    NeoCore::Error error;
    auto program = NeoCore::Program::Compile("int n=0;n+=1;int Private(){return 0;} export int Count(){return n;} export float Add(float x){return x+0.5;}", error);
    Check(program != nullptr, "host API compile");
    if (program)
    {
        Check(!program->Call("Private").ok(), "host cannot invoke private function");
        Check(!program->Call("Add").ok(), "host arity validation");
        Check(!program->Call("Add", {NeoCore::Scalar::String("bad")}).ok(), "host type validation");
        NeoCore::Result sum = program->Call("Add", {NeoCore::Scalar::Int(2)});
        Check(sum.ok() && sum.value.kind() == NeoCore::Scalar::Kind::Float && sum.value.asFloat() == 2.5,
            "host int-to-float conversion");
        Check(program->Run().ok() && program->Run().ok(), "global init once");
        NeoCore::Result count = program->Call("Count");
        Check(count.ok() && count.value.kind() == NeoCore::Scalar::Kind::Int && count.value.asInt() == 1,
            "repeated Run does not repeat initialization");
    }
    auto failing = NeoCore::Program::Compile("int zero=0;int x=1/zero;export int F(int x){return x;}", error);
    Check(failing != nullptr, "Compile does not execute initialization");
    if (failing)
    {
        NeoCore::Result wrong = failing->Call("F", {NeoCore::Scalar::String("bad")});
        Check(!wrong.ok() && wrong.error.message.find("requires int") != std::string::npos,
            "host checks arguments before initialization");
        Check(!failing->Run().ok() && !failing->Run().ok(), "failed initialization stays failed");
        Check(!failing->Call("F", {NeoCore::Scalar::Int(1)}).ok(), "init failure blocks exports");
    }
    NeoCore::Scalar retained;
    {
        auto values = NeoCore::Program::Compile("export string Text(){return \"owned\";} export void Empty(){} export int? Missing(){return null;} export float? Present(){return 2;} export int Fail(){int zero=0;return 1/zero;}", error);
        Check(values != nullptr, "return boundary program compiles");
        if (values)
        {
            NeoCore::Result text = values->Call("Text");
            Check(text.ok() && text.value.kind() == NeoCore::Scalar::Kind::String, "string result");
            retained = text.value;
            NeoCore::Result empty = values->Call("Empty");
            Check(empty.ok() && empty.value.kind() == NeoCore::Scalar::Kind::Void, "void result");
            NeoCore::Result missing = values->Call("Missing");
            Check(missing.ok() && missing.value.kind() == NeoCore::Scalar::Kind::Null, "nullable null result");
            NeoCore::Result present = values->Call("Present");
            Check(present.ok() && present.value.kind() == NeoCore::Scalar::Kind::Float && present.value.asFloat() == 2.0,
                "nullable float result promotion");
            Check(!values->Call("Fail").ok(), "execution failure reported");
            Check(values->Call("Text").ok(), "call after failure");
        }
    }
    Check(retained.kind() == NeoCore::Scalar::Kind::String && retained.asString() == "owned", "result owns string after program destruction");
    auto refs = NeoCore::Program::Compile("int zero=0;int x=1/zero;export void Ref(int& x){x=2;}", error);
    Check(refs != nullptr, "host ref boundary compile");
    if (refs)
    {
        NeoCore::Result result = refs->Call("Ref", {NeoCore::Scalar::Int(1)});
        Check(!result.ok() && result.error.message.find("requires int&") != std::string::npos,
            "host value cannot bind to script reference before init");
    }
    NeoCore::StaticCompileResult diagnostic;
    Check(!NeoCore::CompileStaticSource("int x=1;\nint y=\"bad\";", diagnostic) && diagnostic.errorLine == 2,
        "original source error line");
    std::string deep = "export int Probe(){return 1";
    for (int i = 0; i < 5000; ++i) deep += "+1";
    deep += ";}";
    Check(!NeoCore::CompileStaticSource(deep, diagnostic) && !diagnostic.errorMessage.empty(),
        "flat expression complexity fails without native stack overflow");
    std::printf("NeoCore static regression: %d/%d passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
