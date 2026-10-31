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
    if (!condition)
    {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", label.c_str());
    }
}

static NeoCore::Result Invoke(const std::string& source, const std::string& label, bool debug)
{
    NeoCore::Error error;
    std::unique_ptr<NeoCore::Program> program = NeoCore::Program::Compile(source, error, label, debug);
    Check(program != nullptr, label + " compile: " + error.message);
    if (!program)
    {
        NeoCore::Result result;
        result.error = error;
        return result;
    }
    NeoCore::Result result = program->Call("Probe");
    Check(result.ok(), label + " execute: " + result.error.message);
    return result;
}

static void ExpectInt(const char* source, std::int64_t expected, const char* label, bool debug)
{
    const NeoCore::Result result = Invoke(source, label, debug);
    Check(result.ok() && result.value.kind() == NeoCore::Scalar::Kind::Int && result.value.asInt() == expected,
        std::string(label) + " result");
}

static void ExpectFloat(const char* source, double expected, const char* label, bool debug)
{
    const NeoCore::Result result = Invoke(source, label, debug);
    Check(result.ok() && result.value.kind() == NeoCore::Scalar::Kind::Float && result.value.asFloat() == expected,
        std::string(label) + " result");
}

static void ExpectString(const char* source, const char* expected, const char* label, bool debug)
{
    const NeoCore::Result result = Invoke(source, label, debug);
    Check(result.ok() && result.value.kind() == NeoCore::Scalar::Kind::String && result.value.asString() == expected,
        std::string(label) + " result");
}

