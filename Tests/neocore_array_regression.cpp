#include "NeoCore.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace
{
int checks = 0;
int failures = 0;

void Check(bool value, const std::string& label)
{
    ++checks;
    if (!value) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label.c_str()); }
}

NeoCore::Result Invoke(const char* source, const char* label, bool debug)
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

void ExpectInt(const char* source, std::int64_t expected, const char* label, bool debug)
{
    const NeoCore::Result result = Invoke(source, label, debug);
    Check(result.ok() && result.value.kind() == NeoCore::Scalar::Kind::Int && result.value.asInt() == expected,
        std::string(label) + " integer result");
}

void ExpectFloat(const char* source, double expected, const char* label, bool debug)
{
    const NeoCore::Result result = Invoke(source, label, debug);
    const bool floating = result.ok() && result.value.kind() == NeoCore::Scalar::Kind::Float;
    const double actual = floating ? result.value.asFloat() : 0;
    Check(floating && actual == expected && (expected != 0 || std::signbit(actual) == std::signbit(expected)),
        std::string(label) + " expected " + std::to_string(expected) + ", got " + std::to_string(actual));
}

void ExpectBool(const char* source, const char* label, bool debug)
{
    const NeoCore::Result result = Invoke(source, label, debug);
    Check(result.ok() && result.value.kind() == NeoCore::Scalar::Kind::Bool && result.value.asBool(),
        std::string(label) + " boolean result");
}

void ConstructionAndStorage(bool debug)
{
    ExpectBool("export bool Probe(){array<int> i=array<int>(3);array<float> f=array<float>(2);array<bool> b=array<bool>(65);return i.len()==3&&i[2]==0&&f[1]==0.0&&!b[64];}",
        "scalar array zero defaults and packed bool boundary", debug);
    ExpectBool("export bool Probe(){array<Vector2> a=array<Vector2>(1);array<Vector3> b=array<Vector3>(1);array<Vector4> c=array<Vector4>(1);array<Quaternion> q=array<Quaternion>(1);return a[0].x==0.0&&a[0].y==0.0&&b[0].z==0.0&&c[0].w==0.0&&q[0].w==1.0&&q[0].x==0.0&&q[0].y==0.0&&q[0].z==0.0;}",
        "vector array defaults and quaternion identity", debug);
    ExpectInt("export int Probe(){array<int> a=array<int>();array<float> b=array<float>();array<Vector3> c=array<Vector3>();return a.len()+b.len()+c.len();}",
        0, "empty array constructors", debug);
    ExpectInt("export int Probe(){array<int> a=array<int>(2,4294967297);a[1]=9223372036854775807;return a[0];}",
        4294967297LL, "array integer storage remains 64 bit", debug);
    ExpectFloat("export float Probe(){array<float> a=array<float>(2,16777217);a[1]=9007199254740993;return a[0];}",
        16777217, "float array retains binary64 integer promotion", debug);
    ExpectFloat("export float Probe(){array<float> a=array<float>(1);a[0]=9007199254740993;return a[0];}",
        9007199254740992.0, "float array assignment uses checked integer to double conversion", debug);
    ExpectFloat("export float Probe(){array<float> a=array<float>(1,-0.0);return a[0];}",
        -0.0, "float array preserves negative zero fill", debug);
    ExpectBool("export bool Probe(){array<bool> a=array<bool>(66,true);a[0]=false;a[64]=false;return !a[0]&&a[1]&&a[63]&&!a[64]&&a[65];}",
        "packed bool writes preserve adjacent bits", debug);
    ExpectFloat("export float Probe(){array<Vector3> a=array<Vector3>(2,Vector3(16777217,2,3));array<float> f=array<float>(1,16777217);return f[0]-a[1].x;}",
        1, "vector arrays remain binary32 while float arrays are binary64", debug);
    ExpectFloat("export float Probe(){array<Quaternion> a=array<Quaternion>(1,Quaternion(1,2,3,4));return a[0].w*1000+a[0].x*100+a[0].y*10+a[0].z;}",
        1234, "quaternion array component order", debug);
    ExpectInt("int calls=0;int Fill(){calls+=1;return 7;}export int Probe(){array<int> a=array<int>(3,Fill());return calls*100+a[0]+a[1]+a[2];}",
        121, "constructor fill expression evaluated once", debug);
    ExpectInt("int order=0;int Size(){order=order*10+1;return 2;}int Fill(){order=order*10+2;return 7;}export int Probe(){array<int> a=array<int>(Size(),Fill());return order*10+a.len();}",
        122, "constructor evaluates size before fill", debug);
}

