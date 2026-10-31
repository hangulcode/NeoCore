#include "NeoCore.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
int checks=0;
void Check(bool condition,const std::string& label)
{ ++checks;if(!condition)throw std::runtime_error(label); }
std::unique_ptr<NeoCore::Program> Compile(const char* source,bool debug)
{
    NeoCore::Error error;
    auto program=NeoCore::Program::Compile(source,error,"compact-array.nc",debug);
    Check(bool(program),"compile: "+error.message);return program;
}
NeoCore::Result Invoke(const char* source,bool debug)
{
    auto program=Compile(source,debug);auto result=program->Call("Probe");
    Check(result.ok()&&!result.suspended(),"run: "+result.error.message);return result;
}
void Integer(const char* source,int64_t expected,bool debug,const char* label)
{ auto result=Invoke(source,debug);Check(result.value.kind()==NeoCore::Scalar::Kind::Int&&result.value.asInt()==expected,label); }
void Boolean(const char* source,bool debug,const char* label)
{ auto result=Invoke(source,debug);Check(result.value.kind()==NeoCore::Scalar::Kind::Bool&&result.value.asBool(),label); }
void Functional(bool debug)
{
    Integer("export int Probe(){array32<int> a=array32<int>(2,-2147483648);a[1]=2147483647;return a[0]+a[1];}",-1,debug,"int32 extremes");
    Integer("export int Probe(){array32<int> a=array32<int>();a.append(3);a.append(5);a.resize(4);return a.len()*100+a[0]*10+a[1]+a[3];}",435,debug,"append resize defaults");
    Integer("export int Probe(){array32<int> a=array32<int>(2,4);array32<int> b=a;b[1]=9;int sum=0;foreach(int n in a)sum+=n;return sum;}",13,debug,"foreach and shared identity");
    Integer("array32<int> a=array32<int>(1,4);int Change(){a[0]=9;return 2;}export int Probe(){int n=a[0]+Change();return n*10+a[0];}",69,debug,"read snapshot preserves evaluation order");
    Boolean("export bool Probe(){array32<float> a=array32<float>(1);float stored=(a[0]=1.0/3.0);return stored==a[0]&&stored!=1.0/3.0;}",debug,"assignment returns stored float32 value");
    Boolean("export bool Probe(){array32<float> a=array32<float>(1,16777216);float old=a[0]++;float current=++a[0];return old==16777216.0&&current==a[0]&&current==old;}",debug,"prefix postfix use rounded stored values");
    Boolean("export bool Probe(){array32<float> a=array32<float>(1,0.0);a.append(1.0/3.0);float result=(a[0]+=1.0/3.0);return result==a[1];}",debug,"append and compound share narrowing");
    auto maximum=Invoke("export float Probe(){array32<float> a=array32<float>(1,3.4028235e38);return a[0];}",debug);
    Check(maximum.value.asFloat()==static_cast<double>((std::numeric_limits<float>::max)()),"shortest FLT_MAX rounds finite");
    auto overflow=Invoke("export float Probe(){array32<float> a=array32<float>(1,3.4028235677973366e38);return a[0];}",debug);
    Check(std::isinf(overflow.value.asFloat())&&overflow.value.asFloat()>0,"float32 rounding boundary overflows");
    auto zero=Invoke("export float Probe(){array32<float> a=array32<float>(1,-0.0);return a[0];}",debug);
    Check(zero.value.asFloat()==0&&std::signbit(zero.value.asFloat()),"negative zero preserved");
}
void Errors(bool debug)
{
    const char* badActions[]={"a[0]=2147483648;","a[0]+=1;","++a[0];","a[0]++;","a.append(2147483648);","a[-1]=0;","a[4294967296]=0;"};
    for(const char* action:badActions)
    {
        const std::string source="array32<int> a=array32<int>(1,2147483647);export void Bad(){"+std::string(action)+"}export int Read(){return a[0];}export int Count(){return a.len();}";
        auto program=Compile(source.c_str(),debug);auto bad=program->Call("Bad");
        Check(!bad.ok()&&!bad.error.message.empty(),"invalid compact write reports an error");
        auto value=program->Call("Read");auto count=program->Call("Count");
        Check(value.ok()&&value.value.asInt()==2147483647&&count.ok()&&count.value.asInt()==1,"failed compact write leaves storage unchanged");
    }
    for(const char* source:{"array32<int> a=array32<int>(1,2147483648);","array32<int> a=array32<int>(-1);","array32<float> a=array32<float>(4294967296);"})
    { auto program=Compile(source,debug);Check(!program->Run().ok(),"invalid compact construction fails before allocation"); }
    for(const char* source:{"array32<bool> a;","array32<string> a;","array32<Vector3> a;","array32<int> a=array32<int>(1);a[0]=1.5;"})
    {
        NeoCore::Error error;auto program=NeoCore::Program::Compile(source,error,"invalid-compact.nc",debug);
        Check(!program&&!error.ok(),"unsupported compact type or narrowing rejected statically");
    }
}
void Image(bool debug)
{
    auto program=Compile("export array32<float> a=array32<float>(2,1.0/3.0);export float Probe(){a[1]+=1.0;return a[1];}",debug);
    NeoCore::Error error;std::vector<uint8_t> bytes;
    Check(program->SaveImage(bytes,error),"save compact array image: "+error.message);
    auto loaded=NeoCore::Program::LoadImage(bytes,error);
    Check(bool(loaded),"load compact array image: "+error.message);
    auto first=program->Call("Probe");auto second=loaded->Call("Probe");
    Check(first.ok()&&second.ok()&&first.value.asFloat()==second.value.asFloat(),"compact bytecode image preserves typed operations");
}
}
int main()
{
    try { for(bool debug:{false,true}){Functional(debug);Errors(debug);Image(debug);} }
    catch(const std::exception& error){std::fprintf(stderr,"NeoCore array32: %s\n",error.what());return 1;}
    std::printf("NeoCore array32: %d checks passed\n",checks);return 0;
}