static void CheckExecution(bool debug)
{
    ExpectFloat("struct S{int x;float y;};export float Probe(){S s=S(2,3);return s.x+s.y;}",
        5.0, "constructor field promotion", debug);
    ExpectInt("struct S{int x;float y;bool active;int? number;string? name;};export int Probe(){S s;if(s.x==0&&s.y==0.0&&!s.active&&s.number==null&&s.name==null)return 1;return 0;}",
        1, "scalar field defaults", debug);
    ExpectInt("struct S{int x;float y;};export int Probe(){S s=S();s.x=3;return s.x+toint(s.y);}",
        3, "empty constructor defaults", debug);
    ExpectInt("struct Empty{};export int Probe(){Empty a;Empty b=Empty();a=b;return 1;}",
        1, "empty struct values", debug);
    ExpectInt("struct I{int x;};struct O{I inner;bool active;};export int Probe(){O o;o.inner.x=4;if(o.active)return 99;return o.inner.x;}",
        4, "recursive default construction", debug);
    ExpectInt("struct S{int? number;float? fraction;string? name;};export int Probe(){S s=S(null,2,null);if(s.number==null&&s.fraction==2.0&&s.name==null)return 1;return 0;}",
        1, "nullable scalar constructor fields", debug);
    ExpectFloat("struct S{float x;};export float Probe(){S s=S(1);s.x=2;s.x+=3;return s.x;}",
        5.0, "field assignment materializes float", debug);
    ExpectInt("struct S{int x;};export int Probe(){S s=S(2);int a=s.x++;int b=++s.x;s.x*=3;s.x-=1;return a*100+b*10+s.x;}",
        251, "field compound and increment values", debug);
    ExpectInt("struct S{int x;};S Make(){return S(7);}export int Probe(){return Make().x+S(3).x;}",
        10, "temporary struct field reads", debug);

    // 모든 값 전달 경계에서 복사하여 호출자와 복사본의 필드가 독립적인지 확인한다.
    ExpectInt("struct S{int x;};export int Probe(){S a=S(1);S b=a;b.x=2;return a.x*10+b.x;}",
        12, "declaration copy", debug);
    ExpectInt("struct S{int x;};export int Probe(){S a=S(1);S b=S(0);b=a;a.x=3;return a.x*10+b.x;}",
        31, "assignment copy", debug);
    ExpectInt("struct S{int x;};void Change(S s){s.x=9;}export int Probe(){S s=S(1);Change(s);return s.x;}",
        1, "by-value argument copy", debug);
    ExpectInt("struct S{int x;};S Make(){S s=S(1);return s;}export int Probe(){S s=Make();s.x+=1;return s.x;}",
        2, "returned local struct lifetime", debug);
    ExpectInt("struct S{int x;};S source=S(1);S Read(){return source;}export int Probe(){S copy=Read();copy.x=9;return source.x*10+copy.x;}",
        19, "returned global value copy", debug);
    ExpectInt("struct I{int x;};struct O{I inner;};export int Probe(){I i=I(1);O o=O(i);i.x=7;return i.x*10+o.inner.x;}",
        71, "constructor copies nested value", debug);
    ExpectInt("struct I{int x;};struct O{I inner;};export int Probe(){O a=O(I(1));O b=a;b.inner.x=2;return a.inner.x*10+b.inner.x;}",
        12, "nested declaration deep copy", debug);
    ExpectInt("struct I{int x;};struct O{I inner;};export int Probe(){O a=O(I(1));O b=O(I(0));b.inner=a.inner;a.inner.x=3;return a.inner.x*10+b.inner.x;}",
        31, "nested field assignment copy", debug);
    ExpectString("struct S{string text;};export string Probe(){S a=S(\"old\");S b=a;b.text=\"new\";return a.text..\"|\"..b.text;}",
        "old|new", "string field value copy", debug);
    ExpectString("struct S{string text;};S source=S(\"old\");S Read(){return source;}export string Probe(){S a=Read();source=S(\"new\");return a.text;}",
        "old", "string field return independence", debug);
    ExpectInt("struct I{int x;};struct O{I inner;};export int Probe(){O o=O(I(3));o=o;return o.inner.x;}",
        3, "whole value self assignment", debug);

    ExpectInt("struct S{int x;};S g=S(1);int Replace(){g=S(9);return 0;}int Read(S s,int unused){return s.x;}export int Probe(){return Read(g,Replace());}",
        1, "argument snapshot before whole replacement", debug);
    ExpectInt("struct S{int x;};S g=S(1);int Change(){g.x=9;return 0;}int Read(S s,int unused){return s.x;}export int Probe(){return Read(g,Change());}",
        1, "argument snapshot before field mutation", debug);
    ExpectInt("struct S{int x;};struct Pair{S first;int second;};S g=S(1);int Replace(){g=S(9);return 2;}export int Probe(){Pair p=Pair(g,Replace());return p.first.x*10+p.second;}",
        12, "constructor argument snapshot", debug);
    ExpectInt("struct S{int x;};S g=S(1);S Next(){g=S(2);return g;}int Read(S a,S b){return a.x*10+b.x;}export int Probe(){return Read(g,Next());}",
        12, "each struct argument has its own snapshot", debug);
    ExpectFloat("struct S{float x;int y;};int i=1;int Change(){i=4;return 2;}export float Probe(){S s=S(i,Change());return s.x*10+s.y+i;}",
        16.0, "constructor preserves child promotion before global mutation", debug);
    ExpectInt("struct S{int x;};S Change(S s){s=S(9);return s;}export int Probe(){S s=S(1);S copy=Change(s);return s.x*10+copy.x;}",
        19, "whole parameter replacement leaves caller value unchanged", debug);
    ExpectInt("struct S{int x;};int Inc(int x){return ++x;}export int Probe(){S s=S(1);return Inc(s.x)*10+s.x;}",
        21, "field argument is a scalar value copy", debug);
    ExpectInt("struct S{int x;};export int Probe(){S s=S(1);s=S(5);s.x+=2;return s.x;}",
        7, "field writes after whole assignment", debug);
    ExpectInt("struct I{int x;};struct O{I inner;};export int Probe(){O o=O(I(1));o.inner=I(5);o.inner.x+=2;return o.inner.x;}",
        7, "field writes after nested assignment", debug);
    ExpectInt("struct I{int x;};struct O{I inner;};export int Probe(){O o=O(I(1));I copy=o.inner;o=O(I(5));copy.x+=2;return o.inner.x*10+copy.x;}",
        53, "nested copy stays independent after root replacement", debug);
    ExpectInt("struct S{int x;};S Change(S a,S b){a.x=3;b.x+=a.x;return b;}export int Probe(){S s=S(1);S result=Change(s,s);return s.x*10+result.x;}",
        14, "repeated struct arguments have independent storage", debug);
    ExpectInt("struct I{int x;};struct O{I inner;};int Change(I i,O o){o=O(I(5));i.x+=2;return i.x*10+o.inner.x;}export int Probe(){O o=O(I(1));return Change(o.inner,o)*10+o.inner.x;}",
        351, "nested and root arguments are independent values", debug);
    ExpectInt("struct S{int x;};S Inc(S s){s.x++;return s;}S Relay(S s){return Inc(s);}export int Probe(){S s=S(1);S result=Relay(s);return result.x*10+s.x;}",
        21, "forwarded struct values do not mutate the caller", debug);
    ExpectInt("struct S{int x;};int Local(S s){s.x=9;return s.x;}export int Probe(){S s=S(1);return Local(s)*10+s.x;}",
        91, "field update in a by-value struct parameter", debug);
    ExpectInt("struct S{int x;};export int Probe(){S s=S(1);int result=0;{S s=S(9);result=s.x;}return result*10+s.x;}",
        91, "shadowed struct roots retain separate storage", debug);
    ExpectInt("struct S{int x;};S g=S(1);int Replace(){g=S(9);return 0;}int Read(int x,int unused){return x;}export int Probe(){return Read(g.x,Replace());}",
        1, "field value argument snapshots before source replacement", debug);
    ExpectInt("struct S{int x;int y;};export int Probe(){S s=S(1,2);S t=S(3,4);s.x=(s=t).y;return s.x*10+s.y;}",
        44, "field target resolved after whole assignment RHS", debug);
    ExpectInt("struct I{int x;int y;};struct O{I inner;};export int Probe(){O o=O(I(1,2));I t=I(3,4);o.inner.x=(o.inner=t).y;return o.inner.x*10+o.inner.y;}",
        44, "nested field target resolved after RHS", debug);
    ExpectInt("struct S{int? x;};export int Probe(){S s=S(1);s=S(2);s.x=null;if(s.x==null)return 1;return 0;}",
        1, "nullable field write after replacement", debug);
    ExpectString("struct S{string text;};export string Probe(){S s=S(\"a\");string copy=s.text;s=S(\"b\");s.text=\"c\";return copy..s.text;}",
        "ac", "string field copy survives root replacement", debug);
    ExpectFloat("struct Position{float x;float y;};struct Particle{Position position;int age;};Particle Advance(Particle p,float dt){p.position.x+=dt;p.age+=1;return p;}export float Probe(){Particle p=Particle(Position(1,2),0);Particle copy=p;p=Particle(Position(10,20),3);p.position.x+=2;p=Advance(p,0.5);if(p.age!=4||copy.position.x!=1.0)return -1.0;return p.position.x;}",
        12.5, "particle update returns a new value", debug);
    ExpectInt("struct S{int x;};export int Probe(){S s=S(1);return s.x;}// trailing comment without newline",
        1, "generated helpers after final line comment", debug);
    ExpectInt("struct S{int clone;int assign;};int __neocore_1=5;export int Probe(){S s=S(1,2);s=S(3,4);s.clone+=__neocore_1;return s.clone*10+s.assign;}",
        84, "field and helper-like user names remain distinct", debug);
}

