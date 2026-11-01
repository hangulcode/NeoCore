#include "NeoCore.h"
#include "NeoCoreCompiler.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace
{
int checks = 0;
int failures = 0;
void Check(bool condition, const std::string& label)
{
    ++checks;
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label.c_str()); }
}

void Value(const std::string& source, const NeoCore::Scalar& expected, bool debug)
{
    NeoCore::Error error;
    auto program = NeoCore::Program::Compile(source, error, "constants.nc", debug);
    Check(bool(program), "compile: " + error.message + " source: " + source);
    if (!program) return;
    for (int image = 0; image != 2; ++image)
    {
        const NeoCore::Result result = program->Call("Probe");
        bool matches = result.ok() && !result.suspended() && result.value.kind() == expected.kind();
        if (matches && expected.kind() == NeoCore::Scalar::Kind::Int)
            matches = result.value.asInt() == expected.asInt();
        if (matches && expected.kind() == NeoCore::Scalar::Kind::Float)
            matches = result.value.asFloat() == expected.asFloat() &&
                (expected.asFloat() != 0 || std::signbit(result.value.asFloat()) == std::signbit(expected.asFloat()));
        Check(matches, "constant value: " + result.error.message + " source: " + source);
        if (image == 0)
        {
            std::vector<std::uint8_t> bytes;
            Check(program->SaveImage(bytes, error), "save constants: " + error.message);
            program = NeoCore::Program::LoadImage(bytes, error);
            Check(bool(program), "load constants: " + error.message);
            if (!program) return;
        }
    }
}

void Matrix(bool debug)
{
    const struct Integer { const char* expression; std::int64_t value; } integers[] = {
        {"(3 + 5) * 7 - 4", 52}, {"17 / -3", -5}, {"-17 % 3", -2},
        {"~0", -1}, {"(1 << 40) | 3", 1099511627779LL}, {"-8 >> 2", -2},
        {"1 << 63", (std::numeric_limits<std::int64_t>::min)()},
        {"-1 >> 63", -1}, {"0xffffffffffffffff ^ 0x7fffffffffffffff", (std::numeric_limits<std::int64_t>::min)()},
        {"9223372036854775807 + 1", (std::numeric_limits<std::int64_t>::min)()},
        {"-9223372036854775808 - 1", (std::numeric_limits<std::int64_t>::max)()},
        {"0x4000000000000000 * 4", 0}, {"-(-9223372036854775808)", (std::numeric_limits<std::int64_t>::min)()}
    };
    for (const Integer& item : integers)
    {
        const std::string expression = item.expression;
        Value("const int C=" + expression + ";struct S{int value=" + expression + ";};"
            "export int Probe(){S s;if(s.value!=C)return 12345;switch(C){case " + expression +
            ":return C;default:return 54321;}}", NeoCore::Scalar::Int(item.value), debug);
    }
    const struct Floating { const char* expression; double value; } floating[] = {
        {"1", 1.0}, {"1.0 / 2", 0.5}, {"-0.0", -0.0}, {"-1.25 * 4", -5.0},
        {"9007199254740993 + 0.0", 9007199254740992.0}, {"0.1 + 0.2", 0.30000000000000004}
    };
    for (const Floating& item : floating)
    {
        const std::string expression = item.expression;
        Value("const float C=" + expression + ";struct S{float value=" + expression + ";};"
            "export float Probe(){S s;if(s.value!=C)return 12345.0;return C;}",
            NeoCore::Scalar::Float(item.value), debug);
    }
    Value("const float ONE=1;const float HALF=ONE/2;struct S{float a=HALF;float b=ONE/2;};"
        "export float Probe(){S s;return s.a+s.b;}", NeoCore::Scalar::Float(1), debug);
    Value("const string LABEL=\"a\\tb\";struct S{string name=LABEL;bool on=true;};"
        "export int Probe(){S s;if(!s.on)return 0;switch(s.name){case \"a\\tb\":return 7;default:return 0;}}",
        NeoCore::Scalar::Int(7), debug);
    Value("const string EMPTY=null;export int Probe(){if(EMPTY==null)return 1;return 0;}",
        NeoCore::Scalar::Int(1), debug);
    Value("import math;struct S{Vector3 v=math.Vector3(3.4028235e38,0,0);Quaternion q=Quaternion();};"
        "export float Probe(){S s;if(s.q.w!=1)return 0.0;return s.v.x;}",
        NeoCore::Scalar::Float((std::numeric_limits<float>::max)()), debug);
    Value("import system;const int N=1<<5;struct S{int n=N;float ratio=1.0/2;};"
        "export float Probe(){S s=system.jsonDecodeLenient<S>(\"{}\");return float(s.n)+s.ratio;}",
        NeoCore::Scalar::Float(32.5), debug);
    std::string chain = "const int A0=0;";
    for (int i = 1; i <= 100; ++i)
        chain += "const int A" + std::to_string(i) + "=A" + std::to_string(i - 1) + "+1;";
    Value(chain + "struct S{int n=A100;};export int Probe(){S s;switch(s.n){case A100:return s.n;default:return 0;}}",
        NeoCore::Scalar::Int(100), debug);
}