void MethodsAndAliases(bool debug)
{
    ExpectInt("export int Probe(){array<int> a=array<int>(3,7);a[0]=9;a.resize(1);a.reserve(12);a.resize(3);a.append(11);return a.len()*1000+a[0]*100+a[1]*10+a[2]+a[3];}",
        4988, "resize regrowth restores original fill and reserve preserves size", debug);
    ExpectInt("export int Probe(){array<int> a=array<int>(3,7);a.resize(-5);a.append(3);return a.len()*10+a[0];}",
        13, "negative resize retains documented clamp to zero", debug);
    ExpectFloat("export float Probe(){array<float> a=array<float>();a.append(7);a.append(1.5);a[0]+=2;return a[0]*10+a[1];}",
        91.5, "float append and indexed compound promotion", debug);
    ExpectBool("export bool Probe(){array<bool> a=array<bool>(64,true);a.append(false);a.append(true);a.resize(1);a.resize(66);return a.len()==66&&a[0]&&a[64]&&a[65];}",
        "bool append and resize refill across word boundary", debug);
    ExpectInt("void Change(array<int> a){a[0]=5;a.append(9);}export int Probe(){array<int> a=array<int>(1,1);array<int> b=a;Change(b);return a.len()*100+a[0]*10+a[1];}",
        259, "array assignment and by-value parameters share container", debug);
    ExpectInt("array<int> data=array<int>(1,3);array<int> Get(){return data;}export int Probe(){Get().append(8);Get()[0]=7;return data.len()*100+data[0]*10+data[1];}",
        278, "function returned arrays retain shared mutable container", debug);
    ExpectInt("array<int> Replace(array<int> a){a=array<int>(2,7);return a;}export int Probe(){array<int> a=array<int>(1,3);array<int> old=a;a=Replace(a);return a.len()*100+a[1]*10+old[0];}",
        273, "explicit return replaces array while prior container remains alive", debug);
    ExpectFloat("export float Probe(){array<Vector4> a=array<Vector4>(2,Vector4(1,2,3,4));a[0].w=9;Vector4 v=a[1];v.w=8;return a[0].w*100+a[1].w*10+v.w;}",
        948, "shared vector fill and local vector copy preserve value semantics", debug);
    ExpectFloat("export float Probe(){array<Vector3> a=array<Vector3>(1,Vector3(1,2,3));array<Vector3> b=a;b[0].x=9;Vector3 copy=a[0];copy.x=7;return a[0].x*10+copy.x;}",
        97, "container aliases share but vector element reads copy", debug);
    ExpectFloat("export float Probe(){array<Vector4> a=array<Vector4>(2,Vector4(1,2,3,4));a[0].w=9;a.resize(1);a.resize(2);return a[0].w*10+a[1].w;}",
        94, "vector resize retains independent original fill", debug);
    ExpectFloat("export float Probe(){array<Quaternion> a=array<Quaternion>();a.append(Quaternion(1,2,3,4));a.resize(2);return a[0].x*10+a[1].w;}",
        21, "empty quaternion array grows with identity fill", debug);
}

