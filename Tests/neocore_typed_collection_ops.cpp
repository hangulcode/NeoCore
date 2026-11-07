#if defined(_WIN32) && defined(_DEBUG)
#include <crtdbg.h>
#endif
#include "NeoCoreCompiler.h"
#include "NeoCoreHostBridge.h"
#include "NeoScript.h"
#include "NeoVMProgram.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace NeoScript;

namespace
{
int checks = 0;
int failures = 0;

void Check(bool condition, const std::string& label)
{
    ++checks;
    if (!condition && ++failures <= 80) std::fprintf(stderr, "FAIL: %s\n", label.c_str());
}

struct Outcome
{
    bool valid = false;
    RunStatus status = RunStatus::Failed;
    ValueType type = ValueType::None;
    std::int64_t integer = 0;
    double number = 0;
    bool boolean = false;
    Error error;
};

Outcome Int(std::int64_t value)
{
    Outcome result; result.valid = true; result.status = RunStatus::Completed;
    result.type = ValueType::Int; result.integer = value; return result;
}

Outcome Float(double value)
{
    Outcome result; result.valid = true; result.status = RunStatus::Completed;
    result.type = ValueType::Float; result.number = value; return result;
}

Outcome Bool(bool value)
{
    Outcome result; result.valid = true; result.status = RunStatus::Completed;
    result.type = ValueType::Bool; result.boolean = value; return result;
}

bool SameValue(const Outcome& a, const Outcome& b)
{
    if (!a.valid || !b.valid || a.status != RunStatus::Completed || b.status != RunStatus::Completed || a.type != b.type) return false;
    if (a.type == ValueType::Int) return a.integer == b.integer;
    if (a.type == ValueType::Bool) return a.boolean == b.boolean;
    if (a.type == ValueType::Float)
        return (std::isnan(a.number) && std::isnan(b.number)) || std::memcmp(&a.number, &b.number, sizeof(double)) == 0;
    return a.type == ValueType::None;
}

Outcome Invoke(IRuntime& runtime, InstanceHandle instance, const char* name)
{
    Outcome result;
    auto call = runtime.Call(instance, name);
    result.valid = true;
    result.status = call.invoke();
    if (result.status != RunStatus::Completed) { result.error = call.error(); return result; }
    result.type = call.retType();
    if (result.type == ValueType::Int) result.integer = call.retInt();
    else if (result.type == ValueType::Float) result.number = call.retFloat();
    else if (result.type == ValueType::Bool) result.boolean = call.retBool();
    return result;
}

std::vector<SVMOperation> Operations(const std::vector<uint8_t>& bytes, bool expand = true)
{
    std::vector<SVMOperation> result;
    if (bytes.size() < sizeof(SNeoVMHeader)) return result;
    SNeoVMHeader header;
    std::memcpy(&header, bytes.data(), sizeof(header));
    if (header._iCodeSize < 0 || header._iCodeSize % sizeof(SVMOperation) != 0 ||
        static_cast<std::size_t>(header._iCodeSize) > bytes.size() - sizeof(header)) return result;
    result.resize(static_cast<std::size_t>(header._iCodeSize) / sizeof(SVMOperation));
    if (!result.empty()) std::memcpy(result.data(), bytes.data() + sizeof(header), header._iCodeSize);
    if (expand)
    {
        CNeoVMProgram* program = CNeoVMProgram::Create(bytes.data(), static_cast<int>(bytes.size()));
        if (program)
        {
            for (const auto& plan : program->fusions)
                for (unsigned i = 0; i < plan.count; ++i) result[plan.start + i] = plan.operations[i];
            program->Release();
        }
    }
    return result;
}

bool CompileImage(IRuntime& runtime, const char* source, bool debug,
    std::vector<uint8_t>& bytes, NeoCore::StaticCompileResult& checked)
{
    Check(NeoCore::CompileStaticSource(source, checked), "shape frontend: " + checked.errorMessage);
    if (!checked.success) return false;
    CompileDesc desc;
    desc.source = checked.loweredSource;
    desc.coreIntrinsicPrefix = checked.backendIntrinsicPrefix;
    desc.coreTypedIR = &checked.typedIR;
    desc.sourceName = "aggregate-shape.nc";
    desc.includeDebugInfo = debug;
    const Error error = runtime.CompileToBytecode(desc, bytes);
    Check(error.ok(), "shape bytecode: " + error.message);
    return error.ok();
}

Outcome Run(IRuntime& runtime, const std::string& source, bool debug, bool image,
    const std::string& label)
{
    NeoCore::StaticCompileResult checked;
    Check(NeoCore::CompileStaticSource(source, checked), label + " frontend: " + checked.errorMessage);
    if (!checked.success) return Outcome();
    CompileDesc desc;
    desc.source = checked.loweredSource;
    desc.coreIntrinsicPrefix = checked.backendIntrinsicPrefix;
    desc.coreTypedIR = &checked.typedIR;
    desc.sourceName = "aggregate-ops.nc";
    desc.includeDebugInfo = debug;
    ProgramHandle program;
    Error error;
    if (image)
    {
        std::vector<uint8_t> bytes;
        error = runtime.CompileToBytecode(desc, bytes);
        if (error.ok()) program = runtime.LoadProgram(bytes, &error);
    }
    else
    {
        const CompileResult compiled = runtime.Compile(desc);
        program = compiled.program; error = compiled.error;
    }
    Check(bool(program) && error.ok(), label + " backend compile/load: " + error.message);
    if (!program) return Outcome();
    if (debug) Check(NeoCoreBackend::RemapSourceLines(runtime, program, checked.hostWriteSourceLines), label + " source mapping");
    const InstanceHandle instance = runtime.CreateInstance(program);
    Check(bool(instance), label + " instance");
    Outcome result;
    if (instance)
    {
        result = Invoke(runtime, instance, "Probe");
        Check(SameValue(Invoke(runtime, instance, "Recovery"), Int(29)), label + " recovery and retained state");
        runtime.DestroyInstance(instance);
    }
    runtime.DestroyProgram(program);
    return result;
}

void Compare(IRuntime& runtime, const char* source, const Outcome& expected, bool debug,
    const char* label, int errorLine = 0, const char* recovery = "29")
{
    const std::string complete = std::string(source) + "\nexport int Recovery(){return " + recovery + ";}";
    for (bool image : {false, true})
    {

        const Outcome typed = Run(runtime, complete, debug, image, std::string(label) + " typed");
        if (errorLine == 0)
        {

            Check(SameValue(typed, expected), std::string(label) + " expected result");
        }
        else
        {
            // 직접 명령에는 생성 함수 프레임이 없다. 오류 종류와 원본 줄·호출 뒤 상태를 비교한다.
            Check(typed.valid && typed.status == RunStatus::Failed,
                std::string(label) + " fails at runtime");
            Check(!typed.error.ok() && !typed.error.message.empty(),
                std::string(label) + " runtime error category retained");
            if (debug)
            {
                Check(typed.error.line == static_cast<uint32_t>(errorLine), std::string(label) + " typed source line: " + typed.error.message);

                // 기존 원시 이미지 형식은 원본 파일명 목록을 저장하지 않는다. 줄과 호출 프레임은 보존한다.
                const bool sourceMatches = image || typed.error.sourceName == "aggregate-ops.nc";
                Check(sourceMatches && typed.error.message.find("Probe") != std::string::npos,
                    std::string(label) + " direct failure retains source and caller frame; image=" + std::to_string(image) +
                    " source='" + typed.error.sourceName + "' error=" + typed.error.message);
            }
        }
    }
}

bool IsRuntimeCall(OperationCode op)
{
    return op == NOP_CALL || op == NOP_CALL_L || op == NOP_PTRCALL || op == NOP_PTRCALL_L ||
        op == NOP_PTRCALL2 || op == NOP_PTRCALL2_L || op == NOP_NATIVECALL;
}

bool IsGenericAggregate(OperationCode op)
{
    return op == NOP_CLT_READ || op == NOP_CLT_READ_L || op == NOP_CLT_READ_STATIC_STRING ||
        op == NOP_CLT_MOV || op == NOP_CLT_MOV_L || op == NOP_CLT_WRITE_REF ||
        op == NOP_VEC_MAKE;
}

void CheckShape(IRuntime& runtime, const char* source, const std::vector<OperationCode>& required,
    bool debug, const char* label)
{
    {
        std::vector<uint8_t> bytes;
        NeoCore::StaticCompileResult checked;
        if (!CompileImage(runtime, source, debug, bytes, checked)) return;
        const auto operations = Operations(bytes);
        for (const auto& op : operations)
        {
            {
                Check(!IsRuntimeCall(op.Code()), std::string(label) + " has no runtime helper/library call");
                Check(!IsGenericAggregate(op.Code()), std::string(label) + " has no generic collection operation; opcode=" +
                    std::to_string(op.Code()) + " operands=" + std::to_string(op.n1) + "," + std::to_string(op.n2) + "," +
                    std::to_string(op.n3) + " flags=" + std::to_string(op.argFlag) + " source=" + checked.loweredSource);
            }
        }
        {
            for (OperationCode expected : required)
            {
                bool found = false;
                for (const auto& op : operations) found = found || ArithmeticMeaning(op.Code()) == expected;
                const AggregateOpInfo* info = GetAggregateOpInfo(expected);
                const TypedOpInfo* scalar = GetTypedOpInfo(expected);
                Check((info || scalar) && found, std::string(label) + " emits " + (info ? info->name : scalar ? scalar->name : "missing metadata"));
            }
        }

        Error error;
        ProgramHandle loaded = runtime.LoadProgram(bytes, &error);
        Check(bool(loaded) && error.ok(), std::string(label) + " dedicated image loads: " + error.message);
        if (loaded) runtime.DestroyProgram(loaded);
        for (std::size_t i = 0; i < operations.size(); ++i)
        {
            const AggregateOpInfo* info = GetAggregateOpInfo(operations[i].Code());
            if (!info || info->shape != AggregateOpShape::Window) continue;
            for (int change = 0; change < 3; ++change)
            {
                auto altered = Operations(bytes, false);
                if (change == 0) altered[i].argFlag &= ~NEOS_ARG_N2_LOCAL;
                else if (change == 1) altered[i].n2 = 32767;
                else altered[i].n3 = static_cast<decltype(altered[i].n3)>(info->controlMax + 1);
                std::vector<uint8_t> malformed = bytes;
                std::memcpy(malformed.data() + sizeof(SNeoVMHeader), altered.data(), altered.size() * sizeof(SVMOperation));
                ProgramHandle rejected = runtime.LoadProgram(malformed, &error);
                Check(!rejected && !error.ok(), std::string(label) + " invalid input window/control rejected");
                if (rejected) runtime.DestroyProgram(rejected);
            }
        }
    }
}

void Shapes(IRuntime& runtime, bool debug)
{
    CheckShape(runtime, "export bool Probe(set<int> s,int key){bool a=s.add(key);bool b=s.contains(key);bool c=s.remove(key);return a&&b&&c;}",
        {NOP_SET_I64_ADD,NOP_SET_I64_CONTAINS,NOP_SET_I64_REMOVE}, debug, "integer set operations have no native call");
    CheckShape(runtime, "export bool Probe(set<string> s,string key){bool a=s.add(key);bool b=s.contains(key);bool c=s.remove(key);return a&&b&&c;}",
        {NOP_SET_STRING_ADD,NOP_SET_STRING_CONTAINS,NOP_SET_STRING_REMOVE}, debug, "string set operations have no native call");
    struct NumericShape { const char* operation; OperationCode op; };
    const NumericShape numeric[] = {
        {"%",OP_I64_MOD},{"&",OP_I64_AND},{"|",OP_I64_OR},
        {"^",OP_I64_XOR},{"<<",OP_I64_SHL},{">>",OP_I64_SHR}
    };
    for (const auto& shape : numeric)
    {
        const std::string source = "export int Probe(int a,int b){return a" + std::string(shape.operation) + "b;}";
            {
            std::vector<uint8_t> bytes;
            NeoCore::StaticCompileResult checked;
            if (!CompileImage(runtime, source.c_str(), debug, bytes, checked)) continue;
            int count = 0;
            for (const auto& op : Operations(bytes)) if (ArithmeticMeaning(op.Code()) == shape.op) ++count;
            Check(count == 1, std::string("dedicated scalar integer operation ") + shape.operation);
        }
    }
    CheckShape(runtime, "export float Probe(float a,float b){Vector2 v=Vector2(a,b);Vector2 w=Vector2(b,a);v+=w;v-=w;v*=a;v/=b;v.x=a;return v.y;}",
        {NOP_V2_MAKE,OP_V2_ADD,OP_V2_SUB,OP_V2_MUL,OP_V2_DIV,NOP_V2_GET,NOP_V2_SET}, debug, "Vector2 primitive shape");
    CheckShape(runtime, "export float Probe(float a,float b){Vector3 v=Vector3(a,b,a);Vector3 w=Vector3(b,a,b);v+=w;v-=w;v*=a;v/=b;v.z=a;return v.y;}",
        {NOP_V3_MAKE,OP_V3_ADD,OP_V3_SUB,OP_V3_MUL,OP_V3_DIV,NOP_V3_GET,NOP_V3_SET}, debug, "Vector3 primitive shape");
    CheckShape(runtime, "export float Probe(float a,float b){Vector4 v=Vector4(a,b,a,b);Vector4 w=Vector4(b,a,b,a);v+=w;v-=w;v*=a;v/=b;v.w=a;return v.y;}",
        {NOP_V4_MAKE,OP_V4_ADD,OP_V4_SUB,OP_V4_MUL,OP_V4_DIV,NOP_V4_GET,NOP_V4_SET}, debug, "Vector4 primitive shape");
    CheckShape(runtime, "export float Probe(float a){Quaternion q=Quaternion(1,a,2,3);q.w=a;return q.x;}",
        {NOP_V4_MAKE,NOP_V4_GET,NOP_V4_SET}, debug, "Quaternion uses validated Vec4 physical ops");
    CheckShape(runtime, "export float Probe(float a){Vector3 v=Vector3(a,2,3);v.x=a;v=Vector3(4,5,6);return v.z;}",
        {NOP_CELL_GET,NOP_CELL_SET}, debug, "internal vector storage uses direct cell operations");
    {
        std::vector<uint8_t> bytes;
        NeoCore::StaticCompileResult checked;
        const char* source = "struct P{float x;};struct S{P p;Vector3 v;};"
            "S Next(S s){s.p.x+=1;return s;}export float Probe(){S s=S(P(2),Vector3(1,2,3));"
            "s=Next(s);s.v.x+=4;return s.p.x+s.v.x;}";
        if (CompileImage(runtime, source, debug, bytes, checked))
        {
            for (const SVMOperation& op : Operations(bytes))
                Check(op.Code() != NOP_CELL_NEW && op.Code() != NOP_CELL_GET && op.Code() != NOP_CELL_SET,
                    "struct roots and fields require no alias cells");
            Check(checked.loweredSource.find("__neocore_assign_") == std::string::npos,
                "struct replacement requires no in-place assignment helper");
        }
    }
    CheckShape(runtime, "export int Probe(int n){array<int> a=array<int>(n,7);a[0]=9;a.append(5);a.resize(4);a.reserve(8);return a[0]+a.len();}",
        {NOP_ARRAY_I64_NEW,NOP_ARRAY_I64_GET,NOP_ARRAY_I64_STORE,NOP_ARRAY_I64_APPEND,NOP_ARRAY_LEN,NOP_ARRAY_RESIZE,NOP_ARRAY_RESERVE}, debug, "int array primitive shape");
    CheckShape(runtime, "export float Probe(int n){array<float> a=array<float>(n,1.5);a[0]=2.5;a.append(3.5);return a[0];}",
        {NOP_ARRAY_F64_NEW,NOP_ARRAY_F64_GET,NOP_ARRAY_F64_STORE,NOP_ARRAY_F64_APPEND}, debug, "float array primitive shape");
    CheckShape(runtime, "export bool Probe(int n){array<bool> a=array<bool>(n,true);a[0]=false;a.append(true);return a[0];}",
        {NOP_ARRAY_BOOL_NEW,NOP_ARRAY_BOOL_GET,NOP_ARRAY_BOOL_STORE,NOP_ARRAY_BOOL_APPEND}, debug, "bool array primitive shape");
    CheckShape(runtime, "export float Probe(int n){array<Vector2> a=array<Vector2>(n,Vector2(1,2));a[0]=Vector2(3,4);a.append(Vector2(5,6));a[0].x+=2;return a[0].y;}",
        {NOP_ARRAY_V2_NEW,NOP_ARRAY_V2_GET,NOP_ARRAY_V2_SET,NOP_ARRAY_V2_APPEND,NOP_V2_GET,NOP_V2_SET}, debug, "vector2 array primitive shape");
    CheckShape(runtime, "export float Probe(int n){array<Vector3> a=array<Vector3>(n,Vector3(1,2,3));a[0]=Vector3(3,4,5);a.append(Vector3(5,6,7));return a[0].z;}",
        {NOP_ARRAY_V3_NEW,NOP_ARRAY_V3_GET,NOP_ARRAY_V3_SET,NOP_ARRAY_V3_APPEND}, debug, "vector3 array primitive shape");
    CheckShape(runtime, "export float Probe(int n){array<Vector4> a=array<Vector4>(n,Vector4(1,2,3,4));a[0]=Vector4(3,4,5,6);a.append(Vector4(5,6,7,8));return a[0].w;}",
        {NOP_ARRAY_V4_NEW,NOP_ARRAY_V4_GET,NOP_ARRAY_V4_SET,NOP_ARRAY_V4_APPEND}, debug, "vector4 array primitive shape");
}

void Values(IRuntime& runtime, bool debug)
{
    Compare(runtime, "export int Probe(){array<int> a=array<int>(1,31);a[0]-=1;a[0]*=3;a[0]/=4;a[0]%=5;a[0]&=7;a[0]|=8;a[0]^=3;a[0]<<=2;a[0]>>=1;int pre=++a[0];int post=a[0]++;int dec=--a[0];int old=a[0]--;return pre*10000+post*1000+dec*100+old*10+a[0];}",
        Int(211108), debug, "all integer array compound and increment modes");
    Compare(runtime, "export float Probe(){array<float> a=array<float>(1,2.5);a[0]+=2;a[0]-=1;a[0]*=4;a[0]/=2;float pre=++a[0];float post=a[0]++;float dec=--a[0];float old=a[0]--;return pre+post+dec+old+a[0];}",
        Float(39), debug, "all floating array compound and increment modes");
    Compare(runtime, "export int Probe(){array<int> a=array<int>(1,9223372036854775807);a[0]+=1;return a[0];}",
        Int((std::numeric_limits<std::int64_t>::min)()), debug, "array compound integer addition wraps");
    Compare(runtime, "export int Probe(){array<int> a=array<int>(1,-7);a[0]%=3;return a[0];}",
        Int(-1), debug, "array remainder keeps dividend sign");
    Compare(runtime, "export int Probe(){array<int> a=array<int>(1,-8);a[0]>>=2;return a[0];}",
        Int(-2), debug, "array signed right shift");
    Compare(runtime, "export int Probe(){array<int> a=array<int>(1,1);a[0]<<=63;return a[0];}",
        Int((std::numeric_limits<std::int64_t>::min)()), debug, "array left shift boundary uses 64 bits");
    Compare(runtime, "export float Probe(){array<float> a=array<float>(1,1.0);a[0]/=0.0;return a[0];}",
        Float((std::numeric_limits<double>::infinity)()), debug, "floating array compound zero division");
    Compare(runtime, "export int Probe(){int a=-7;int b=3;return (a%b)+((a&b)|(a^b));}",
        Int(-6), debug, "typed scalar remainder and bitwise operations");
    Compare(runtime, "export int Probe(){int a=1;int n=63;return (a<<n)>>n;}",
        Int(-1), debug, "typed scalar shifts preserve signed 64 bit semantics");
    Compare(runtime, "export float Probe(){Vector3 v=Vector3(1,2,3);v+=Vector3(3,4,5);v-=Vector3(1,2,3);v*=4;v/=2;return v.x*100+v.y*10+v.z;}", Float(690), debug, "vector compound operations");
    Compare(runtime, "export float Probe(){Vector2 v=-Vector2(0.0,1);return v.x;}", Float(-0.0), debug, "vector negative zero");
    Compare(runtime, "export float Probe(){Vector3 v=Vector3(3,0,0)*1.00000004470348358154296875;return v.x;}", Float(3), debug, "scalar narrows before vector arithmetic");
    Compare(runtime, "export float Probe(){Vector3 v=Vector3(1,0,-1)/0.0;return v.x;}", Float((std::numeric_limits<double>::infinity)()), debug, "vector infinity");
    Compare(runtime, "export float Probe(){Vector3 v=Vector3(1,0,-1)/0.0;return v.y;}", Float((std::numeric_limits<double>::quiet_NaN)()), debug, "vector NaN");
    Compare(runtime, "export float Probe(){Quaternion q;q.x=2;q.w+=3;return q.w*10+q.x;}", Float(42), debug, "quaternion wxyz");
    Compare(runtime, "export float Probe(){Vector4 a=Vector4(1,2,3,4);Vector4 b=(a+=Vector4(1,1,1,1));b.w=9;return a.w*10+b.w;}", Float(59), debug, "vector compound result owns value");
    Compare(runtime, "export float Probe(){array<float> a=array<float>(1,-0.0);return a[0];}", Float(-0.0), debug, "array signed zero");
    Compare(runtime, "export int Probe(){array<int> a=array<int>(2,4294967297);a[1]=9223372036854775807;return a[0];}", Int(4294967297LL), debug, "integer array width");
    Compare(runtime, "export float Probe(){array<float> a=array<float>(1);a[0]=9007199254740993;return a[0];}", Float(9007199254740992.0), debug, "float array promotion");
    Compare(runtime, "export bool Probe(){array<bool> a=array<bool>(66,true);a[0]=false;a[64]=false;a.append(true);return !a[0]&&a[63]&&!a[64]&&a[65]&&a[66];}", Bool(true), debug, "packed bool word boundary");
    Compare(runtime, "export int Probe(){array<int> a=array<int>(3,7);a[0]=9;a.resize(1);a.reserve(12);a.resize(3);a.append(11);return a.len()*1000+a[0]*100+a[1]*10+a[2]+a[3];}", Int(4988), debug, "growth retains original fill");
    Compare(runtime, "export float Probe(){array<Vector4> a=array<Vector4>(2,Vector4(1,2,3,4));array<Vector4> b=a;b[0].w=9;Vector4 copy=a[1];copy.w=8;a.resize(1);a.resize(2);return a[0].w*100+a[1].w*10+copy.w;}", Float(948), debug, "array aliases and Vec4 COW fill");
    Compare(runtime, "export float Probe(){array<Vector3> a=array<Vector3>(1,Vector3(16777216,0,0));float pre=++a[0].x;float post=a[0].x++;return pre+post-a[0].x;}", Float(16777216), debug, "component increment stored precision");
    Compare(runtime, "export float Probe(){array<Vector4> a=array<Vector4>(1);return (a[0].w=16777217)-a[0].w;}", Float(0), debug, "component assignment returns narrowed value");
    Compare(runtime, "export float Probe(){array<Quaternion> a=array<Quaternion>();a.append(Quaternion(1,2,3,4));a.resize(2);return a[0].x*10+a[1].w;}", Float(21), debug, "quaternion array default identity");
    Compare(runtime, "array<int> Replace(array<int> a){a=array<int>(2,7);return a;}export int Probe(){array<int> a=array<int>(1,3);array<int> old=a;a=Replace(a);return a.len()*100+a[1]*10+old[0];}", Int(273), debug, "explicit array replacement return");
}

void Sets(IRuntime& runtime, bool debug)
{
    Compare(runtime, "import system;export int Probe(){set<int> s=set<int>();if(!s.add(7)||s.add(7)||!s.contains(7)||s.contains(8))return 0;if(!s.remove(7)||s.remove(7)||s.contains(7))return 0;return s.len()+1;}",
        Int(1), debug, "integer set add contains remove return exact booleans");
    Compare(runtime, "import system;export int Probe(){set<string> s=set<string>();string key=\"a\"..tostring(7);if(!s.add(key)||s.add(\"a7\")||!s.contains(\"a7\"))return 0;if(!s.remove(key)||s.remove(key))return 0;return s.len()+1;}",
        Int(1), debug, "string set dynamic keys use canonical equality");
    Compare(runtime, "import system;export int Probe(){set<int> s=set<int>();int lo=-9223372036854775808;int hi=9223372036854775807;if(!s.add(lo)||!s.add(hi)||!s.contains(lo)||!s.remove(hi))return 0;return s.len();}",
        Int(1), debug, "integer set full width keys");
    Compare(runtime, "import system;export int Probe(){set<string> s=set<string>();s.add(\"\");string key=null;if(s.contains(key)||s.remove(key)||!s.contains(\"\"))return 0;return s.len();}",
        Int(1), debug, "null set query differs from an empty string");
    Compare(runtime, "import system;export int Probe(){set<int> s=set<int>();set<int> alias=s;s.add(2);if(!alias.contains(2))return 0;alias.remove(2);return s.len()+1;}",
        Int(1), debug, "set mutation remains shared through aliases");
    Compare(runtime, "import system;set<int> s=set<int>();int Replace(){s=set<int>();return 7;}export int Probe(){set<int> old=s;bool added=s.add(Replace());if(added&&old.contains(7)&&!s.contains(7))return 1;return 0;}",
        Int(1), debug, "set add snapshots receiver before argument rebinding");
    Compare(runtime, "import system;set<int> s=set<int>([7]);int Replace(){s=set<int>();return 7;}export int Probe(){if(s.contains(Replace())&&!s.contains(7))return 1;return 0;}",
        Int(1), debug, "set contains snapshots receiver before argument rebinding");
    Compare(runtime, "import system;set<int> s=set<int>([7]);int Replace(){s=set<int>([7]);return 7;}export int Probe(){set<int> old=s;if(s.remove(Replace())&&old.len()==0&&s.contains(7))return 1;return 0;}",
        Int(1), debug, "set remove snapshots receiver before argument rebinding");
    Compare(runtime, "import system;int order=0;set<int> s=set<int>();set<int> Owner(){order=order*10+1;return s;}int Key(){order=order*10+2;return 3;}export int Probe(){Owner().add(Key());Owner().contains(Key());Owner().remove(Key());return order;}",
        Int(121212), debug, "each set receiver and argument evaluates once in order");
    Compare(runtime, "import system;set<string> Make(){return set<string>([\"x\"]);}export int Probe(){if(Make().contains(\"x\")&&Make().remove(\"x\")&&Make().add(\"y\"))return 1;return 0;}",
        Int(1), debug, "temporary set receivers survive until the result is stored");
    Compare(runtime, "import system;export int Probe(){map<int,set<string>> m={1:set<string>()};m[1].add(\"x\");if(m[1].contains(\"x\")&&m[1].remove(\"x\"))return 1;return 0;}",
        Int(1), debug, "set operations on stored receivers preserve ownership");
    Compare(runtime, "import system;export int Probe(){set<string> s=set<string>();for(int i in 0,128){string key=\"item\"..tostring(i);s.add(key);}for(int i in 0,128){string key=\"item\"..tostring(i);if(!s.contains(key)||!s.remove(key))return -1;}return s.len();}",
        Int(0), debug, "set growth and deletion preserve dynamic string keys");
    for (const char* statement : {
        "set<int> s=null;s.contains(1);", "set<int> s=null;s.add(1);", "set<int> s=null;s.remove(1);",
        "set<string> s=null;s.contains(\"x\");", "set<string> s=null;s.add(\"x\");", "set<string> s=null;s.remove(\"x\");",
        "set<string> s=set<string>();s.add(null);"
    })
    {
        const std::string source=std::string("import system;export int Probe(){\n")+statement+"\nreturn 0;}";
        Compare(runtime,source.c_str(),Outcome(),debug,statement,2);
    }
}

void ListWrites(IRuntime& runtime, bool debug)
{
    Compare(runtime, "export int Probe(){list<int> a=[9];int p=a[0]++;int q=++a[0];a[0]+=3;a[0]-=2;a[0]*=4;a[0]/=3;a[0]%=7;a[0]|=8;a[0]&=14;a[0]^=3;a[0]<<=2;a[0]>>=1;return p*1000+q*100+a[0];}",
        Int(10118), debug, "list integer compound and increment use typed arithmetic");
    Compare(runtime, "export float Probe(){list<float> a=[2.0];float p=a[0]++;float q=--a[0];a[0]+=3;a[0]-=1;a[0]*=2;a[0]/=4;return p*100+q*10+a[0];}",
        Float(222), debug, "list float compound widens integer operands");
    Compare(runtime, "int order=0;list<int> a=[2];int Rhs(){order=order*10+1;return 3;}list<int> Owner(){order=order*10+2;return a;}int Key(){order=order*10+3;return 0;}export int Probe(){Owner()[Key()]+=Rhs();return order*10+a[0];}",
        Int(1235), debug, "list compound evaluates RHS then owner and key once");
    Compare(runtime, "list<int> a=[2];int Rhs(){a=[9];return 3;}export int Probe(){list<int> old=a;a[0]+=Rhs();return old[0]*100+a[0];}",
        Int(212), debug, "list compound resolves rebound target after RHS");
    Compare(runtime, "list<int> a=[2];int Key(){a=[9];return 0;}export int Probe(){list<int> old=a;a[Key()]+=3;return old[0]*10+a[0];}",
        Int(59), debug, "list owner is snapshotted before key side effects");
    Compare(runtime, "list<int> a=[29];\nexport int Probe(){\na[0]/=0;\nreturn 0;}",
        Outcome(), debug, "list division failure retains element", 3, "a[0]");
    Compare(runtime, "list<int> a=[29];\nexport int Probe(){\na[0]<<=64;\nreturn 0;}",
        Outcome(), debug, "list shift failure retains element", 3, "a[0]");
    Compare(runtime, "list<int> a=[29];\nexport int Probe(){\na[1]+=1;\nreturn 0;}",
        Outcome(), debug, "list compound missing index reports original source", 3, "a[0]");
    for (const char* source : {
        "export void Probe(){map<string,int> a={};a[\"missing\"]+=1;}",
        "export void Probe(){map<string,int> a={};a[\"x\"]=2;a[\"x\"]++;}",
        "export void Probe(){map<string,float> a={};--a[\"missing\"]; }"})
    {
        NeoCore::StaticCompileResult checked;
        Check(!NeoCore::CompileStaticSource(source, checked) && !checked.errorMessage.empty(),
            "map element compound arithmetic is rejected explicitly");
    }
}

void MapCompounds(IRuntime& runtime, bool debug)
{
    const char* operators[] = {"+=", "-=", "*=", "/="};
    const int answers[] = {16, 8, 48, 3};
    for (int kind = 0; kind < 4; ++kind)
    {
        const bool stringKey = kind >= 2, floating = (kind & 1) != 0;
        const std::string type = floating ? "float" : "int";
        const std::string key = stringKey ? "\"value\"" : "7";
        const std::string mapType = "map<" + std::string(stringKey ? "string" : "int") + "," + type + ">";
        for (int operation = 0; operation < 4; ++operation)
        {
            const std::string update = "x" + std::string(operators[operation]) + "m[" + key + "];";
            const std::string shape = "export " + type + " Probe(" + mapType + " m){" + type + " x=12;" + update + "return x;}";
            const OperationCode read = stringKey ? (floating ? NOP_MAP_STR_F64_LOAD : NOP_MAP_STR_I64_LOAD)
                : (floating ? NOP_MAP_INT_F64_LOAD : NOP_MAP_INT_I64_LOAD);
            const OperationCode arithmetic = static_cast<OperationCode>((floating ? OP_F64_ADD : OP_I64_ADD) + (operation << 8));
            CheckShape(runtime, shape.c_str(), {read, arithmetic}, debug, "map compound primitive shape");
            const std::string source = "export " + type + " Probe(){" + mapType + " m={" + key + ":4};" + type + " x=12;" + update + "return x;}";
            Compare(runtime, source.c_str(), floating ? Float(answers[operation]) : Int(answers[operation]), debug, "typed map compound result");
        }
    }

    Compare(runtime, "int x=1;map<string,int> m={\"a\":2};string Key(){x=10;return \"a\";}export int Probe(){x+=m[Key()];return x;}", Int(12), debug, "target read follows key effects");
    Compare(runtime, "const string key=\"\\u0061\\n\";map<string,int> m={\"a\\n\":2};export int Probe(){int x=1;x+=m[key];x+=m[\"\\u0061\\n\"];return x;}", Int(5), debug, "decoded literal and constant keys agree");
    Compare(runtime, "int x=7;map<int,int> m={7:4};export int Probe(){x+=m[x];int y=x;return x*100+y;}", Int(1111), debug, "key aliases destination and later copy keeps destination");
    Compare(runtime, "int order=0;map<int,int> m={0:4};map<int,int> Owner(){order=order*10+1;return m;}int Key(){order=order*10+2;m={0:9};return 0;}export int Probe(){int x=3;x+=Owner()[Key()];return order*100+x;}", Int(1207), debug, "receiver captured before key replaces map");
    Compare(runtime, "map<string,int> m={\"a\":4};export int Probe(){int x=12;return (x+=m[\"a\"])+(x=1);}", Int(17), debug, "consumed compound result survives later write");
    Compare(runtime, "map<int,int> m={0:1};export int Probe(){int x=9223372036854775807;x+=m[0];return x;}", Int((std::numeric_limits<std::int64_t>::min)()), debug, "fused addition wraps int64");
    Compare(runtime, "map<string,float> m={\"a\":0.0};export float Probe(){float x=-0.0;x*=m[\"a\"];return x;}", Float(-0.0), debug, "fused float preserves signed zero");
    Compare(runtime, "map<int,float> m={0:0.0};export float Probe(){float x=1.0;x/=m[0];return x;}", Float((std::numeric_limits<double>::infinity)()), debug, "fused float division preserves infinity");

    Compare(runtime, "int x=29;map<string,int> m;\nexport int Probe(){\nx+=m[\"a\"];return x;}", Outcome(), debug, "null map preserves destination", 3, "x");
    Compare(runtime, "int x=29;map<string,int> m={};\nexport int Probe(){\nx+=m[\"missing\"];return x;}", Outcome(), debug, "missing map key preserves destination", 3, "x");
    Compare(runtime, "int x=29;map<string,int> m={};\nexport int Probe(){string key;\nx-=m[key];return x;}", Outcome(), debug, "null string key fails before write", 3, "x");
    Compare(runtime, "int x=29;map<int,int> m={0:0};\nexport int Probe(){\nx/=m[0];return x;}", Outcome(), debug, "map division by zero preserves destination", 3, "x");
    Compare(runtime, "int x=-9223372036854775808;map<int,int> m={0:-1};\nexport int Probe(){\nx/=m[0];return x;}", Outcome(), debug, "map integer division overflow preserves destination", 3, "(x+9223372036854775807)+30");

    // 숫자 경로 이외에도 원본 맵의 마지막 소유권·벡터 COW·임시 슬롯 재사용을 검사한다.
    Compare(runtime, "map<string,map<string,int>> m={\"a\":{\"v\":7}};export int Probe(){map<string,int> child=m[\"a\"];m=null;return child[\"v\"];}", Int(7), debug, "map read keeps nested ownership");
    Compare(runtime, "map<int,Vector4> m={0:Vector4(1,2,3,4)};export float Probe(){Vector4 v=m[0];v.w=9;return v.w*10+m[0].w;}", Float(94), debug, "reference map read preserves vector value semantics");
}

void Order(IRuntime& runtime, bool debug)
{
    Compare(runtime, "int order=0;int Size(){order=order*10+1;return 2;}int Fill(){order=order*10+2;return 7;}export int Probe(){array<int> a=array<int>(Size(),Fill());return order*10+a.len();}", Int(122), debug, "constructor argument order");
    Compare(runtime, "int order=0;array<Vector3> data=array<Vector3>(1);Vector3 Rhs(){order=order*10+1;return Vector3(1,2,3);}array<Vector3> Target(){order=order*10+2;return data;}int Index(){order=order*10+3;return 0;}export int Probe(){Target()[Index()]+=Rhs();return order;}", Int(123), debug, "whole vector RHS target index once");
    Compare(runtime, "int order=0;array<Vector3> data=array<Vector3>(1);float Rhs(){order=order*10+1;return 3;}array<Vector3> Target(){order=order*10+2;return data;}int Index(){order=order*10+3;return 0;}export int Probe(){Target()[Index()].x+=Rhs();return order;}", Int(123), debug, "component RHS target index once");
    Compare(runtime, "array<Vector3> data=array<Vector3>(1,Vector3(1,2,3));int Index(){data=array<Vector3>(1,Vector3(9,8,7));return 0;}export float Probe(){array<Vector3> old=data;data[Index()].x=5;return old[0].x*10+data[0].x;}", Float(59), debug, "index call does not replace captured owner");
    Compare(runtime, "array<int> data=array<int>(1,2);int Change(){data=array<int>(1,9);return 7;}export int Probe(){array<int> old=data;data.append(Change());return old.len()*100+old[1]*10+data[0];}", Int(279), debug, "append captures receiver before argument");
    Compare(runtime, "array<Vector4> data=array<Vector4>(1,Vector4(1,2,3,4));Vector4 Change(){data[0].w=9;return Vector4(0,0,0,2);}export float Probe(){Vector4 v=data[0]+Change();return v.w*10+data[0].w;}", Float(69), debug, "array read snapshot before RHS mutation");
    Compare(runtime, "array<Vector4> data=array<Vector4>(1,Vector4(1,2,3,4));float Rhs(){data.resize(0);data.resize(1);data[0].w=7;return 2;}export float Probe(){data[0].w+=Rhs();return data[0].w;}", Float(9), debug, "component target after RHS resize");
    Compare(runtime, "Vector3 g=Vector3(1,2,3);int Scalar(){g.x=9;return 2;}export float Probe(){Vector3 value=g*Scalar();return value.x;}", Float(2), debug, "vector first snapshot");
    Compare(runtime, "Vector3 g=Vector3(1,2,3);int Scalar(){g.x=9;return 2;}export float Probe(){Vector3 value=Scalar()*g;return value.x;}", Float(18), debug, "scalar first evaluation");
}

void Errors(IRuntime& runtime, bool debug)
{
    const char* failureStatements[] = {
        "array<int> a=array<int>(-1);", "array<float> a=array<float>(4294967296);",
        "array<int> a=array<int>(2147483648);", "array<int> a=array<int>();a.reserve(-1);",
        "array<int> a=array<int>();a.reserve(4294967296);", "array<bool> a=array<bool>();a.resize(4294967296);",
        "array<int> a=array<int>(1);int n=a[-1];", "array<float> a=array<float>(1);a[1]=3;",
        "array<Vector3> a=array<Vector3>(1);float n=a[4294967296].x;",
        "array<Vector4> a=array<Vector4>(1);a[2147483648].w+=1;",
        "array<int> a=array<int>(1,7);a[0]/=0;", "array<int> a=array<int>(1,7);a[0]%=0;",
        "array<int> a=array<int>(1,-9223372036854775808);a[0]/=-1;",
        "array<int> a=array<int>(1,-9223372036854775808);a[0]%=-1;",
        "array<int> a=array<int>(1,1);a[0]<<=-1;", "array<int> a=array<int>(1,1);a[0]<<=64;",
        "array<int> a=array<int>(1,-1);a[0]>>=-1;", "array<int> a=array<int>(1,-1);a[0]>>=64;",
        "int value=1;int count=-1;int result=value<<count;", "int value=1;int count=64;int result=value>>count;",
        "int value=7;int zero=0;int result=value%zero;",
        "int value=-9223372036854775808;int minus=-1;int result=value%minus;"
    };
    for (const char* statement : failureStatements)
    {
        const std::string source = std::string("export int Probe(){\n") + statement + "\nreturn 0;}";
        Compare(runtime, source.c_str(), Outcome(), debug, statement, 2);
    }
    Compare(runtime, "array<int> data=array<int>(1,29);\nexport int Probe(){\ndata[1]=7;\nreturn 0;}",
        Outcome(), debug, "failed write retains existing array", 3, "data[0]");
    Compare(runtime, "array<int> data=array<int>(1,29);\nexport int Probe(){\ndata.resize(4294967296);\nreturn 0;}",
        Outcome(), debug, "failed resize retains existing array", 3, "data[0]");
    Compare(runtime, "array<int> data=array<int>(1,29);\nexport int Probe(){\ndata[0]/=0;\nreturn 0;}",
        Outcome(), debug, "failed divide compound retains element", 3, "data[0]");
    Compare(runtime, "array<int> data=array<int>(1,29);\nexport int Probe(){\ndata[0]<<=64;\nreturn 0;}",
        Outcome(), debug, "failed shift compound retains element", 3, "data[0]");
}

void Cleanup(IRuntime& runtime, bool debug)
{
    // 슬롯 재사용·원소 교체·COW 복사 뒤 프로그램을 해제하면 살아 있는 저장소 수가 원래대로 돌아와야 한다.
    const char* source =
        "export float Probe(){array<Vector4> a=array<Vector4>(2,Vector4(1,2,3,4));"
        "array<Vector4> witness=a;Vector4 v=Vector4(5,6,7,8);"
        "for(int i in 0,64){Vector4 copy=a[0];a[0]=v;v=copy+Vector4(1,1,1,1);"
        "a.append(v);a.resize(2);a[1].w+=1;v.w=5;}"
        "return witness[1].w;}\nexport int Recovery(){return 29;}";
    {
        AllocStats before; GetAllocStats(before);
        const Outcome result = Run(runtime, source, debug, true, "aggregate reference cleanup");
        AllocStats after; GetAllocStats(after);
        Check(SameValue(result, Float(68)), "repeated vector array updates preserve witness");
        Check(before.lists == after.lists && before.arrays == after.arrays && before.vectors == after.vectors &&
            before.bufferBytes == after.bufferBytes, "vector and array storage released after repeated overwrites");
    }
}
}

int main()
{
#if defined(_WIN32) && defined(_DEBUG)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    IRuntime* runtime = CreateRuntime();
    Check(runtime != nullptr, "runtime creation");
    if (!runtime) return 1;
    runtime->FreezeBindings();
    for (bool debug : {false, true})
    {
        Shapes(*runtime, debug);
        Values(*runtime, debug);
        Order(*runtime, debug);
        ListWrites(*runtime, debug);
        Sets(*runtime, debug);
        MapCompounds(*runtime, debug);
        Errors(*runtime, debug);
        Cleanup(*runtime, debug);
    }
    DestroyRuntime(runtime);
    std::printf("NeoCore typed collection ops: %d/%d passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
