#include "NeoCore.h"
#include "NeoCoreCompiler.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
int checks = 0, failures = 0;

void Check(bool value, const std::string& label)
{
    ++checks;
    if (!value) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label.c_str()); }
}

void Value(const char* source, const NeoCore::Scalar& expected, bool debug)
{
    NeoCore::Error error;
    auto program = NeoCore::Program::Compile(source, error, "conditional.nc", debug);
    Check(bool(program), "compile: " + error.message + " source: " + source);
    if (!program) return;
    for (int image = 0; image < 2; ++image)
    {
        const NeoCore::Result result = program->Call("Probe");
        bool valid = result.ok() && !result.suspended() && result.value.kind() == expected.kind();
        if (valid && expected.kind() == NeoCore::Scalar::Kind::Int) valid = result.value.asInt() == expected.asInt();
        if (valid && expected.kind() == NeoCore::Scalar::Kind::Float)
            valid = result.value.asFloat() == expected.asFloat() &&
                (expected.asFloat() != 0 || std::signbit(result.value.asFloat()) == std::signbit(expected.asFloat()));
        if (valid && expected.kind() == NeoCore::Scalar::Kind::Bool) valid = result.value.asBool() == expected.asBool();
        if (valid && expected.kind() == NeoCore::Scalar::Kind::String) valid = result.value.asString() == expected.asString();
        Check(valid, "selected result: " + result.error.message + " source: " + source);
        if (image == 0)
        {
            std::vector<std::uint8_t> bytes;
            Check(program->SaveImage(bytes, error), "save image: " + error.message);
            program = NeoCore::Program::LoadImage(bytes, error);
            Check(bool(program), "load image: " + error.message);
            if (!program) return;
        }
    }
}

void Matrix(bool debug)
{
    const struct IntegerCase { const char* source; std::int64_t result; } cases[] = {
        {"export int Probe(){int a=7;int b=3;a==b?a:b;return a==b?a:b;}",3},
        {"export int Probe(){int a=3;int b=3;int d=3;int e=9;int f=4;int k=0;k=(a==b?a:b)==d?e:f;return k;}",9},
        {"export int Probe(){int a=7;int b=3;int d=7;int e=9;int f=4;int k=0;k=(a==b?a:b)==d?e:f;return k;}",4},
        {"export int Probe(){return false?1:true?2:3;}",2},
        {"export int Probe(){return true?false?1:2:3;}",2},
        {"export int Probe(){return false||true&&false?1:2+3*4;}",14},
        {"export int Probe(){int x=0;int k=true?x=4:8;return k*10+x;}",44},
        {"export int Probe(){int x=0;int k=false?4:(x=8);return k*10+x;}",88},
        {"int trace=0;bool Cond(){trace=trace*10+1;return false;}int A(){trace=trace*10+2;return 7;}int B(){trace=trace*10+3;return 8;}export int Probe(){int v=Cond()?A():B();return trace*10+v;}",138},
        {"int trace=0;bool Cond(){trace=trace*10+1;return true;}int A(){trace=trace*10+2;return 7;}int B(){trace=trace*10+3;return 8;}export int Probe(){int v=Cond()?A():B();return trace*10+v;}",127},
        {"int x=3;int Change(){x=7;return 11;}export int Probe(){int y=x+(true?Change():0);return y*10+x;}",147},
        {"int x=3;int Change(){x=7;return 11;}export int Probe(){x=true?x+Change():0;return x;}",14},
        {"int x=0;int F(int a,int b){return a*10+b;}export int Probe(){return F(true?(x=2):0,x);}",22},
        {"export int Probe(){int divisor=0;return true?7:1/divisor;}",7},
        {"export int Probe(){return true?7:1/0;}",7},
        {"export int Probe(){int divisor=0;return false?1/divisor:8;}",8},
        {"export int Probe(){map<string,int> m={};return false?m[\"missing\"]:9;}",9},
        {"export int Probe(){map<string,int> m={true?\"a\":\"b\":true?3:7};return m[true?\"a\":\"b\"];}",3},
        {"export int Probe(){bool c=false;list<int> a=c?[]:[7,8];return a.len()*10+a[0];}",27},
        {"export int Probe(){bool c=true;map<string,int> m=c?{\"k\":7}:{};return m[\"k\"];}",7},
        {"export int Probe(){list<int> a=[1];list<int> b=[2];list<int> selected=false?a:b;selected[0]=9;return a[0]*10+b[0];}",19},
        {"struct S{int n;};export int Probe(){S a=S(1);S b=S(2);S selected=true?a:b;selected.n=9;return a.n*10+b.n;}",12},
        {"struct Inner{int n;};struct S{Inner i;};export int Probe(){S a=S(Inner(1));S b=S(Inner(2));S selected=false?a:b;selected.i.n=9;return a.i.n*10+b.i.n;}",12},
        {"export int Probe(){Vector3 a=Vector3(1,2,3);Vector3 b=Vector3(4,5,6);Vector3 selected=false?a:b;selected.x=9;return toint(a.x*10+b.x);}",14},
        {"int A(int x){return x+3;}int B(int x){return x+7;}export int Probe(){fn(int)->int f=false?A:B;return f(2);}",9},
        {"export int Probe(){int seed=5;fn()->int f=true?fn()->int{return seed;}:null;return f();}",5},
        {"export int Probe(){string s=null;string t=false?\"a\":null;if(t==null)return 7;return 0;}",7},
        {"export int Probe(){int total=0;for(int i in 0,100){string text=i%2==0?\"alpha\":\"b\";total+=text.len();}return total;}",300},
        {"export int Probe(){int total=0;for(int i in false?9:0,3){total+=i;}return total;}",3},
        {"export int Probe(){int n=0;while(n<3?true:false)n+=1;return n;}",3},
        {"const bool C=true;const int K=C?7:1/0;struct S{int n=C?9:1/0;};export int Probe(){S s;switch(s.n){case C?9:1/0:return K;default:return 0;}}",7},
        {"const bool C=false;struct S{Vector3 p=C?Vector3(9,9,9):Vector3(1,2,3);string name=C?\"a\":\"bb\";};export int Probe(){S s;return toint(s.p.z)*10+s.name.len();}",32}
    };
    for (const auto& item : cases) Value(item.source, NeoCore::Scalar::Int(item.result), debug);
    Value("export string Probe(){bool c=true;string k=c?\"b\":\"a\";return k;}",NeoCore::Scalar::String("b"),debug);
    Value("export string Probe(){string k=\"old\";k=false?k:\"new\";return k;}",NeoCore::Scalar::String("new"),debug);
    Value("export float Probe(){bool c=false;return c?7:2.5;}",NeoCore::Scalar::Float(2.5),debug);
    Value("export float Probe(){bool c=true;return c?7:2.5;}",NeoCore::Scalar::Float(7),debug);
    Value("export float Probe(){bool c=true;return c?-0.0:0.0;}",NeoCore::Scalar::Float(-0.0),debug);
    Value("const float N=true?7:2.5;export float Probe(){return N;}",NeoCore::Scalar::Float(7),debug);
    Value("export bool Probe(){bool c=false;return (c?null:null)==null;}",NeoCore::Scalar::Bool(true),debug);
}

