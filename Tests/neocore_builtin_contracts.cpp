#include "NeoCore.h"
#include "NeoCoreApi.h"
#include "NeoCoreCompiler.h"

#include <cstdio>
#include <string>
#include <vector>
#if defined(_WIN32) && defined(_DEBUG)
#include <crtdbg.h>
#endif

namespace
{
int checks = 0, failures = 0;
void Check(bool condition, const std::string& text)
{
    ++checks;
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", text.c_str()); }
}

// 선언에서 풀린 매개변수 타입으로 인자 식을 만든다. 등록부의 모든 서명을 같은 방법으로 검사한다.
std::string Argument(const NeoCore::TypeRegistry& types, NeoCore::TypeId type)
{
    const NeoCore::TypeDesc& description = types.Get(type);
    switch (description.kind)
    {
    case NeoCore::TypeKind::Int:
    case NeoCore::TypeKind::Float: return "integer";
    case NeoCore::TypeKind::Bool: return "true";
    case NeoCore::TypeKind::String: return "\"a\"";
    case NeoCore::TypeKind::Vector2: return "Vector2(1,2)";
    case NeoCore::TypeKind::Vector3: return "Vector3(1,2,3)";
    case NeoCore::TypeKind::Vector4: return "Vector4(1,2,3,4)";
    case NeoCore::TypeKind::Quaternion: return "Quaternion()";
    case NeoCore::TypeKind::BuiltinObject: return description.name == "TimeSpan" ? "span" : "null";
    case NeoCore::TypeKind::Function:
    {
        std::string text = "fn(";
        for (std::size_t i = 0; i < description.parameters.size(); ++i)
            text += (i ? "," : "") + types.DisplayName(description.parameters[i]) + " p" + std::to_string(i);
        const NeoCore::TypeKind result = types.Get(description.returnType).kind;
        return text + ")->" + types.DisplayName(description.returnType)
            + (result == NeoCore::TypeKind::Bool ? "{return true;}" : result == NeoCore::TypeKind::Void ? "{}" : "{return 0;}");
    }
    default: return "null";
    }
}

std::string Prefix()
{
    return "import math;import system;int integer=1;string text=\"abc\";"
        "TimeSpan span=system.time_span_ms(1);Stopwatch watch=system.stopwatch();"
        "AsyncRequest request=system.async_create();"
        "list<int> ints=[1];array<int> nums=array<int>(1);array32<int> small=array32<int>(1);"
        "map<string,int> table={\"a\":1};set<int> unique=set<int>();";
}

// 소유자마다 수신자 변수 하나를 쓴다. 타입 변수(T, K, V)는 그 수신자 타입으로 풀린다.
struct Receiver { const char* owner; const char* variable; NeoCore::TypeId type; };

std::string Call(const NeoCore::TypeRegistry& types, const NeoCore::ApiDeclaration& declaration,
    const NeoCore::ApiCallSignature& signature, const std::string& receiver, std::size_t count, bool badType = false)
{
    std::string call = receiver + "." + declaration.name + "(";
    for (std::size_t i = 0; i < count; ++i)
    {
        if (i) call += ",";
        const NeoCore::TypeId type = i < signature.parameters.size() ? signature.parameters[i] : types.Int();
        // Out 매개변수에는 같은 타입의 변수를 out 표식과 함께 넘긴다. 틀린 타입은 다른 타입의 변수로 만든다.
        if (signature.Mode(i) == NeoCore::ApiParameterMode::Out)
            call += std::string("out ") + ((type == types.String()) != (badType && i + 1 == count) ? "text" : "integer");
        else
            call += badType && i + 1 == count ? (type == types.String() ? "true" : "\"wrong\"") : Argument(types, type);
    }
    return call + ");";
}

void Signatures()
{
    NeoCore::TypeRegistry types;
    const Receiver receivers[] = {
        {"string", "text", types.String()}, {"list", "ints", types.ListOf(types.Int())},
        {"array", "nums", types.ArrayOf(types.Int())}, {"array", "small", types.Array32Of(types.Int())},
        {"map", "table", types.MapOf(types.String(), types.Int())}, {"set", "unique", types.SetOf(types.Int())},
        {"TimeSpan", "span", types.LookupNamed("TimeSpan")}, {"Stopwatch", "watch", types.LookupNamed("Stopwatch")},
        {"AsyncRequest", "request", types.LookupNamed("AsyncRequest")},
        {"math", "math", NeoCore::InvalidType}, {"system", "system", NeoCore::InvalidType}
    };
    std::size_t covered = 0;
    for (const NeoCore::ApiDeclaration& declaration : NeoCore::CoreApi())
    {
        // 서명만으로 정해지지 않는 선언은 전용 규칙의 회귀 시험이 따로 있다.
        if (declaration.Is(NeoCore::ApiCustom)) continue;
        bool found = false;
        for (const Receiver& receiver : receivers)
        {
            if (declaration.owner != receiver.owner) continue;
            found = true;
            const NeoCore::ApiCallSignature signature = NeoCore::ResolveApiSignature(types, declaration, receiver.type);
            const std::string label = NeoCore::FormatApiDeclaration(declaration) + " on " + receiver.variable;
            const std::size_t minimum = signature.minimum, maximum = signature.parameters.size();
            NeoCore::StaticCompileResult result;
            Check(NeoCore::CompileStaticSource(Prefix() + Call(types, declaration, signature, receiver.variable, minimum), result),
                label + " accepts its minimum arity: " + result.errorMessage);
            if (maximum != minimum)
                Check(NeoCore::CompileStaticSource(Prefix() + Call(types, declaration, signature, receiver.variable, maximum), result),
                    label + " accepts optional arguments: " + result.errorMessage);
            Check(!NeoCore::CompileStaticSource(Prefix() + Call(types, declaration, signature, receiver.variable, maximum + 1), result),
                label + " rejects extra arguments");
            Check(result.errorMessage.find(declaration.key) != std::string::npos, label + " names the API in the diagnostic: " + result.errorMessage);
            if (minimum)
            {
                Check(!NeoCore::CompileStaticSource(Prefix() + Call(types, declaration, signature, receiver.variable, minimum - 1), result),
                    label + " rejects missing arguments");
                Check(!NeoCore::CompileStaticSource(Prefix() + Call(types, declaration, signature, receiver.variable, minimum, true), result),
                    label + " rejects the wrong argument type");
            }
        }
        Check(found, declaration.key + " has a receiver in this test");
        ++covered;
    }
    Check(covered > 100, "the registry exposes the built-in API set");
    // 등록부에 없는 이름은 수신자 타입마다 컴파일 오류다.
    NeoCore::StaticCompileResult result;
    for (const Receiver& receiver : receivers)
        Check(!NeoCore::CompileStaticSource(Prefix() + receiver.variable + ".no_such_api();", result),
            std::string(receiver.variable) + " rejects an unregistered method");
    // 연결 이름은 소유자와 이름으로 유일하다.
    for (const NeoCore::ApiDeclaration& declaration : NeoCore::CoreApi())
        Check(NeoCore::FindCoreApiIndex(declaration.key) == static_cast<int>(declaration.index), declaration.key + " resolves to its own index");
}

void Runtime(const std::string& source, bool success, bool debug)
{
    NeoCore::Error error;
    auto program=NeoCore::Program::Compile(source,error,"contracts.nc",debug);
    Check(bool(program),"compile runtime case: "+error.message+" source: "+source);
    if (!program) return;
    const auto result=program->Call("Probe");
    Check(result.ok()==success && !result.suspended(),"runtime result: "+result.error.message+" source: "+source);
    if (!success && debug) Check(result.error.line>0,"runtime failure keeps its source location");
}

void NumericAndNullable(bool debug)
{
    NeoCore::TypeRegistry types;
    std::string body=Prefix()+"export int Probe(){";
    for (const NeoCore::ApiDeclaration& declaration : NeoCore::CoreApi())
        if (declaration.owner=="math")
        {
            const NeoCore::ApiCallSignature signature=NeoCore::ResolveApiSignature(types,declaration,NeoCore::InvalidType);
            body+=Call(types,declaration,signature,"math",signature.minimum);
        }
    body+="return 1;}";
    Runtime(body,true,debug);
    Runtime("export int Probe(){string s=\"abc\";return s.sub(9223372036854775807,2).len()+s.sub(-3,2).len();}",true,debug);
    const char* failures[]={
        "string s=null;s.sub(0,1);",
        "string s=null;\"abc\".find(s);",
        "string s=null;\"abc\".split(s);",
        "string s=null;\"abc\".replace(s,\"x\");",
        "string s=null;\"abc\".replaceAll(\"a\",s);",
        "string s=null;\"abc\".regex_match(s);",
        "string s=null;\"abc\".regex_replace(\"a\",s,true);",
        "system.utc_date(null,0);",
        "system.date(null,0);",
        "system.utc_parse_ms(null);",
        "TimeSpan s=null;system.time_span_ms(1).add(s);",
        "list<int> xs=[2,1];fn(int,int)->bool cmp=null;xs.sort(cmp);",
        "map<string,int> m={\"a\":1};fn(int,int)->bool cmp=null;m.sort(cmp);",
        "AsyncRequest request=system.async_create();request.get(1,null,fn(bool ok,string body)->void{});",
        "AsyncRequest request=system.async_create();fn(bool,string)->void cb=null;request.get(1,\"https://example.invalid\",cb);",
        "AsyncRequest request=system.async_create();request.add_header(null,\"x\");",
        "coroutine<fn()->void> co=null;coroutine.resume(co);",
        "list<int> xs=[1];xs.reserve(-1);",
        "array<int> a=array<int>(2147483648,0);",
        "array32<int> a=array32<int>(1,2147483648);",
        "system.time_span(9223372036854775807,0,0,0,0);",
        "system.utc_time_ms(1800,1,1,0,0,0,0);",
        "array<int> a=array<int>(1);int i=2;print(a[i]);",
        "set<string> s=set<string>();s.add(null);"
    };
    for (const char* statement:failures)
        Runtime(std::string("import system;import coroutine;export void Probe(){\n")+statement+"\n}",false,debug);
}
}

int main()
{
#if defined(_WIN32) && defined(_DEBUG)
    _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
#endif
    Signatures();
    NeoCore::StaticCompileResult result;
    Check(!NeoCore::CompileStaticSource("Vector3 v;int i=0;print(v[i]);",result),
        "unsupported dynamic vector indexing remains a compile error");
    NumericAndNullable(false);
    NumericAndNullable(true);
    std::printf("NeoCore builtin contracts: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