void VectorWrites(bool debug)
{
    ExpectFloat("export float Probe(){array<Vector3> a=array<Vector3>(1,Vector3(1,2,3));a[0]+=Vector3(3,4,5);a[0]-=Vector3(1,2,3);a[0]*=4;a[0]/=2.0;return a[0].x*100+a[0].y*10+a[0].z;}",
        690, "whole vector array element compound operations", debug);
    ExpectFloat("export float Probe(){array<Vector2> a=array<Vector2>(1,Vector2(2,3));a[0].x+=4;a[0].x*=2;a[0].x-=2;a[0].x/=2;float old=a[0].x++;float now=++a[0].x;return old*100+now*10+a[0].y;}",
        573, "vector element component arithmetic and increment values", debug);
    ExpectFloat("export float Probe(){array<Vector4> a=array<Vector4>(1);float value=(a[0].w=16777217);return value-a[0].w;}",
        0, "component assignment result is narrowed stored value", debug);
    ExpectFloat("export float Probe(){array<Vector3> a=array<Vector3>(1,Vector3(16777216,0,0));float value=(a[0].x+=1);return value;}",
        16777216, "component compound result is narrowed stored value", debug);
    ExpectFloat("export float Probe(){array<Vector3> a=array<Vector3>(1,Vector3(16777216,0,0));float pre=++a[0].x;float post=a[0].x++;return pre+post-a[0].x;}",
        16777216, "component prefix and postfix retain float32 storage precision", debug);
    ExpectFloat("export float Probe(){array<Quaternion> a=array<Quaternion>(1);a[0].x=2;a[0].w+=3;return a[0].w*10+a[0].x;}",
        42, "quaternion element component writes preserve nominal layout", debug);
    ExpectFloat("array<Vector3> data=array<Vector3>(1,Vector3(1,2,3));array<Vector3> Get(){return data;}export float Probe(){Get()[0].z+=4;return data[0].z;}",
        7, "factory returned array vector component writes back", debug);
    ExpectFloat("export float Probe(){array<Vector4> a=array<Vector4>(1,Vector4(1,2,3,4));Vector4 result=(a[0]+=Vector4(1,1,1,1));result.w=9;return a[0].w*10+result.w;}",
        59, "whole-vector element compound expression returns independent value", debug);
}

void EvaluationOrder(bool debug)
{
    ExpectInt("int order=0;array<Vector3> data=array<Vector3>(1);float Rhs(){order=order*10+1;return 3;}array<Vector3> Target(){order=order*10+2;return data;}int Index(){order=order*10+3;return 0;}export int Probe(){Target()[Index()].x=Rhs();return order;}",
        123, "component assignment executes RHS then container then index once", debug);
    ExpectInt("int order=0;array<Vector3> data=array<Vector3>(1);float Rhs(){order=order*10+1;return 3;}array<Vector3> Target(){order=order*10+2;return data;}int Index(){order=order*10+3;return 0;}export int Probe(){Target()[Index()].x+=Rhs();return order;}",
        123, "component compound executes RHS then container then index once", debug);
    ExpectInt("int order=0;array<Vector4> data=array<Vector4>(1);Vector4 Rhs(){order=order*10+1;return Vector4(1,2,3,4);}array<Vector4> Target(){order=order*10+2;return data;}int Index(){order=order*10+3;return 0;}export int Probe(){Target()[Index()]+=Rhs();return order;}",
        123, "whole-vector element compound executes RHS then container then index once", debug);
    ExpectInt("int calls=0;array<Vector4> data=array<Vector4>(1);int Index(){calls+=1;return 0;}export int Probe(){data[Index()].w++;++data[Index()].w;return calls;}",
        2, "increment component index is evaluated once", debug);
    ExpectFloat("array<Vector3> data=array<Vector3>(1,Vector3(1,2,3));float Rhs(){data=array<Vector3>(1,Vector3(9,8,7));return 2;}export float Probe(){array<Vector3> old=data;data[0].x=Rhs();return data[0].x*100+data[0].y*10+old[0].x;}",
        281, "component write resolves rebound array after RHS", debug);
    ExpectFloat("array<Vector3> data=array<Vector3>(1,Vector3(1,2,3));Vector3 Rhs(){data=array<Vector3>(1,Vector3(9,8,7));return Vector3(2,3,4);}export float Probe(){array<Vector3> old=data;data[0]+=Rhs();return data[0].x*100+data[0].y*10+old[0].x;}",
        1211, "whole-vector element compound resolves rebound array after RHS", debug);
    // 대상 배열 값을 얻은 뒤 인덱스 함수가 변수를 다시 묶어도 이미 선택한 컨테이너는 유지한다.
    ExpectFloat("array<Vector3> data=array<Vector3>(1,Vector3(1,2,3));array<Vector3> Get(){return data;}int Index(){data=array<Vector3>(1,Vector3(9,8,7));return 0;}export float Probe(){array<Vector3> old=data;Get()[Index()].x=5;return old[0].x*10+data[0].x;}",
        59, "index side effects do not replace previously evaluated container", debug);
    ExpectFloat("array<Vector3> data=array<Vector3>(1,Vector3(1,2,3));int Index(){data=array<Vector3>(1,Vector3(9,8,7));return 0;}export float Probe(){array<Vector3> old=data;data[Index()].x=5;return old[0].x*10+data[0].x;}",
        59, "direct indexed target snapshots container before index rebinds variable", debug);
    ExpectInt("array<int> data=array<int>(1,1);int Change(){data=array<int>(1,9);return 7;}export int Probe(){array<int> old=data;data.append(Change());return old.len()*100+old[1]*10+data[0];}",
        279, "array method snapshots receiver before argument rebinds variable", debug);
    ExpectFloat("array<Vector4> data=array<Vector4>(1,Vector4(1,2,3,4));Vector4 Change(){data[0].w=9;return Vector4(0,0,0,2);}export float Probe(){Vector4 result=data[0]+Change();return result.w*10+data[0].w;}",
        69, "vector array binary operand snapshots before RHS changes source element", debug);
    ExpectFloat("array<Vector4> data=array<Vector4>(1,Vector4(1,2,3,4));float Rhs(){data.resize(0);data.resize(1);data[0].w=7;return 2;}export float Probe(){data[0].w+=Rhs();return data[0].w;}",
        9, "component write resolves element after RHS resizes container", debug);
    ExpectFloat("array<Vector3> data=array<Vector3>(1,Vector3(1,2,3));int Index(){data.resize(0);data.resize(1);data[0].x=7;return 0;}export float Probe(){data[Index()].x+=2;return data[0].x;}",
        9, "element is resolved after index resizes shared array", debug);
}