void Reject(const char* source, const char* message)
{
    NeoCore::StaticCompileResult checked;
    Check(!NeoCore::CompileStaticSource(source,checked) && checked.errorMessage.find(message)!=std::string::npos,
        "reject: "+std::string(source)+" error: "+checked.errorMessage);
}

void Errors(bool debug)
{
    NeoCore::Error error;
    auto program=NeoCore::Program::Compile(
        "export int Probe(){\nint divisor=0;\nreturn false ? 7 :\n1/divisor;\n}",error,"branch-error.nc",debug);
    Check(bool(program),"runtime error fixture: "+error.message);
    if (!program) return;
    auto result=program->Call("Probe");
    Check(!result.ok() && result.error.message.find("division by zero")!=std::string::npos,"selected branch reports its error");
    if(debug) Check(result.error.line==4,"selected branch error retains its source line");
    program=NeoCore::Program::Compile(
        "int count=0;int Task(){count+=1;sleep(0);return 7;}export int Probe(){return true?Task():0;}export int Count(){return count;}",
        error,"branch-sleep.nc",debug);
    Check(bool(program),"suspension fixture: "+error.message);
    if (!program) return;
    auto instance=program->CreateInstance(error);
    Check(bool(instance),"suspension instance");
    if (!instance) return;
    Check(instance->Run().ok(),"suspension instance initialization");
    result=instance->Call("Probe");
    Check(result.ok() && result.suspended(),"selected branch may suspend");
    result=instance->Resume(0);
    Check(result.ok() && !result.suspended() && result.value.asInt()==7,"conditional resumes with selected result");
    result=instance->Call("Count");
    Check(result.ok() && result.value.asInt()==1,"conditional does not repeat its call after suspension");
}

void Modules(bool debug)
{
    NeoCore::CompileOptions options;
    options.moduleLoader=[](const std::string& name,const std::string&,NeoCore::ModuleSource& output,NeoCore::Error&) {
        if(name!="choices") return false;
        output.sourceName="choices.nc";
        output.source="public struct S{int n;};public S Pick(bool c){return c?S(3):S(7);}";
        return true;
    };
    NeoCore::Error error;
    auto program=NeoCore::Program::Compile(
        "import choices;export int Probe(){choices.S s=choices.Pick(false);return (s.n==7?s.n:3)==7?9:4;}",
        error,"module-conditional.nc",debug,options);
    Check(bool(program),"module conditional: "+error.message);
    if(!program) return;
    const auto result=program->Call("Probe");
    Check(result.ok() && result.value.asInt()==9,"module struct conditional return and nested comparison");
}
}

int main()
{
    for(bool debug:{false,true}) { Matrix(debug); Errors(debug); Modules(debug); }
    for(const char* condition:{"1","0.0","\"x\"","null","Vector3()"})
    {
        const std::string source="int n="+std::string(condition)+"?1:2;";
        Reject(source.c_str(),"condition requires 'bool'");
    }
    Reject("list<int> a=[1];int n=a?1:2;","condition requires 'bool'");
    Reject("int n=true?1:\"x\";","conditional branches");
    Reject("int n=true?1:null;","conditional branches");
    Reject("int n=true?1:2.5;","cannot convert");
    Reject("int n=true?1:missing;","unknown variable");
    Reject("void A(){}void B(){}true?A():B();","conditional branches");
    Reject("Vector3 v=true?Vector3():Vector2();","conditional branches");
    Reject("int a=1;int b=2;(true?a:b)=3;","assignment requires");
    Reject("int n=true?1;","expected ':'");
    Reject("int n=true?:2;","expected an expression");
    Reject("const int N=true?1:unknown;","unknown variable");
    std::printf("NeoCore conditional expressions: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