static void CheckRejections(bool debug)
{
    const struct { const char* source; const char* label; } invalid[] = {
        {"struct A{int x;};struct B{int x;};A a=B(1);", "nominal initialization mismatch"},
        {"struct A{int x;};struct B{int x;};A a=A(1);B b=B(2);a=b;", "nominal assignment mismatch"},
        {"struct A{int x;};struct B{int x;};void F(A a){}F(B(1));", "nominal argument mismatch"},
        {"struct A{int x;};struct B{int x;};A F(){return B(1);}", "nominal return mismatch"},
        {"struct S{S child;};", "direct recursive values"},
        {"struct A{B child;};struct B{A child;};", "mutual recursive values"},
        {"struct S{S? child;};", "recursive nullable struct"},
        {"struct S{int x;int x;};", "duplicate field"},
        {"struct S{int x;};struct S{float y;};", "duplicate struct definition"},
        {"struct S{void x;};", "void field"},
        {"struct S{null x;};", "bare null field"},
        {"struct S{int& x;};", "reference field"},
        {"struct S{int32 x;};", "int32 field"},
        {"struct S{float32 x;};", "float32 field"},
        {"struct S{any x;};", "dynamic field"},
        {"struct S{const int x;};", "field modifier"},
        {"struct S{int x=1;};", "field initializer"},
        {"struct S{int Method(){return 1;}};", "struct method"},
        {"void F(){struct S{int x;};}", "local struct definition"},
        {"struct S{int x;};S? s=null;", "nullable struct declaration"},
        {"struct S{string text;};S s;", "string field has no implicit default"},
        {"struct S{string text;};S s=S();", "string field has no empty constructor"},
        {"struct I{string text;};struct O{I inner;};O o;", "nested string prevents default"},
        {"struct S{int x;int y;};S s=S(1);", "constructor missing argument"},
        {"struct S{int x;};S s=S(1,2);", "constructor excess argument"},
        {"struct S{int x;};S s=S(1.5);", "constructor narrowing"},
        {"struct S{int x;};S s=S(null);", "nonnullable field initialization"},
        {"struct S{int x;};S s=S(1);s.x=1.5;", "field assignment narrowing"},
        {"struct S{int x;};S s=S(1);s.x+=1.5;", "field compound narrowing"},
        {"struct S{int x;};S s=S(1);s.missing=2;", "unknown field write"},
        {"struct S{int x;};S s=S(1);int x=s.missing;", "unknown field read"},
        {"struct S{int x;};S a=S(1);S b=S(1);bool eq=a==b;", "struct equality"},
        {"struct S{int x;};S a=S(1);S b=S(1);bool eq=a!=b;", "struct inequality"},
        {"struct S{int x;};S s=S(1);s+=s;", "struct arithmetic"},
        {"struct S{int x;};S& r=S(1);", "temporary whole struct reference"},
        {"struct S{int x;};S value=S(1);S& alias=value;", "named whole struct reference"},
        {"struct S{int x;};S value=S(1);int& alias=value.x;", "named field reference"},
        {"struct S{int x;};void Change(S& value){value.x=2;}", "struct reference parameter"},
        {"struct S{int x;};void Change(int& value){value=2;}", "scalar reference parameter"},
        {"struct S{int x;};int& r=S(1).x;", "temporary field reference"},
        {"struct S{int x;};S Make(){return S(1);}int& r=Make().x;", "returned temporary field reference"},
        {"struct S{int x;};void Set(int& x){x=2;}Set(S(1).x);", "temporary field reference argument"},
        {"struct S{int x;};S(1).x=2;", "temporary field assignment"},
        {"struct S{int x;};S Make(){return S(1);}Make().x++;", "temporary field increment"},
        {"struct S{int x;};S s=S(1);float& r=s.x;", "field reference requires exact type"},
        {"struct S{int x;};S& F(){S s=S(1);return s;}", "escaping struct reference return"},
        {"struct S{int x;};int S(){return 1;}", "function conflicts with struct name"}
    };
    for (const auto& item : invalid)
    {
        NeoCore::Error error;
        const std::unique_ptr<NeoCore::Program> program = NeoCore::Program::Compile(item.source, error, item.label, debug);
        Check(!program && !error.ok(), std::string("reject ") + item.label);
    }
}