void RuntimeFailures(bool debug)
{
    // 표현 범위 밖 크기를 사용하므로 테스트가 실제 대용량 버퍼를 할당해서는 안 된다.
    const char* statements[] = {
        "array<int> a=array<int>(-1);",
        "array<float> a=array<float>(4294967296);",
        "array<int> a=array<int>(2147483648);",
        "array<int> a=array<int>();a.reserve(-1);",
        "array<int> a=array<int>();a.reserve(4294967296);",
        "array<int> a=array<int>();a.reserve(2147483648);",
        "array<bool> a=array<bool>();a.resize(4294967296);",
        "array<int> a=array<int>(1);int n=a[-1];",
        "array<float> a=array<float>(1);a[1]=3;",
        "array<Vector3> a=array<Vector3>(1);float n=a[4294967296].x;",
        "array<int> a=array<int>(1);int n=a[2147483648];",
        "array<Vector4> a=array<Vector4>(1);a[4294967296].w+=1;",
        "array<Quaternion> a=array<Quaternion>();a[0].w++;"
    };
    for (const char* statement : statements)
    {
        const std::string source = std::string("export int Probe(){\n") + statement + "\nreturn 0;}";
        NeoCore::Error error;
        std::unique_ptr<NeoCore::Program> program = NeoCore::Program::Compile(source, error, "bad_array.nc", debug);
        Check(program != nullptr, std::string("runtime failure source compiles: ") + statement + ": " + error.message);
        if (!program) continue;
        const NeoCore::Result result = program->Call("Probe");
        Check(!result.ok() && !result.error.message.empty(), std::string("array runtime failure: ") + statement);
        if (debug) Check(result.error.line == 2, std::string("array failure retains source line: ") + statement);
    }
    NeoCore::Error error;
    std::unique_ptr<NeoCore::Program> program = NeoCore::Program::Compile(
        "array<Vector3> data=array<Vector3>(1);int Index(){data.resize(0);return 0;}"
        "export int Probe(){data[Index()].x+=1;return 0;}", error, "resize_during_index.nc", debug);
    Check(program != nullptr, "resize during index source compiles: " + error.message);
    if (program) Check(!program->Call("Probe").ok(), "index shrink rejects missing element before component write");
}

