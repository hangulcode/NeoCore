#include "NeoCore.h"
#include "NeoCoreCompiler.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

namespace
{
int checks = 0, failures = 0;
void Check(bool condition, const std::string& label)
{
    ++checks;
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label.c_str()); }
}

void Defines(bool debug)
{
    using namespace NeoCore;
    CompileOptions options;
    options.defines = {
        {"COUNT", CompileDefineKind::Int, "42"},
        {"NEG", CompileDefineKind::Int, "-7"},
        {"MINIMUM", CompileDefineKind::Int, "-9223372036854775808"},
        {"RATE", CompileDefineKind::Float, "2"},
        {"ZERO", CompileDefineKind::Float, "-0.0"},
        {"TEXT", CompileDefineKind::String, "\"COUNT \\u0041\""},
        {"FLAG", CompileDefineKind::Bool, "true"},
        {"NIL", CompileDefineKind::Null, "null"},
        {"ALIAS", CompileDefineKind::Identifier, "counter"},
        {"INTEGER", CompileDefineKind::Identifier, "int"}
    };
    options.moduleLoader = [](const std::string& name, const std::string&, ModuleSource& out, Error&) {
        if (name != "values") return false;
        out.sourceName = "values.nc";
        out.source = "INTEGER ALIAS=COUNT; public int Read(){return ALIAS-NEG;}";
        return true;
    };
    Error error;
    const std::string source =
        "import values as values; // COUNT is not expanded here\n"
        "export int Value(){return values.Read()+COUNT-NEG;}\n"
        "export int Minimum(){return MINIMUM;}\n"
        "export float Rate(){return RATE;}\n"
        "export float Zero(){return ZERO;}\n"
        "export string Text(){return TEXT;}\n"
        "export bool Flag(){return FLAG;}\n"
        "export int? Empty(){return NIL;}\n";
    std::unique_ptr<Program> program = Program::Compile(source, error, "defines.nc", debug, options);
    Check(program != nullptr, "define compilation: " + error.message);
    if (!program) return;
    Result result = program->Call("Value");
    Check(result.ok() && result.value.asInt() == 98, "define expansion precedes module-local renaming and preserves unary minus");
    result = program->Call("Minimum");
    Check(result.ok() && result.value.asInt() == (std::numeric_limits<std::int64_t>::min)(), "minimum integer define");
    result = program->Call("Rate");
    Check(result.ok() && result.value.kind() == Scalar::Kind::Float && result.value.asFloat() == 2.0, "integer spelling in float define remains float");
    result = program->Call("Zero");
    Check(result.ok() && result.value.asFloat() == 0.0 && std::signbit(result.value.asFloat()), "negative zero define");
    result = program->Call("Text");
    Check(result.ok() && result.value.asString() == "COUNT A", "string define does not recursively replace its content");
    result = program->Call("Flag"); Check(result.ok() && result.value.asBool(), "boolean define");
    result = program->Call("Empty"); Check(result.ok() && result.value.kind() == Scalar::Kind::Null, "null define");
    std::vector<std::uint8_t> image;
    Check(program->SaveImage(image, error), "save resolved define image: " + error.message);
    CompileOptions changed = options;
    changed.defines[0].text = "100";
    std::unique_ptr<Program> loaded = Program::LoadImage(image, error, changed);
    Check(loaded != nullptr, "load resolved define image: " + error.message);
    if (loaded) { result = loaded->Call("Value"); Check(result.ok() && result.value.asInt() == 98, "image retains compiled define values"); }
    StaticCompileResult staticResult;
    Check(CompileStaticSource("export int Read(){return COUNT-NEG;}", staticResult, nullptr, &options.defines), "direct frontend accepts defines");
    Check(staticResult.loweredSource.find("COUNT") == std::string::npos && staticResult.loweredSource.find("NEG") == std::string::npos, "direct frontend lowers resolved defines");
    CompileOptions wrong;
    wrong.defines.push_back(CompileDefine("BAD", CompileDefineKind::String, "\"longer text\""));
    program = Program::Compile("\nexport int Invalid(){return BAD;}", error, "diagnostic.nc", debug, wrong);
    Check(!program && error.sourceName == "diagnostic.nc" && error.line == 2 && error.column == 29, "define diagnostic retains original source location: " + std::to_string(error.column));
    const CompileDefine invalid[] = {
        {"COUNT", CompileDefineKind::Int, "9223372036854775808"},
        {"COUNT", CompileDefineKind::Int, "1+2"},
        {"COUNT", CompileDefineKind::Float, "0x10"},
        {"COUNT", CompileDefineKind::String, "\"\\uD800\""},
        {"COUNT", CompileDefineKind::String, "\"\\0\""},
        {"COUNT", CompileDefineKind::Bool, "1"},
        {"COUNT", CompileDefineKind::Null, "false"},
        {"int", CompileDefineKind::Int, "1"},
        {"bad name", CompileDefineKind::Int, "1"}
    };
    for (const CompileDefine& define : invalid)
    {
        wrong.defines.assign(1, define);
        program = Program::Compile("export int Read(){return 0;}", error, "invalid.nc", debug, wrong);
        Check(!program && !error.ok(), "unused invalid define is rejected: " + define.name + "=" + define.text);
    }
    wrong.defines.assign(2, CompileDefine("DUP", CompileDefineKind::Int, "1"));
    program = Program::Compile("export int Read(){return 0;}", error, "duplicate.nc", debug, wrong);
    Check(!program && error.message.find("duplicate") != std::string::npos, "duplicate defines rejected");
}