static void CheckHostBoundary(bool debug)
{
    NeoCore::Error error;
    std::unique_ptr<NeoCore::Program> program = NeoCore::Program::Compile(
        "struct S{int x;};int zero=0;int invalid=1/zero;export S Probe(){return S(1);}",
        error, "host struct return preflight", debug);
    Check(program != nullptr, "host struct return source compiles: " + error.message);
    if (program)
    {
        const NeoCore::Result result = program->Call("Probe");
        Check(!result.ok() && result.error.message.find("division by zero") != std::string::npos,
            "supported host struct return preserves initialization errors");
        const NeoCore::Result initialized = program->Run();
        Check(!initialized.ok() && initialized.error.message.find("division by zero") != std::string::npos,
            "failed struct global initialization is retained");
    }
    program = NeoCore::Program::Compile("struct S{int x;};export S Probe(){return S(7);}", error,
        "host struct return", debug);
    Check(program != nullptr, "host struct value compiles");
    if (program)
    {
        const NeoCore::Result structure = program->Call("Probe");
        Check(structure.ok() && structure.value.kind() == NeoCore::Scalar::Kind::Struct &&
            structure.value.Count() == 1 && structure.value.At(0).asInt() == 7, "host receives typed struct fields");
    }
    program = NeoCore::Program::Compile(
        "struct S{int x;};int zero=0;int invalid=1/zero;export void Whole(S s){}",
        error, "host struct parameter preflight", debug);
    Check(program != nullptr, "host struct parameter source compiles: " + error.message);
    if (program)
    {
        const NeoCore::Result value = program->Call("Whole", {NeoCore::Scalar::Int(1)});
        Check(!value.ok() && value.error.message.find("requires S") != std::string::npos,
            "host scalar cannot initialize a struct parameter");
    }
}

