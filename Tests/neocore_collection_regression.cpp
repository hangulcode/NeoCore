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
    ExpectInt("export int Probe(){map<string,int> m={};m[\"x\"]=2;m[\"x\"]=3;return m[\"x\"];}", 3,
        "map indexed insert and overwrite", debug);
    ExpectInt("export int Probe(){map<string,int> m={};m[\"x\"]=2;m[\"y\"]=4;return m.len();}", 2,
        "map length counts distinct keys", debug);
    ExpectInt("export int Probe(){map<int,int> m={1:2,-3:4};return m[1]*10+m[-3];}", 24,
        "integer map keys", debug);
    ExpectFloat("export float Probe(){map<string,float> m={};m[\"x\"]=2;return m[\"x\"];}", 2.0,
        "map float values promote on write", debug);
    ExpectInt("export int Probe(){map<int,bool> m={1:false};if(m[1])return 99;if(m.contains(1)&&!m.contains(2))return 1;return 0;}", 1,
        "false map value is present", debug);
    ExpectString("export string Probe(){map<string,string> m={\"x\":\"\"};if(m.contains(\"x\"))return m[\"x\"];return \"bad\";}", "",
        "empty string map value is present", debug);
    ExpectInt("import system;export int Probe(){map<string,string> m=system.jsonDecode<map<string,string>>(\"{\\\"x\\\":null}\");if(m.contains(\"x\")&&m[\"x\"]==null&&!m.contains(\"absent\"))return 1;return 0;}", 1,
        "present null differs from an absent key", debug);
    ExpectInt("export int Probe(){map<string,int> a={\"x\":1};map<string,int> b=a;b[\"x\"]=9;return a[\"x\"];}", 9,
        "map assignment aliases storage", debug);
    ExpectInt("void Change(map<string,int> m){m[\"x\"]=6;}export int Probe(){map<string,int> m={\"x\":1};Change(m);return m[\"x\"];}", 6,
        "map parameter aliases caller storage", debug);
    ExpectInt("int calls=0;string Key(){calls++;return \"x\";}export int Probe(){map<string,int> m={\"x\":2};int value=m[Key()];return value*10+calls;}", 21,
        "map read evaluates key once", debug);
    ExpectInt("map<string,int> m={\"x\":2};string Replace(){m={\"x\":9};return \"x\";}export int Probe(){return m[Replace()];}", 2,
        "map receiver is captured before key rebinds it", debug);
    ExpectInt("map<string,int> m={\"x\":2};string Replace(){m={};return \"x\";}export int Probe(){if(m.contains(Replace()))return 1;return 0;}", 1,
        "contains captures receiver before key rebinds it", debug);
    ExpectInt("map<string,int> m={\"x\":2};string Change(){m[\"x\"]=9;return \"x\";}export int Probe(){return m[Change()];}", 9,
        "map key effects happen before lookup", debug);
    ExpectInt("int calls=0;string Key(){calls++;return \"x\";}export int Probe(){map<string,int> m={\"x\":2};if(m.contains(Key()))return calls;return 0;}", 1,
        "contains evaluates key once", debug);
    ExpectInt("export int Probe(){map<string,int> m={\"get\":5,\"len\":7};return m[\"get\"]*10+m[\"len\"];}", 57,
        "method-like keys remain readable", debug);
    ExpectInt("export int Probe(){map<string,int> m={\"contains\":99};if(m.contains(\"contains\"))return m.len();return 0;}", 1,
        "stored method key does not shadow typed contains", debug);
    ExpectInt("export int Probe(){map<string,int> m={\"x\":2};string key=null;if(!m.contains(key))return 1;return 0;}", 1,
        "contains null string key is false", debug);
    ExpectInt("struct Item{int x;};export int Probe(){map<string,Item> m={\"x\":Item(3)};Item v=m[\"x\"];v.x=9;return m[\"x\"].x;}", 3,
        "map struct reads preserve value copies", debug);
    for (const char* source : {
        "export int Probe(){map<string,int> m={};return m[\"absent\"];}",
        "export string Probe(){map<string,string> m={};return m[\"absent\"];}",
        "export list<int> Probe(){map<int,list<int>> m={};return m[7];}"
    })
    {
        NeoCore::Error error;
        auto program=NeoCore::Program::Compile(source,error,"strict map read",debug);
        Check(program!=nullptr,"strict map read compiles: "+error.message);
        const NeoCore::Result result=program?program->Call("Probe"):NeoCore::Result();
        Check(program && !result.ok() && result.error.message.find("map key not found")!=std::string::npos,
            "absent key fails for every value type: "+result.error.message);
    }
    for (const char* source : {
        "export int Probe(){map<string,int> m=null;return m[\"x\"];}",
        "export bool Probe(){map<int,int> m=null;return m.contains(1);}"
    })
    {
        NeoCore::Error error;
        auto program=NeoCore::Program::Compile(source,error,"null map",debug);
        const NeoCore::Result result=program?program->Call("Probe"):NeoCore::Result();
        Check(program && !result.ok() && result.error.message.find("null map")!=std::string::npos,
            "null map receiver fails: "+result.error.message);
    }
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
        {"map<string,int> m={};int n=m.get(\"x\",0);", "removed map.get fallback"},
        {"map<string,int> m={};int n=m.get(\"x\");", "removed map.get"},
        {"map<string,int> m={};int n=m[1];", "map read key type"},
        {"map<string,int> m={};bool found=m.contains(1);", "contains key type"},
        {"map<string,int> m={};bool found=m.contains();", "contains requires a key"},
        {"map<string,int> m={};bool found=m.contains(\"x\",\"y\");", "contains rejects extra arguments"},
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