void CompactArrays(bool debug)
{
    using namespace NeoCore;
    Error error;
    const std::string source =
        "export int Read(){array32<int> a=array32<int>(2,5); a[0]+=2; int old=a[0]++; a.append(9); a.reserve(8); a.resize(4); a[3]=3; int sum=0; foreach(int v in a){sum+=v;} return sum+old;}"
        "export float Stored(){array32<float> a=array32<float>(1,16777216.0); float v=(a[0]+=1.0); float old=a[0]++; return v+old+a[0];}"
        "export float Maximum(){array32<float> a=array32<float>(1); a[0]=3.4028235e38; return a[0];}"
        "export int Overflow(){array32<int> a=array32<int>(1,2147483647); a[0]++; return a[0];}"
        "export int AppendOverflow(){array32<int> a=array32<int>(); a.append(2147483648); return a.len();}"
        "export int NewOverflow(){array32<int> a=array32<int>(1,2147483648); return a.len();}";
    std::unique_ptr<Program> program = Program::Compile(source, error, "compact.nc", debug);
    Check(program != nullptr, "compact array compile: " + error.message);
    if (!program) return;
    Result result = program->Call("Read");
    Check(result.ok() && result.value.asInt() == 32, "compact integer methods, typed writes and foreach: " + result.error.message);
    result = program->Call("Stored");
    Check(result.ok() && result.value.asFloat() == 50331648.0, "compact float expressions return stored rounded values: " + result.error.message);
    result = program->Call("Maximum");
    Check(result.ok() && result.value.asFloat() == static_cast<double>((std::numeric_limits<float>::max)()), "compact float maximum rounding");
    for (const char* name : {"Overflow", "AppendOverflow", "NewOverflow"})
    {
        result = program->Call(name);
        Check(!result.ok(), std::string("compact integer range failure: ") + name);
        Check(program->Reset(error), "reset after compact range failure");
    }
    for (const char* type : {"bool", "Vector3", "string", "int32", "float32"})
    {
        const std::string script = "array32<" + std::string(type) + "> a=array32<" + type + ">();";
        program = Program::Compile(script, error, "invalid.nc", debug);
        Check(!program, "reject compact array unsupported type: " + std::string(type));
    }
}
}

int main()
{
    for (bool debug : {false, true}) { Defines(debug); CompactArrays(debug); }
    std::printf("NeoCore defines and compact arrays: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