static void CheckDiagnostics(bool debug)
{
    const std::string badField =
        "struct S{int x;};\n"
        "export int Probe(){\n"
        " S s=S(1);\n"
        " return s.missing;\n"
        "}\n";
    NeoCore::Error error;
    std::unique_ptr<NeoCore::Program> program = NeoCore::Program::Compile(badField, error, "original_field_line.nc", debug);
    Check(!program && error.line == 4 && error.sourceName == "original_field_line.nc",
        "field diagnostic retains original source line");

    const std::string badRhs =
        "struct S{\n"
        " int value;\n"
        "};\n"
        "export int Probe(){\n"
        " S s=S(1);\n"
        " int zero=0;\n"
        " s.value=1/zero;\n"
        " return s.value;\n"
        "}\n";
    program = NeoCore::Program::Compile(badRhs, error, "original_field_rhs.nc", debug);
    Check(program != nullptr, "runtime field RHS source compiles: " + error.message);
    if (program)
    {
        const NeoCore::Result result = program->Call("Probe");
        Check(!result.ok() && result.error.message.find("division by zero") != std::string::npos &&
            result.error.sourceName == "original_field_rhs.nc", "field RHS reports the original runtime error");
        if (debug)
            Check(result.error.line == 7 || result.error.message.find("Line(7)") != std::string::npos,
                "runtime field RHS retains original line after helper generation: " + result.error.message);
    }
    const std::string multiLine =
        "int zero=0;\n"
        "export int Probe(){\n"
        " return 1 +\n"
        "   (8 / zero);\n"
        "}\n";
    program = NeoCore::Program::Compile(multiLine, error, "multiline_expression.nc", debug);
    Check(program != nullptr, "multiline scalar source compiles: " + error.message);
    if (program)
    {
        const NeoCore::Result result = program->Call("Probe");
        Check(!result.ok() && result.error.message.find("division by zero") != std::string::npos,
            "multiline scalar runtime failure");
        if (debug)
            Check(result.error.line == 4 || result.error.message.find("Line(4)") != std::string::npos,
                "multiline operation keeps its original line: " + result.error.message);
    }
}

int main()
{
    for (int mode = 0; mode < 2; ++mode)
    {
        const bool debug = mode != 0;
        CheckExecution(debug);
        CheckRejections(debug);
        CheckHostBoundary(debug);
        CheckDiagnostics(debug);
    }
    std::printf("NeoCore struct regression: %d/%d passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