void Rejections(bool debug)
{
    const char* sources[] = {
        "array<int> a;", "array<string> a=array<string>();", "array<array<int>> a=array<array<int>>();",
        "array<int?> a=array<int?>();", "array<int>? a=null;int value=a[0];",
        "array<int> a=array<int>(1.5);", "array<int> a=array<int>(true);",
        "array<int> a=array<int>(1,1.5);", "array<float> a=array<float>(1,true);",
        "array<bool> a=array<bool>(1,1);", "array<int> a=array<int>(1,2,3);",
        "array<int> a=array<int>(1);a[0]=1.5;", "array<int> a=array<int>(1);a[0]=true;",
        "float Value(){return 1.5;}array<int> a=array<int>(1);a[0]=Value();",
        "float Value(){return 1.5;}array<int> a=array<int>();a.append(Value());",
        "array<int> a=array<int>(1);int x=a[0.0];", "array<int> a=array<int>(1);int& r=a[0];",
        "void F(int& x){}array<int> a=array<int>(1);F(a[0]);",
        "array<Vector3> a=array<Vector3>(1);Vector3& v=a[0];",
        "array<Vector3> a=array<Vector3>(1);float& x=a[0].x;",
        "array<int> a=array<int>();array<float> b=a;",
        "array<Vector4> a=array<Vector4>();array<Quaternion> b=a;",
        "array<Quaternion> a=array<Quaternion>(1,Vector4());",
        "array<Vector4> a=array<Vector4>(1,Quaternion());",
        "array<Vector3> a=array<Vector3>(1);a[0]=Vector2();",
        "array<Quaternion> a=array<Quaternion>(1);a[0]+=Quaternion();",
        "array<int> a=array<int>();a.append(1.5);", "array<float> a=array<float>();a.append(true);",
        "array<int> a=array<int>();a.resize();", "array<int> a=array<int>();a.resize(1,2);",
        "array<int> a=array<int>();a.reserve(1.0);", "array<int> a=array<int>();int x=a.len(1);",
        "array<int> a=array<int>();a.append();", "array<int> a=array<int>();a.append(1,2);",
        "array<int> a=array<int>();a.missing();", "array<Vector2> a=array<Vector2>(1);float z=a[0].z;"
    };
    for (const char* source : sources)
    {
        NeoCore::Error error;
        const std::unique_ptr<NeoCore::Program> program = NeoCore::Program::Compile(source, error, "reject_array.nc", debug);
        Check(!program && !error.ok(), std::string("reject incompatible array source: ") + source);
    }
}

void HostBoundary(bool debug)
{
    NeoCore::Error error;
    std::unique_ptr<NeoCore::Program> program = NeoCore::Program::Compile(
        "int zero=0;int fail=1/zero;export array<int> Probe(){return array<int>();}",
        error, "scalar_host_array_return.nc", debug);
    Check(program != nullptr, "array return source compiles: " + error.message);
    if (!program) return;
    const NeoCore::Result result = program->Call("Probe");
    Check(!result.ok() && result.error.message.find("division by zero") != std::string::npos,
        "supported host array call reports global initialization failure: " + result.error.message);
    const NeoCore::Result initialized = program->Run();
    Check(!initialized.ok() && initialized.error.message.find("division by zero") != std::string::npos,
        "failed global initialization is retained");
    program = NeoCore::Program::Compile("export array<int> Probe(){return array<int>(2,7);}", error,
        "host_array_return.nc", debug);
    Check(program != nullptr, "host array return compiles");
    if (program)
    {
        const NeoCore::Result array = program->Call("Probe");
        Check(array.ok() && array.value.kind() == NeoCore::Scalar::Kind::Array && array.value.Count() == 2 &&
            array.value.At(1).asInt() == 7, "host receives typed array values");
    }
}
}

int main()
{
    for (int mode = 0; mode != 2; ++mode)
    {
        const bool debug = mode != 0;
        ConstructionAndStorage(debug);
        MethodsAndAliases(debug);
        VectorWrites(debug);
        EvaluationOrder(debug);
        RuntimeFailures(debug);
        Rejections(debug);
        HostBoundary(debug);
    }
    std::printf("NeoCore array regression: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