static void CheckAssignmentOrder(bool debug)
{
    const char* source = R"NC(
int trace=0;
int F(){trace=trace*10+1;return 0;}
int G(){trace=trace*10+2;return 10;}
export int List(){trace=0;list<int> x=[10];x[F()]=x[F()]+G();return trace*100+x[0];}
export int Array(){trace=0;array<int> x=array<int>(1,10);x[F()]=x[F()]+G();return trace*100+x[0];}
export int Compact(){trace=0;array32<int> x=array32<int>(1,10);x[F()]=x[F()]+G();return trace*100+x[0];}
export int Map(){trace=0;map<int,int> x={0:10};x[F()]=x[F()]+G();return trace*100+x[0];}
list<int> values=[1];
list<int> previous=values;
int Replace(){values=[9];return 4;}
export int Rebind(){values[0]=Replace();return values[0]*10+previous[0];}
int value=7;
int Key(){value=11;return 0;}
export int Snapshot(){list<int> x=[1];int result=(x[Key()]=value);return result*100+x[0]*10+value;}
export int Results(){list<int> x=[1];int sum=(x[0]=2)+(x[0]=3);return sum*10+x[0];}
export int Nested(){trace=0;list<list<int>> x=[[1]];x[F()][F()]=G();return trace*10+x[0][0];}
export int StringMap(){trace=0;map<string,string> x={};x[tostring(F())]=tostring(G());return trace*10+x["0"].len();}
int Named(){return 7;}
export int Functions(){list<fn()->int> a=[null];map<string,fn()->int> m={};a[0]=Named;m["f"]=Named;return a[0]()*10+m["f"]();}
struct Copy {int n;};
export int StructCopy(){Copy value=Copy(1);list<Copy> a=[Copy(0)];Copy result=(a[0]=value);value.n=2;result.n=3;return a[0].n;}
export int NullValues(){list<string> a=["old"];map<string,string> m={"null":"old"};string result=(a[0]=null);m["null"]=null;if(result==null&&a[0]==null&&!m.contains("null"))return 1;return 0;}
export int LocalSnapshot(){list<int> a=[0];int value=7;a[(value=0)]=value;return a[0]*10+value;}
int shared=5;
int Change(){shared=9;return 3;}
export int ConsumedSnapshot(){list<int> a=[0];int result=(a[0]=shared)+Change();return result*100+a[0]*10+shared;}
export int ConsumedLocalSnapshot(){list<int> a=[0];int value=5;int result=(a[0]=value)+(value=9);return result*100+a[0]*10+value;}
)NC";
    NeoCore::Error error;
    auto program=NeoCore::Program::Compile(source,error,"collection-order.nc",debug);
    Check(bool(program),"assignment order compile: "+error.message);
    if(!program) return;
    std::vector<std::uint8_t> image;
    Check(program->SaveImage(image,error),"assignment order image: "+error.message);
    for(int pass=0;pass<2;++pass)
    {
        if(pass) program=NeoCore::Program::LoadImage(image,error);
        Check(bool(program),"assignment order load: "+error.message);
        if(!program) return;
        const struct Case { const char* name; std::int64_t expected; } cases[]={
            {"List",12120},{"Array",12120},{"Compact",12120},{"Map",12120},
            {"Rebind",41},{"Snapshot",781},{"Results",53},{"Nested",2120},{"StringMap",212},
            {"Functions",77},{"StructCopy",1},{"NullValues",1},
            {"LocalSnapshot",70},{"ConsumedSnapshot",859},{"ConsumedLocalSnapshot",1459}
        };
        for(const auto& test:cases)
        {
            const auto result=program->Call(test.name);
            Check(result.ok() && result.value.kind()==NeoCore::Scalar::Kind::Int && result.value.asInt()==test.expected,
                std::string("assignment order ")+test.name+": "+result.error.message);
        }
    }
    const auto failing=NeoCore::Program::Compile(
        "int calls=0;int Key(){calls+=1;return 0;}export int Probe(){list<int> x=[1];\nx[Key()]=1/0;return 0;}"
        "export int State(){return calls;}",error,"collection-write-error.nc",debug);
    Check(bool(failing),"failing assignment compile: "+error.message);
    if(failing)
    {
        const auto failure=failing->Call("Probe");
        Check(!failure.ok() && failure.error.message.find("integer division by zero")!=std::string::npos &&
            (!debug || failure.error.line==2),"RHS error precedes index and retains source line");
        const auto state=failing->Call("State");
        Check(state.ok() && state.value.asInt()==0,"failed RHS does not evaluate target index");
    }
}