void Reject(const std::string& source, const std::string& message)
{
    NeoCore::StaticCompileResult checked;
    Check(!NeoCore::CompileStaticSource(source, checked) && checked.errorLine > 0 &&
        checked.errorMessage.find(message) != std::string::npos,
        "frontend rejects " + source + ": " + checked.errorMessage);
}

void Rejections()
{
    const char* invalid[] = {"1/0", "1%0", "-9223372036854775808/-1", "-9223372036854775808%-1", "1<<64", "1>>-1"};
    for (const char* expression : invalid)
    {
        const std::string value = expression;
        Reject("const int C=" + value + ";", "constant");
        Reject("struct S{int n=" + value + ";};", "constant");
        Reject("int n=0;switch(n){case " + value + ":;}", "constant");
    }
    Reject("Vector3 Make(){return Vector3(7,8,9);}struct S{Vector3 v=Make();};", "constant expression");
    Reject("Vector3 Make(float x,float y,float z){return Vector3(x*10,y,z);}struct S{Vector3 v=Make(1,2,3);};", "constant expression");
    Reject("const float X=1.0/0.0;", "division by zero");
    Reject("struct S{float x=1.0/0.0;};", "division by zero");
    Reject("const float X=1e308*2.0;", "non-finite");
    Reject("struct S{float x=1e308*2.0;};", "non-finite");
    Reject("struct S{Vector3 v=Vector3(1e100,0,0);};", "non-finite");
    Reject("const string A=\"a\";string value=A;switch(value){case A:;case \"\\u0061\":;}", "duplicate case");
    Reject("int value=0;switch(value){case 1<<4:;case 16:;}", "duplicate case");
    Reject("const bool X=!false;", "logical");
    Reject("struct S{bool x=!false;};", "logical");
    Reject("const string X=\"a\"..\"b\";", "concatenation");
    Reject("struct S{string x=\"a\"..\"b\";};", "concatenation");
    const char* forms[] = {"const int C=\n1/0;", "struct S{int n=\n1/0;};", "int n=0;switch(n){case\n1/0:;}"};
    for (const char* source : forms)
    {
        NeoCore::StaticCompileResult checked;
        Check(!NeoCore::CompileStaticSource(source, checked) && checked.errorLine == 2,
            "constant failure retains original source line");
    }
}

void Modules(bool debug)
{
    NeoCore::CompileOptions options;
    options.moduleLoader = [](const std::string& name, const std::string&, NeoCore::ModuleSource& output, NeoCore::Error&) {
        if (name != "cfg") return false;
        output.sourceName = "cfg.nc";
        output.source = "public const int SIZE=1<<5;public const float HALF=1.0/2;public struct Data{int size=SIZE;float ratio=HALF;};";
        return true;
    };
    NeoCore::Error error;
    auto program = NeoCore::Program::Compile("import cfg;export int Probe(){cfg.Data d;switch(d.size){case cfg.SIZE:return toint(d.ratio*100);default:return 0;}}",
        error, "module-constants.nc", debug, options);
    Check(bool(program), "module constants compile: " + error.message);
    if (!program) return;
    const NeoCore::Result result = program->Call("Probe");
    Check(result.ok() && result.value.asInt() == 50, "module constant uses agree");
}
}

int main()
{
    for (bool debug : {false, true}) { Matrix(debug); Modules(debug); }
    Rejections();
    std::printf("NeoCore constants: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
