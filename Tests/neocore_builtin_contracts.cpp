#include "NeoCore.h"
#include "NeoCoreBuiltins.h"
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

std::string Argument(char code)
{
    switch (code)
    {
    case '2': return "Vector2(1,2)";
    case '3': return "Vector3(1,2,3)";
    case '4': return "Vector4(1,2,3,4)";
    case 'b': return "true";
    case 'f': return "integer";
    case 'h': return "fn(bool success,string text)->void{}";
    case 'i': return "integer";
    case 'p': return "span";
    case 'q': return "Quaternion()";
    case 's': return "\"a\"";
    default: return "null";
    }
}

std::string Prefix()
{
    return "import math;import system;int integer=1;string text=\"abc\";"
        "TimeSpan span=system.time_span_ms(1);Stopwatch watch=system.stopwatch();"
        "AsyncRequest request=system.async_create();";
}

std::string Call(const NeoCore::BuiltinSignature& signature, unsigned count, bool badType = false)
{
    std::string owner=signature.owner;
    if (owner=="string") owner="text";
    else if (owner=="TimeSpan") owner="span";
    else if (owner=="Stopwatch") owner="watch";
    else if (owner=="AsyncRequest") owner="request";
    std::string call=owner+"."+signature.name+"(";
    for (unsigned i=0;i<count;++i)
    {
        if (i) call+=",";
        const char code=i<signature.maximum ? signature.parameters[i] : 'i';
        call+=badType && i+1==count ? (code=='s' ? "true" : "\"wrong\"") : Argument(code);
    }
    return call+");";
}

void Signatures()
{
    std::size_t count=0;
    const auto* signatures=NeoCore::GetBuiltinSignatures(count);
    for (std::size_t i=0;i<count;++i)
    {
        const auto& signature=signatures[i];
        const std::string label=std::string(signature.owner)+"."+signature.name;
        NeoCore::StaticCompileResult result;
        Check(NeoCore::CompileStaticSource(Prefix()+Call(signature,signature.minimum),result),
            label+" accepts its minimum arity: "+result.errorMessage);
        if (signature.maximum!=signature.minimum)
            Check(NeoCore::CompileStaticSource(Prefix()+Call(signature,signature.maximum),result),
                label+" accepts optional arguments: "+result.errorMessage);
        Check(!NeoCore::CompileStaticSource(Prefix()+Call(signature,signature.maximum+1),result),label+" rejects extra arguments");
        if (signature.minimum)
        {
            Check(!NeoCore::CompileStaticSource(Prefix()+Call(signature,signature.minimum-1),result),label+" rejects missing arguments");
            Check(!NeoCore::CompileStaticSource(Prefix()+Call(signature,signature.minimum,true),result),label+" rejects the wrong argument type");
        }
    }
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
    std::size_t count=0;
    const auto* signatures=NeoCore::GetBuiltinSignatures(count);
    std::string body=Prefix()+"export int Probe(){";
    for (std::size_t i=0;i<count;++i)
        if (std::string(signatures[i].owner)=="math") body+=Call(signatures[i],signatures[i].minimum);
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