static void CheckTryGet(bool debug)
{
    ExpectInt("export int Probe(){map<string,int> m={\"x\":7};int v=99;if(!m.try_get(\"x\",out v))return -1;int a=v;if(m.try_get(\"missing\",out v))return -2;return a*100+v;}",707,
        "try_get hits and misses preserve output",debug);
    ExpectInt("int v=8;export int Probe(){map<int,int> m={3:0};if(!m.try_get(3,out v))return -1;return v;}",0,
        "try_get zero value and global output",debug);
    ExpectInt("export int Probe(){map<int,bool> m={3:false};bool v=true;if(!m.try_get(3,out v)||v)return -1;return 1;}",1,
        "try_get false is a present value",debug);
    ExpectFloat("export float Probe(){map<int,float> m={3:1.25};float v=9.0;m.try_get(3,out v);return v;}",1.25,
        "try_get floating output and discarded result",debug);
    ExpectString("import system;export string Probe(){map<string,string> m=system.jsonDecode<map<string,string>>(\"{\\\"x\\\":null}\");string v=\"old\";if(!m.try_get(\"x\",out v)||v!=null)return \"bad\";v=\"keep\";string k=null;if(m.try_get(k,out v))return \"bad key\";return v;}","keep",
        "try_get present null and null query key",debug);
    ExpectString("export string Probe(){map<string,string> m={\"x\":\"result\"};string key=\"x\";if(!m.try_get(key,out key))return \"bad\";return key;}","result",
        "try_get output may also be the key variable",debug);
    ExpectInt("export int Probe(){map<int,list<int>> m={1:[2]};list<int> v=[];if(!m.try_get(1,out v))return -1;v[0]=9;return m[1][0];}",9,
        "try_get collections retain shared identity",debug);
    ExpectFloat("export float Probe(){map<int,Vector3> m={1:Vector3(2,3,4)};Vector3 v=Vector3(0,0,0);if(!m.try_get(1,out v))return -1.0;v.x=9;return m[1].x+v.x;}",11,
        "try_get vectors copy by value",debug);
    ExpectInt("struct Inner{int n;};struct Item{Inner i;list<int> shared;};export int Probe(){map<int,Item> m={1:Item(Inner(2),[3])};Item v=Item();if(!m.try_get(1,out v))return -1;v.i.n=9;v.shared[0]=7;return m[1].i.n*100+v.i.n*10+m[1].shared[0];}",297,
        "try_get structs copy nested values and share collection fields",debug);
    ExpectInt("map<int,int> m={1:3};int calls=0;int Key(){calls++;m={1:9};return 1;}export int Probe(){int v=0;m.try_get(Key(),out v);return v*10+calls;}",31,
        "try_get captures receiver before evaluating key once",debug);
    ExpectInt("int v=0;int Key(){v=6;return 2;}export int Probe(){map<int,int> m={};m.try_get(Key(),out v);return v;}",6,
        "try_get miss preserves key evaluation effects",debug);
    ExpectInt("export int Probe(){map<int,int> m={1:3};int v=8;if(false&&m.try_get(1,out v))return -1;bool b=true?m.try_get(1,out v):false;return b?v:99;}",3,
        "try_get obeys conditional and short circuit execution",debug);
    ExpectInt("export int Probe(){map<int,int> m={1:4};int out=0;if(!m.try_get(1,out out))return -1;return out;}",4,
        "out remains a legal variable name",debug);
    ExpectInt("export int Probe(){map<int,fn(int)->int> m={1:fn(int n)->int{return n+2;}};fn(int)->int v=null;if(!m.try_get(1,out v))return -1;return v(3);}",5,
        "try_get function values",debug);
    ExpectInt("import system;export int Probe(){list<int> items=[2];map<int,set<int>> m={1:system.set(items)};set<int> v=null;if(!m.try_get(1,out v))return -1;return v.contains(2)?1:0;}",1,
        "try_get set values",debug);
    ExpectInt("int Read(int v){map<int,int> m={1:7};m.try_get(1,out v);return v;}export int Probe(){int v=3;return Read(v)*10+v;}",73,
        "try_get parameter output remains by value",debug);
    ExpectInt("export int Probe(){map<int,int> m={1:7};int v=2;fn()->int f=fn()->int{m.try_get(1,out v);return v;};return f()*10+v;}",72,
        "try_get captured output uses the closure snapshot",debug);
    const struct {const char* source;const char* diagnostic;} invalid[]={
        {"void F(out int x){}","out parameters belong to registered API signatures only"},
        {"void F(int x){}int x=0;F(out x);","out is only allowed for an API parameter declared out"},
        {"fn(out int)->void f=null;","out parameters belong to registered API signatures only"},
        {"fn(int)->void f=fn(out int x)->void{};","out parameters belong to registered API signatures only"},
        {"map<int,int> m={};int x=0;m.try_get(1,x);","'map.try_get' argument 2 'value' is an out parameter"},
        {"map<int,int> m={};float x=0.0;m.try_get(1,out x);","requires an out variable of type 'int', not 'float'"},
        {"map<int,int> m={};const int x=0;m.try_get(1,out x);","cannot modify a const value"},
        {"map<int,int> m={};list<int> x=[0];m.try_get(1,out x[0]);","requires a variable after 'out'"},
        {"struct S{int x;};map<int,int> m={};S s=S();m.try_get(1,out s.x);","requires a variable after 'out'"},
        {"map<int,int> m={};int x=0;m.try_get(\"bad\",out x);","'map.try_get' argument 1 'key' requires 'int', not 'string'"},
        {"map<int,int> m={};int x=0;m.contains(out x);","'map.contains' argument 1 'key' is not an out parameter"},
        {"list<int> m=[];int x=0;m.try_get(1,out x);","unknown or unsupported typed method 'try_get'"},
        {"void F(int x){}fn(int)->void f=F;int x=0;f(out x);","out is only allowed for an API parameter declared out"},
        {"struct out{int n;};","out marks an output argument and cannot name a type"},
        {"map<int,int> m={};int x=0;m.try_get(out x,1);","'map.try_get' argument 1 'key' is not an out parameter"},
        {"map<int,int> m={};int x=0;m.try_get(1);","'map.try_get' requires 2 arguments, not 1"},
        {"import math;float x=0.0;float y=math.sin(out x);","'math.sin' argument 1 'radian' is not an out parameter"},
        {"struct S{int x;};S s=S(1,out s);","out is only allowed for an API parameter declared out"}
    };
    for(const auto& test:invalid)
    {
        NeoCore::Error error;
        auto program=NeoCore::Program::Compile(test.source,error,"try-get-error.nc",debug);
        Check(!program && error.message.find(test.diagnostic)!=std::string::npos,
            std::string("try_get rejected: ")+test.source+" => "+error.message);
    }
    NeoCore::Error error;
    auto nullMap=NeoCore::Program::Compile("export int Probe(){int v=7;map<int,int> m=null;\nm.try_get(1,out v);return v;}",error,"try-get-null.nc",debug);
    Check(bool(nullMap),"null try_get compile: "+error.message);
    if(nullMap)
    {
        const auto result=nullMap->Call("Probe");
        Check(!result.ok() && result.error.message.find("null map")!=std::string::npos && (!debug||result.error.line==2),"try_get null map error and source line");
    }
    NeoCore::CompileOptions options;
    options.moduleLoader=[](const std::string& name,const std::string&,NeoCore::ModuleSource& module,NeoCore::Error&)->bool{
        module.sourceName=name+".nc";
        module.source=name=="helper" ? "public int target=1;int Value(int v){return v;}public int Read(){map<int,int> m={1:7};int value=0;m.try_get(1,out value);return value+Value(2);}"
            : name=="badcall" ? "void F(int x){}public int Read(){int x=0;F(out x);return x;}"
            : name=="baddecl" ? "public void G(out int x){}"
            : "public int Read(){map<int,int> m={1:7};int out=0;m.try_get(1,out out);return out;}";
        return true;
    };
    auto program=NeoCore::Program::Compile("import helper as h;import vars as v;export int Probe(){map<int,int> m={1:9};m.try_get(1,out h.target);return h.Read()+v.Read()+h.target;}",error,"try-get-entry.nc",debug,options);
    Check(bool(program),"try_get imported module compile: "+error.message);
    if(program)
    {
        std::vector<std::uint8_t> bytes;
        Check(program->SaveImage(bytes,error),"try_get image save: "+error.message);
        auto loaded=NeoCore::Program::LoadImage(bytes,error);
        Check(bool(loaded),"try_get image load: "+error.message);
        if(loaded){const auto result=loaded->Call("Probe");Check(result.ok()&&result.value.asInt()==25,"try_get imported module image result: "+result.error.message);}
    }
    const struct {const char* source;const char* diagnostic;} modules[]={
        {"import badcall as b;export int Probe(){return b.Read();}","out is only allowed for an API parameter declared out"},
        {"import baddecl as b;export int Probe(){return 0;}","out parameters belong to registered API signatures only"}
    };
    for(const auto& test:modules)
    {
        NeoCore::Error moduleError;
        auto rejected=NeoCore::Program::Compile(test.source,moduleError,"try-get-module-error.nc",debug,options);
        Check(!rejected && moduleError.message.find(test.diagnostic)!=std::string::npos,
            std::string("out rejected inside an imported module: ")+test.source+" => "+moduleError.message);
    }
}

int main()
{
    for (int mode = 0; mode < 2; ++mode)
    {
        const bool debug = mode != 0;
        CheckLists(debug);
        CheckAssignmentOrder(debug);
        CheckMaps(debug);
        CheckTryGet(debug);
        CheckStrings(debug);
        CheckRejections(debug);
    }
    std::printf("NeoCore collection regression: %d/%d passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
