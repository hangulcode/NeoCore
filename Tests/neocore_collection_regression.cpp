#include "NeoCore.h"

#include <cstdio>
#include <string>

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

static NeoCore::Result Invoke(const char* source, const char* label, bool debug)
{
    NeoCore::Error error;
    std::unique_ptr<NeoCore::Program> program = NeoCore::Program::Compile(source, error, label, debug);
    Check(program != nullptr, std::string(label) + " compile: " + error.message);
    if (!program)
    {
        NeoCore::Result result;
        result.error = error;
        return result;
    }
    NeoCore::Result result = program->Call("Probe");
    Check(result.ok(), std::string(label) + " execute: " + result.error.message);
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

static void CheckLists(bool debug)
{
    ExpectInt("export int Probe(){list<int> a=[];return a.len();}", 0,
        "contextual empty list", debug);
    ExpectInt("export int Probe(){list<int> a=[1,2,3];return a.len()*100+a[0]*10+a[2];}", 313,
        "integer list literal and length", debug);
    ExpectFloat("export float Probe(){list<float> a=[1,2.5];return a[0]+a[1];}", 3.5,
        "float list literal promotes integer elements", debug);
    ExpectInt("export int Probe(){list<bool> a=[true,false];if(a[0]&&!a[1])return 1;return 0;}", 1,
        "boolean list literal", debug);
    ExpectString("export string Probe(){list<string> a=[\"a\",\"b\"];a[1]=\"c\";return a[0]..a[1];}", "ac",
        "string list values", debug);
    ExpectInt("export int Probe(){list<int> a=[];a.resize(3);a[1]=7;return a[0]+a[1]+a[2]+a.len();}", 10,
        "integer resize initializes every new element", debug);
    ExpectFloat("export float Probe(){list<float> a=[];a.resize(2);a[0]=3;return a[0]+a[1];}", 3.0,
        "float resize and writes retain float storage", debug);
    ExpectInt("export int Probe(){list<bool> a=[];a.resize(2);if(a[0]||a[1])return 99;a[1]=true;if(a[1])return 1;return 0;}", 1,
        "boolean resize initializes false", debug);
    ExpectInt("export int Probe(){list<int> a=[3,4,5];a.resize(1);a.resize(3);return a[0]*100+a[1]*10+a[2];}", 300,
        "resize preserves retained elements and resets growth", debug);
    ExpectInt("export int Probe(){list<int> a=[1,2];a.resize(0);return a.len();}", 0,
        "resize to zero", debug);
    ExpectString("export string Probe(){list<string> a=[\"a\"];a.resize(3,\"b\");return a[0]..a[1]..a[2];}", "abb",
        "string resize uses explicit nonnull fill", debug);
    ExpectFloat("export float Probe(){list<float> a=[];a.resize(2,1);return a[0]+a[1];}", 2.0,
        "resize fill materializes float promotion", debug);
    ExpectInt("int calls=0;int Fill(){calls++;return 7;}export int Probe(){list<int> a=[];a.resize(3,Fill());return a[0]+a[1]+a[2]+calls;}", 22,
        "resize fill evaluates once", debug);
    ExpectInt("export int Probe(){list<int> a=[1];list<int> b=a;b[0]=7;return a[0];}", 7,
        "list assignment aliases storage", debug);
    ExpectInt("void Change(list<int> a){a[0]=8;}export int Probe(){list<int> a=[1];Change(a);return a[0];}", 8,
        "list parameter aliases caller storage", debug);
    ExpectInt("list<int> values=[1];list<int> Read(){return values;}export int Probe(){list<int> a=Read();a[0]=9;return values[0];}", 9,
        "returned list aliases existing storage", debug);
    ExpectInt("export int Probe(){list<int> a=[1];list<int> b=a;b=[2];return a[0]*10+b[0];}", 12,
        "list rebinding does not replace another alias", debug);
    ExpectInt("void Change(list<int> a){a.resize(2);a[1]=5;}export int Probe(){list<int> a=[1];Change(a);return a.len()*10+a[1];}", 25,
        "resize is observable through list aliases", debug);
    ExpectInt("export int Probe(){list<int> a=[2];a[0]+=3;int old=a[0]++;return old*10+a[0];}", 56,
        "indexed integer assignment operators", debug);
    ExpectFloat("export float Probe(){list<float> a=[1.0];a[0]=2;a[0]+=3;return a[0];}", 5.0,
        "indexed float assignments materialize widening", debug);
    ExpectInt("int count=0;int Index(){count++;return 0;}export int Probe(){list<int> a=[3];int x=a[Index()];return x*10+count;}", 31,
        "list read evaluates the index once", debug);
}

static void CheckMaps(bool debug)
{
    ExpectInt("export int Probe(){map<string,int> m={};return m.get(\"missing\",7);}", 7,
        "missing map key uses explicit fallback", debug);
    ExpectInt("export int Probe(){map<string,int> m={};m[\"x\"]=2;m[\"x\"]=3;return m.get(\"x\",9);}", 3,
        "map indexed insert and overwrite", debug);
    ExpectInt("export int Probe(){map<string,int> m={};m[\"x\"]=2;m[\"x\"]=3;m[\"y\"]=4;return m.len();}", 2,
        "map length counts distinct keys", debug);
    ExpectInt("export int Probe(){map<int,int> m={};m[1]=2;m[-3]=4;return m.get(1,0)*10+m.get(-3,0);}", 24,
        "integer map keys", debug);
    ExpectFloat("export float Probe(){map<string,float> m={};m[\"x\"]=2;return m.get(\"x\",0);}", 2.0,
        "map float values promote on write", debug);
    ExpectFloat("export float Probe(){map<string,float> m={};return m.get(\"missing\",3);}", 3.0,
        "map float fallback promotes integer", debug);
    ExpectInt("export int Probe(){map<int,bool> m={};m[1]=false;if(m.get(1,true))return 99;if(m.get(2,true))return 1;return 0;}", 1,
        "false map value is not treated as a missing key", debug);
    ExpectString("export string Probe(){map<string,string> m={};m[\"x\"]=\"\";return m.get(\"x\",\"bad\")..m.get(\"missing\",\"ok\");}", "ok",
        "empty string map value is not a missing key", debug);
    ExpectInt("export int Probe(){map<string,int> m={};m[\"x\"]=4;int present=m[\"x\"];if(present==4&&!m.contains(\"absent\"))return 1;return 0;}", 1,
        "raw map reads produce the value type", debug);
    ExpectInt("export int Probe(){map<string,string> m={};m[\"x\"]=\"v\";string present=m[\"x\"];string missing=m[\"absent\"];if(present==\"v\"&&missing==null)return 1;return 0;}", 1,
        "missing reference map reads are null", debug);
    {
        NeoCore::Error error;
        std::unique_ptr<NeoCore::Program> program = NeoCore::Program::Compile(
            "export int Probe(){map<string,int> m={};return m[\"missing\"];}", error, "missing raw map result", debug);
        Check(program != nullptr, "missing raw map result compile: " + error.message);
        const NeoCore::Result missing = program ? program->Call("Probe") : NeoCore::Result();
        Check(program && !missing.ok() && missing.error.message.find("map key not found") != std::string::npos,
            "missing value-typed map read fails at runtime: " + missing.error.message);
    }
    const NeoCore::Result present = Invoke("export int Probe(){map<string,int> m={};m[\"x\"]=8;return m[\"x\"];}",
        "present raw map result", debug);
    Check(present.ok() && present.value.kind() == NeoCore::Scalar::Kind::Int && present.value.asInt() == 8,
        "present raw map value retains scalar storage");
    ExpectInt("export int Probe(){map<string,int> a={};a[\"x\"]=1;map<string,int> b=a;b[\"x\"]=9;return a.get(\"x\",0);}", 9,
        "map assignment aliases storage", debug);
    ExpectInt("void Change(map<string,int> m){m[\"x\"]=6;}export int Probe(){map<string,int> m={};Change(m);return m.get(\"x\",0);}", 6,
        "map parameter aliases caller storage", debug);

    // get의 인자는 왼쪽부터 한 번씩 평가하고, 기본값도 조회 성공 여부와 무관하게 평가한다.
    ExpectInt("int calls=0;int Fallback(){calls++;return 9;}export int Probe(){map<string,int> m={};m[\"x\"]=2;int value=m.get(\"x\",Fallback());return value*10+calls;}", 21,
        "map fallback evaluates eagerly", debug);
    ExpectInt("int calls=0;string Key(){calls++;return \"x\";}export int Probe(){map<string,int> m={};m[\"x\"]=2;int value=m.get(Key(),9);return value*10+calls;}", 21,
        "map get evaluates key once", debug);
    ExpectInt("map<string,int> m={};int Change(){m[\"x\"]=9;return 0;}export int Probe(){m[\"x\"]=2;return m.get(\"x\",Change());}", 9,
        "map lookup occurs after eager fallback side effect", debug);
    ExpectInt("map<string,int> m={};int Insert(){m[\"x\"]=9;return 1;}export int Probe(){return m.get(\"x\",Insert());}", 9,
        "eager fallback can insert a previously missing key", debug);
    ExpectInt("map<string,int> m={};int Replace(){m={};m[\"x\"]=9;return 0;}export int Probe(){m[\"x\"]=2;return m.get(\"x\",Replace());}", 2,
        "map receiver is captured before fallback rebinds it", debug);
    ExpectInt("export int Probe(){map<string,int> m={};m[\"get\"]=5;m[\"len\"]=7;return m.get(\"get\",0)*10+m.get(\"len\",0);}", 57,
        "stored method-like keys do not shadow typed get", debug);
    ExpectInt("export int Probe(){map<string,int> m={};m[\"len\"]=99;return m.len();}", 1,
        "stored len key does not shadow typed length", debug);
}

static void CheckStrings(bool debug)
{
    ExpectInt("export int Probe(){string s=\"abc\";return s.len();}", 3,
        "ASCII string length", debug);
    ExpectInt("export int Probe(){string s=\"A\\uD83D\\uDE42\\uAC00\";return s.len();}", 3,
        "Unicode length counts code points", debug);
    ExpectInt("export int Probe(){string s=\"e\\u0301\";return s.len();}", 2,
        "Unicode length does not merge combining code points", debug);
    ExpectInt("export int Probe(){string s=\"\";return s.len();}", 0,
        "empty string length", debug);
}

static void CheckRejections(bool debug)
{
    const struct { const char* source; const char* label; } invalid[] = {
        {"list a=[];", "list requires an element type"},
        {"list<int> a=[1,true];", "heterogeneous list literal"},
        {"list<int> a=[1,2.5];", "list literal narrowing"},
        {"list<float> a=[true];", "boolean is not a numeric list element"},
        {"list<string> a=[1];", "string list requires explicit string conversion"},
        {"list<int> a=[null];", "nonnull list element"},
        {"list<int> a=[1];a[0]=2.5;", "list write narrowing"},
        {"list<int> a=[1];a[0]+=2.5;", "list compound narrowing"},
        {"list<int> a=[1];int n=a[0.0];", "floating list read index"},
        {"list<int> a=[1];a[\"0\"]=2;", "string list write index"},
        {"list<int> a=[1];int n=a[true];", "boolean list read index"},
        {"list<int> a=[];a.resize(1.5);", "resize requires integer length"},
        {"list<int> a=[];a.resize();", "resize requires one length"},
        {"list<int> a=[1];int n=a.len(1);", "list length takes no arguments"},
        {"list<int> a=[];a.resize(1,2.5);", "resize fill narrowing"},
        {"list<int?> a=[];", "nullable marker on list elements"},
        {"list<int> a=[1];list<float> b=a;", "lists are invariant"},
        {"list<int> a=[1];int& r=a[0];", "list element reference is not implemented"},
        {"void Set(int& r){}list<int> a=[1];Set(a[0]);", "list element reference argument is not implemented"},
        {"map<string,int> m={};m[1]=2;", "map write key type"},
        {"map<int,int> m={};m[\"x\"]=2;", "integer map key does not accept string"},
        {"map<string,int> m={};m[\"x\"]=2.5;", "map write narrowing"},
        {"map<string,int> m={};m[\"x\"]=null;", "nonnull map value cannot be null"},
        {"map<string,int> m={};int n=m.get(1,0);", "get key type"},
        {"map<string,int> m={};int n=m.get(\"x\",1.5);", "get fallback narrowing"},
        {"map<string,int> m={};int n=m.get(\"x\",null);", "get requires nonnull fallback"},
        {"map<string,int> m={};int n=m.get(\"x\");", "get requires explicit fallback"},
        {"map<string,int> m={};int n=m.get(\"x\",0,1);", "get rejects extra argument"},
        {"map<string,int> m={};map<string,float> other=m;", "maps are invariant"},
        {"map<string,int?> m={};", "nullable marker on map values"},
        {"map<float,int> m={};", "floating map keys are deferred"},
        {"map<string,int> m={};int& r=m[\"x\"];", "map element reference is not implemented"},
        {"string s=\"a\";int n=s.len(1);", "string length takes no arguments"},
        {"list<int> a=[1];int n=a.unknown();", "unknown collection method does not bypass checking"}
    };
    for (const auto& item : invalid)
    {
        NeoCore::Error error;
        const std::unique_ptr<NeoCore::Program> program = NeoCore::Program::Compile(item.source, error, item.label, debug);
        Check(!program && !error.ok(), std::string("reject ") + item.label);
    }
}

int main()
{
    for (int mode = 0; mode < 2; ++mode)
    {
        const bool debug = mode != 0;
        CheckLists(debug);
        CheckMaps(debug);
        CheckStrings(debug);
        CheckRejections(debug);
    }
    std::printf("NeoCore collection regression: %d/%d passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
