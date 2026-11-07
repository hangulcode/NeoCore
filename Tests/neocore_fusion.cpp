#include "NeoCoreCompiler.h"
#include "NeoScript.h"
#include "NeoVMProgram.h"
#include "NeoVMFusion.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using namespace NeoScript;
namespace {
int checks=0, failures=0, fusedCases=0, updateCases=0;
void Check(bool ok,const std::string& label) { ++checks; if(!ok && ++failures<40) std::fprintf(stderr,"FAIL: %s\n",label.c_str()); }
struct Value {
    RunStatus status=RunStatus::Failed;
    ValueType type=ValueType::None;
    std::int64_t integer=0; double number=0; bool boolean=false;
    std::string error;
};
Value Invoke(IRuntime& runtime,InstanceHandle instance,const char* name) {
    Value value; auto call=runtime.Call(instance,name); value.status=call.invoke();
    if(value.status!=RunStatus::Completed) { value.error=call.error().message; return value; }
    value.type=call.retType();
    if(value.type==ValueType::Int) value.integer=call.retInt();
    if(value.type==ValueType::Float) value.number=call.retFloat();
    if(value.type==ValueType::Bool) value.boolean=call.retBool();
    return value;
}
bool Same(const Value& a,const Value& b) {
    return a.status==b.status && a.type==b.type && a.integer==b.integer && a.boolean==b.boolean &&
        std::memcmp(&a.number,&b.number,sizeof(double))==0 && a.error==b.error;
}
void Reject(IRuntime& runtime,const std::vector<uint8_t>& bytes,const char* label) {
    Error error; auto program=runtime.LoadProgram(bytes,&error);
    Check(!program && !error.ok(),label); if(program) runtime.DestroyProgram(program);
}
// 같은 원시 명령을 복원하여 프런트엔드와 실행 융합의 차이를 분리한다.
std::vector<uint8_t> Unfused(const std::vector<uint8_t>& image,const CNeoVMProgram& program) {
    std::vector<uint8_t> result=image;
    if(program.fusions.empty()) return result;
    std::size_t chunk=4+(program.fusions.size()<65535?2:6);
    for(const auto& plan:program.fusions) {
        chunk+=7+sizeof(SVMOperation)*plan.count;
        std::memcpy(result.data()+sizeof(SNeoVMHeader)+plan.start*sizeof(SVMOperation),plan.operations,sizeof(SVMOperation)*plan.count);
    }
    result.resize(result.size()-chunk); return result;
}
// 새 갱신 형식을 기존 세 피연산자로 되돌려, 서로 다른 실행 경로의 값과 오류를 비교한다.
std::vector<uint8_t> ExpandedUpdates(const std::vector<uint8_t>& image) {
    std::vector<uint8_t> result=image;
    SNeoVMHeader header={};std::memcpy(&header,result.data(),sizeof(header));
    for(int offset=0;offset<header._iCodeSize;offset+=sizeof(SVMOperation)) {
        SVMOperation op={};std::memcpy(&op,result.data()+sizeof(header)+offset,sizeof(op));
        if(!IsArithmeticUpdate(op))continue;
        op.n3=op.n2;op.n2=op.n1;
        op.argFlag=static_cast<u8>((op.argFlag&(NEOS_ARG_N1_LOCAL|NEOS_OP_SUB_MASK))|
            ((op.argFlag&NEOS_ARG_N1_LOCAL)>>1)|((op.argFlag&NEOS_ARG_N2_LOCAL)>>1));
        std::memcpy(result.data()+sizeof(header)+offset,&op,sizeof(op));
    }
    return result;
}
void Compare(IRuntime& runtime,const std::string& source,const std::string& label,bool expectFailure=false) {
    NeoCore::StaticCompileResult checked;
    Check(NeoCore::CompileStaticSource(source+"\nexport int Recovery(){int x=3;return (x+4)*5;}",checked),label+" frontend: "+checked.errorMessage);
    if(!checked.success) return;
    CompileDesc desc; desc.source=checked.loweredSource; desc.coreTypedIR=&checked.typedIR;
    desc.coreIntrinsicPrefix=checked.backendIntrinsicPrefix; desc.sourceName="fusion.nc";
    std::vector<uint8_t> image; Error error=runtime.CompileToBytecode(desc,image);
    Check(error.ok(),label+" bytecode: "+error.message); if(!error.ok()) return;
    std::string failure; CNeoVMProgram* code=CNeoVMProgram::Create(image.data(),static_cast<int>(image.size()),&failure);
    Check(code!=nullptr,label+" verified image: "+failure); if(!code) return;
    if(!code->fusions.empty()) ++fusedCases;
    if(label=="nested for temporaries") {
        bool forwarded=false;
        for(const auto& plan:code->fusions)
            if(plan.count==2 && ArithmeticMeaning(plan.operations[0].Code())==OP_I64_ADD &&
                ArithmeticMeaning(plan.operations[1].Code())==OP_I64_ADD && (plan.elide&1)) forwarded=true;
        Check(forwarded,"nested loop forwards its arithmetic temporary across unrelated bound normalization");
    }
    if(label=="owned first destination" || label=="owned second destination") {
        bool injected=false;
        if(!code->fusions.empty()) {
            const auto& plan=code->fusions.front();
            const short target=plan.operations[label=="owned first destination"?0:1].n1;
            for(int i=0;i<plan.start;++i) {
                SVMOperation op={};
                std::memcpy(&op,image.data()+sizeof(SNeoVMHeader)+i*sizeof(op),sizeof(op));
                if((op.Code()==NOP_MOV || (op.op==NOP_SLOT_COPY &&
                    (op.argFlag&NEOS_OP_SUB_MASK)==static_cast<u8>(SlotKind::String))) &&
                    FusionLocal(op,0) && !FusionLocal(op,1)) {
                    op.SetCode(NOP_MOV);
                    op.n1=target;
                    std::memcpy(image.data()+sizeof(SNeoVMHeader)+i*sizeof(op),&op,sizeof(op));
                    injected=true;break;
                }
            }
        }
        Check(injected,label+" initially owns a string before arithmetic reuses it");
        Reject(runtime,image,"string write into a fixed numeric destination rejected");
        code->Release();
        return;
    }
    std::vector<uint8_t> original=Unfused(image,*code);
    const std::vector<uint8_t> expanded=ExpandedUpdates(original);
    if(original!=expanded)++updateCases;
    if(!code->fusions.empty()) {
        auto invalid=image;
        invalid[sizeof(SNeoVMHeader)+code->fusions.front().start*sizeof(SVMOperation)]=NOP_EXTEND;
        Reject(runtime,invalid,"orphan extension rejected");
        invalid=image; invalid.pop_back(); Reject(runtime,invalid,"truncated fusion rejected");
        std::size_t chunk=4+(code->fusions.size()<65535?2:6);
        for(const auto& plan:code->fusions) chunk+=7+sizeof(SVMOperation)*plan.count;
        invalid=image; invalid[image.size()-chunk+6+5]^=1;
        Reject(runtime,invalid,"altered liveness rejected");
        invalid=image; invalid[image.size()-chunk+6+6]^=1;
        Reject(runtime,invalid,"altered arithmetic update proof rejected");
    }
    code->Release();
    ProgramHandle fused=runtime.LoadProgram(image,&error), plain=runtime.LoadProgram(original,&error), classic=runtime.LoadProgram(expanded,&error);
    Check(bool(fused)&&bool(plain)&&bool(classic),label+" differential images load");
    if(fused&&plain&&classic) {
        auto a=runtime.CreateInstance(fused),b=runtime.CreateInstance(plain),c=runtime.CreateInstance(classic);
        for(int run=0;run<3;++run) {
            Value left=Invoke(runtime,a,"Probe"),right=Invoke(runtime,b,"Probe");
            Check(Same(left,right),label+" fused matches original ("+left.error+" / "+right.error+")");
            Check(Same(left,Invoke(runtime,c,"Probe")),label+" matches three-operand arithmetic");
            if(label=="owned first destination" || label=="owned second destination")
                Check(left.integer==40,label+" retained its arithmetic result");
            Check(left.status==(expectFailure?RunStatus::Failed:RunStatus::Completed),label+" expected status");
            left=Invoke(runtime,a,"Recovery"); right=Invoke(runtime,b,"Recovery");
            Check(Same(left,right)&&left.integer==35,label+" error trap/recovery");
            Check(Same(left,Invoke(runtime,c,"Recovery")),label+" expanded error recovery");
        }
        runtime.DestroyInstance(a);runtime.DestroyInstance(b);runtime.DestroyInstance(c);
    }
    if(fused)runtime.DestroyProgram(fused);if(plain)runtime.DestroyProgram(plain);if(classic)runtime.DestroyProgram(classic);
    desc.includeDebugInfo=true;image.clear();error=runtime.CompileToBytecode(desc,image);
    Check(error.ok(),label+" debug compile");
    code=error.ok()?CNeoVMProgram::Create(image.data(),static_cast<int>(image.size()),&failure):nullptr;
    Check(code&&code->fusions.empty(),label+" debug retains instruction boundaries");if(code)code->Release();
    if(error.ok()) {
        auto a=runtime.LoadProgram(image,&error),b=runtime.LoadProgram(ExpandedUpdates(image),&error);
        Check(bool(a)&&bool(b),label+" debug update images load");
        if(a&&b) {
            auto ai=runtime.CreateInstance(a),bi=runtime.CreateInstance(b);
            const Value actual=Invoke(runtime,ai,"Probe"),reference=Invoke(runtime,bi,"Probe");
            Check(Same(actual,reference),label+" debug result and source error location");
            runtime.DestroyInstance(ai);runtime.DestroyInstance(bi);
        }
        if(a)runtime.DestroyProgram(a);if(b)runtime.DestroyProgram(b);
    }
}
}
void CheckNormalizationLiveness() {
    std::vector<SVMOperation> code(3);
    code[0].SetCode(NOP_CHANGE_INT); code[0].argFlag=NEOS_ARG_N1_LOCAL; code[0].n1=3;
    code[1].SetCode(NOP_MOVI); code[1].argFlag=NEOS_ARG_N1_LOCAL; code[1].n1=4;
    code[2].SetCode(NOP_RETURN); code[2].argFlag=NEOS_OP_CALL_NORESULT;
    std::vector<std::vector<int>> targets;
    Check(!FusionMayReadAfter(code,0,3,0,4,targets),"unrelated integer normalization does not keep a temporary alive");
    Check(FusionMayReadAfter(code,0,3,0,3,targets),"integer normalization reads its operand before overwriting it");
    code[0].SetCode(NOP_CALL);
    Check(FusionMayReadAfter(code,0,3,0,4,targets),"unknown call boundary retains a temporary");
}
void CheckUpdateEncoding(IRuntime& runtime) {
    for(unsigned flags=0;flags<8;++flags) {
        SVMOperation op={};op.SetCode(OP_I64_ADD);op.argFlag|=static_cast<u8>(flags<<5);
        op.n1=3;op.n2=3;op.n3=7;
        const bool eligible=FusionLocal(op,0)==FusionLocal(op,1),rightLocal=FusionLocal(op,2);
        PackArithmeticUpdate(op);
        Check(IsArithmeticUpdate(op)==eligible,"packing compares storage base as well as slot index");
        if(eligible)Check(op.n1==3&&op.n2==7&&op.n3==0&&FusionLocal(op,1)==rightLocal&&!FusionLocal(op,2),"packed source and unused slot flags");
    }
    NeoCore::StaticCompileResult checked;
    Check(NeoCore::CompileStaticSource("export int Probe(){int x=7;int y=2;x+=y;return x;}",checked),"packing frontend");
    CompileDesc desc;desc.source=checked.loweredSource;desc.coreTypedIR=&checked.typedIR;
    desc.coreIntrinsicPrefix=checked.backendIntrinsicPrefix;desc.includeDebugInfo=true;desc.sourceName="update.nc";
    std::vector<uint8_t> image;Error error=runtime.CompileToBytecode(desc,image);
    Check(error.ok(),"packing bytecode export");if(!error.ok())return;
    SNeoVMHeader header={};std::memcpy(&header,image.data(),sizeof(header));
    bool found=false;
    for(int offset=0;offset<header._iCodeSize;offset+=sizeof(SVMOperation)) {
        SVMOperation op={};const std::size_t at=sizeof(header)+offset;
        std::memcpy(&op,image.data()+at,sizeof(op));if(!IsArithmeticUpdate(op))continue;
        found=true;Check(ArithmeticMeaning(op.Code())==OP_I64_ADD&&op.n3==0,"compiler emits update before any program load");
        auto invalid=image;SVMOperation bad=op;bad.n3=1;std::memcpy(invalid.data()+at,&bad,sizeof(bad));
        Reject(runtime,invalid,"update rejects unused operand payload");
        invalid=image;bad=op;bad.argFlag|=NEOS_ARG_N3_LOCAL;std::memcpy(invalid.data()+at,&bad,sizeof(bad));
        Reject(runtime,invalid,"update rejects unused operand flag");
        invalid=image;bad=op;bad.SetCode(OP_I64_NEG);std::memcpy(invalid.data()+at,&bad,sizeof(bad));
        Reject(runtime,invalid,"unary operation rejects update modifier");
        invalid=image;bad=op;bad.argFlag&=~NEOS_ARG_N1_LOCAL;bad.n1=0;std::memcpy(invalid.data()+at,&bad,sizeof(bad));
        Reject(runtime,invalid,"update rejects constant destination");
    }
    Check(found,"update is serialized by compiler rather than selected by loader");
}
int main() {
    CheckNormalizationLiveness();
    IRuntime* runtime=CreateRuntime();if(!runtime)return 1;runtime->FreezeBindings();
    CheckUpdateEncoding(*runtime);
    Compare(*runtime,"int LoopSum(int outer,int inner){int last=0;for(int o in 0,outer,1){int bias=last&1;int sum=0;for(int i in 0,inner,1)sum+=i+bias;last=sum;}return last;}export int Probe(){return LoopSum(4,6);}","nested for temporaries");
    for(const char* label:{"owned first destination","owned second destination"})
        Compare(*runtime,"export int Probe(){string held=\"owned value\";int a=17;int b=3;int c=2;return (a+b)*c;}",label);
    const char* cases[]={
        "export int Probe(){int a=17;int b=3;int c=2;return c-(a+b);}",
        "export float Probe(){float a=1.5;float b=0.5;float c=2.5;return c/(a+b);}",
        "export int Probe(){int a=17;int b=3;a+=b;return a+a;}",
        "export int Probe(){int a=17;int b=3;int c=2;return (a=b+c)+a;}",
        "export float Probe(){float a=1.5;float b=0.5;a+=b;return a*a;}",
        "export int Probe(){int a=9223372036854775807;int b=1;return (a+b)-b;}",
        "export int Probe(){int a=17;int b=3;return ((a+b)*b-(a>>b))^((a&7)+(b<<2));}",
        "export float Probe(){float a=0.1;float b=0.2;return ((a+b)*1.5-b)/a;}",
        "export float Probe(){float a=-0.0;float b=2.0;return a/b;}",
        "export bool Probe(){float a=0.0;float b=a/a;return !(b+1.0<3.0);}",
        "export int Probe(){int sum=0;for(int i in 0,60){if((i*3+1)<40)sum+=i;else sum-=i;}return sum;}",
        "export int Probe(){int a=3;int b=(a+1)*2;switch(b){case 8:return a+b;default:return a-b;}}",
        "int g=1;int F(){g+=2;return g;}export int Probe(){int a=g;return (a+g)*F()+g;}",
        "export int Probe(){list<int> a=[2,3];list<int> b=a;a[0]=(a[1]+4)*3;b[1]+=a[0];return a[0]+a[1];}",
        "export float Probe(){list<float> a=[1.5,2.5];a[0]=(a[1]+1.0)*2.0;return a[0]+a[1];}",
        "export bool Probe(){list<bool> a=[true,false];a[0]=!a[1];return !a[0];}",
        "export int Probe(){array<int> a=array<int>(3,2);a[1]=(a[0]+1)*4;a[2]+=a[1];return a[1]+a[2];}",
        "export float Probe(){array<float> a=array<float>(3,0.5);a[1]=(a[0]+1.0)*4.0;return a[1]+a[2];}",
        "export bool Probe(){array<bool> a=array<bool>(3,true);a[1]=!a[0];return !a[1];}",
        "export int Probe(){array32<int> a=array32<int>(3,2);a[1]=(a[0]+1)*4;return a[1]+a[2];}",
        "export float Probe(){array32<float> a=array32<float>(3,0.1);float x=(a[0]=0.1+0.2);return (x-a[0])+a[1];}",
        "export int Probe(){map<int,int> a={1:2,2:3};a[3]=(a[1]+a[2])*4;return a[3]+a[1];}",
        "export float Probe(){map<int,float> a={1:2.5,2:3.5};a[3]=(a[1]+a[2])*4;return a[3]+a[1];}",
        "export bool Probe(){map<int,bool> a={1:true,2:false};a[3]=!a[2];return !a[3];}",
        "export int Probe(){map<string,int> a={\"a\":2,\"b\":3};a[\"c\"]=(a[\"a\"]+a[\"b\"])*4;return a[\"c\"]+a[\"a\"];}",
        "export float Probe(){map<string,float> a={\"a\":2.5,\"b\":3.5};a[\"c\"]=(a[\"a\"]+a[\"b\"])*4;return a[\"c\"]+a[\"a\"];}",
        "export bool Probe(){map<string,bool> a={\"a\":true,\"b\":false};a[\"c\"]=!a[\"b\"];return !a[\"c\"];}",
        "struct P{int x;float y;};struct S{P p;};export float Probe(){S s=S(P(2,0.5));s.p.x=s.p.x*3+1;s.p.y=s.p.y*2+1;return float(s.p.x)+s.p.y;}",
        "int g=0;int Key(){g+=1;return g;}int Val(){g+=1;return 7;}export int Probe(){g=0;list<int> a=[0,0,0,0,0];a[Key()]=a[Key()]+Val();return a[3]*10+g;}",
        "export int Probe(){int s=0;for(int i in 0,80){list<int> a=[i];s+=(a[0]*2+1);if(s>100)s=s-33;}return s;}"
    };
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i)Compare(*runtime,cases[i],"case "+std::to_string(i));
    const char* errors[]={
        "export int Probe(){int a=3;int b=0;return (a+1)%b;}",
        "export int Probe(){int a=3;int b=-1;return (a+1)>>b;}",
        "export int Probe(){int a=-9223372036854775808;int b=-1;int c=0;return (a+c)/b;}",
        "export int Probe(){int a=3;int b=0;return (a+1)/b;}",
        "export int Probe(){int a=3;int b=64;return (a+1)<<b;}",
        "export int Probe(){list<int> a=[3];return (a[0]+1)*a[1];}",
        "export int Probe(){array<int> a=array<int>(1,3);a[2]=(a[0]+1)*2;return 1;}",
        "export int Probe(){map<string,int> a={\"a\":3};return a[\"a\"]+a[\"b\"];}",
        "export int Probe(){list<int> a=null;int x=3;return (x+1)*a[0];}",
        "export int Probe(){array32<int> a=array32<int>(1,0);int x=2147483647;a[0]=x+1;return 1;}"
    };
    for(unsigned i=0;i<sizeof(errors)/sizeof(errors[0]);++i)Compare(*runtime,errors[i],"error "+std::to_string(i),true);
    for(int seed=1;seed<=80;++seed) {
        std::string expression="x";
        for(int j=0;j<12;++j)expression="(("+expression+"+"+std::to_string(seed+j)+")*3&65535)";
        Compare(*runtime,"export int Probe(){int x="+std::to_string(seed)+";return "+expression+";}","chain "+std::to_string(seed));
    }
    const char* integerOps[]={"+","-","*","/","%","&","|","^","<<",">>"};
    for(int first=0;first<10;++first) for(int second=0;second<10;++second)
        Compare(*runtime,"export int Probe(){int a=17;int b=3;int c=2;return (a"+std::string(integerOps[first])+"b)"+integerOps[second]+"c;}",
            "integer pair "+std::to_string(first)+":"+std::to_string(second));
    const char* floatOps[]={"+","-","*","/"};
    for(int first=0;first<4;++first) for(int second=0;second<4;++second)
        Compare(*runtime,"export float Probe(){float a=1.5;float b=0.5;float c=2.5;return (a"+std::string(floatOps[first])+"b)"+floatOps[second]+"c;}",
            "float pair "+std::to_string(first)+":"+std::to_string(second));
    for(const char* op:integerOps) {
        Compare(*runtime,"export int Probe(){int a=17;int b=2;a"+std::string(op)+"=b;return a;}",std::string("integer update ")+op);
        Compare(*runtime,"int g=17;export int Probe(){int b=2;g"+std::string(op)+"=b;return g;}",std::string("global update ")+op);
        Compare(*runtime,"export int Probe(){int a=2;a"+std::string(op)+"=a;return a;}",std::string("self update ")+op);
    }
    for(const char* op:floatOps) {
        Compare(*runtime,"export float Probe(){float a=1.5;float b=0.5;a"+std::string(op)+"=b;return a;}",std::string("float update ")+op);
        Compare(*runtime,"float g=1.5;export float Probe(){float b=0.5;g"+std::string(op)+"=b;return g;}",std::string("global float update ")+op);
    }
    for(const char* source : {
        "export int Probe(){int a=9223372036854775807;a+=1;return a;}",
        "export int Probe(){int a=-9223372036854775808;a-=1;return a;}",
        "export int Probe(){int a=9223372036854775807;a*=2;return a;}",
        "export float Probe(){float a=-0.0;a/=2.0;return a;}",
        "export float Probe(){float a=0.0;a/=a;return a;}",
        "int g=0;int Change(){g=20;return 3;}export int Probe(){g=7;g=g+Change();return g;}",
        "export int Probe(){int a=7;int b=(a+=2);return a+b;}",
        "export int Probe(){int s=0;for(int i in 0,20){string old=\"held\";s+=(i+1)*2;}return s;}",
        "export int Probe(){int a=17;int b=2;return ((a+b)*b)+b;}",
        "export float Probe(){float a=1.5;float b=0.5;return ((a+b)*b)+b;}"
    }) Compare(*runtime,source,"update boundary");
    for(const char* op:{"/","%","<<",">>"}) {
        Compare(*runtime,"export int Probe(){int a=7;int b="+std::string(op[0]=='/'||op[0]=='%'?"0":"64")+";a"+op+"=b;return a;}",std::string("update error ")+op,true);
    }
    for(const char* owner : {"list<int> a=[17];", "map<int,int> a={0:17};", "array<int> a=array<int>(1,17);", "array32<int> a=array32<int>(1,17);"})
        for(const char* op:integerOps)
            Compare(*runtime,"export int Probe(){"+std::string(owner)+"int b=2;return a[0]"+op+"b;}",std::string(owner)+op);
    for(const char* owner : {"list<float> a=[1.5];", "map<int,float> a={0:1.5};", "array<float> a=array<float>(1,1.5);", "array32<float> a=array32<float>(1,1.5);"})
        for(const char* op:floatOps)
            Compare(*runtime,"export float Probe(){"+std::string(owner)+"float b=2.5;return a[0]"+op+"b;}",std::string(owner)+op);
    Check(fusedCases>=100,"general pass actually fuses the scalar/container/long-chain cases");
    Check(updateCases>=30,"differential checks actually execute packed updates");
    DestroyRuntime(runtime);
    std::printf("NeoCore fusion: %d/%d passed; %d fused cases; %d update cases\n",checks-failures,checks,fusedCases,updateCases);
    return failures?1:0;
}
