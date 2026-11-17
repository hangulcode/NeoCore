#include "NeoCoreHost.h"
#include "NeoCoreDebugger.h"
#include <cstdio>
#include <stdexcept>

namespace {
int checks=0;
void Require(bool value,const std::string& label){++checks;if(!value)throw std::runtime_error(label);}
const NeoCore::ProfileEntry& Entry(const NeoCore::ProfileSnapshot& p,const std::string& name) {
    for(const auto& e:p.entries)if(e.name==name)return e;
    throw std::runtime_error("missing profile entry: "+name);
}
void Accounting(const NeoCore::ProfileSnapshot& p) {
    Require(p.enabled && p.complete,"profile remains complete");
    std::uint64_t self=0;
    for(const auto& entry:p.entries) {
        self+=entry.selfNanoseconds;
        Require(entry.inclusiveNanoseconds>=entry.selfNanoseconds,"inclusive >= self: "+entry.name);
        Require(entry.inclusiveNanoseconds<=p.activeNanoseconds,"recursive inclusive does not exceed active time: "+entry.name);
    }
    Require(self==p.activeNanoseconds,"self times partition active execution");
}
void Test(bool debug,bool image) {
    NeoCore::HostSchema schema;
    const auto obj=schema.AddObject("Host");
    const auto hit=schema.AddMethod(obj,"Hit",schema.Types().Int(),{});
    const auto nested=schema.AddMethod(obj,"Nested",schema.Types().Int(),{});
    const auto throws=schema.AddMethod(obj,"Throws",schema.Types().Int(),{});
    const auto reenter=schema.AddMethod(obj,"Reenter",schema.Types().Int(),{schema.Types().Int()});
    schema.AddGlobal("host",obj);
    int live=1;
    NeoCore::HostBindings bindings(schema,[](void* p,NeoCore::TypeId,NeoCore::ObjectHandle)->void* {return p;},&live);
    bindings.BindGlobal("host",NeoCore::ObjectHandle(1));
    bindings.Bind(hit,[](NeoCore::HostCall& c){c.ReturnInt(1);return true;});
    bindings.Bind(throws,[](NeoCore::HostCall&)->bool {throw std::runtime_error("host failure");});
    bindings.Bind(nested,[](NeoCore::HostCall& c){const auto r=c.CallScript("Callback");c.ReturnInt(r.value.asInt());return true;});
    bindings.Bind(reenter,[](NeoCore::HostCall& c){const auto r=c.CallScript("Bounce",{NeoCore::Scalar::Int(c.Int(0))});c.ReturnInt(r.value.asInt());return r.ok();});
    NeoCore::Error error;
    auto program=NeoCore::Program::Compile(
        "import coroutine;\n"
        "export int Leaf(){return host.Hit();}\n"
        "export int Callback(){return Leaf();}\n"
        "export int Root(){int v=0;for(int i in 0,3)v+=Leaf();return v+host.Nested();}\n"
        "fn()->int Make(int seed){return fn()->int{return seed+Leaf();};}\n"
        "export int Closure(){fn()->int f=Make(4);return f()+f();}\n"
        "export int Indirect(){fn()->int f=Leaf;return f();}\n"
        "export int Wait(){sleep(0);return Leaf();}\n"
        "export int Fail(){int z=0;return 1/z;}\n"
        "export int HostFail(){return host.Throws();}\n"
        "export int Recurse(int n){if(n==0)return 1;return Recurse(n-1)+1;}\n"
        "export int Bounce(int n){if(n==0)return 1;return host.Reenter(n-1)+1;}\n"
        "export int MutualA(int n){if(n==0)return 1;return MutualB(n-1)+1;}\n"
        "export int MutualB(int n){if(n==0)return 1;return MutualA(n-1)+1;}\n"
        "export int Tree(int n){if(n<2)return n;return Tree(n-1)+Tree(n-2);}\n"
        "export void Chain(int n){Leaf();if(n>0){coroutine<fn(int)->void> c=coroutine.create(Chain);coroutine.resume(c,n-1);coroutine.resume(c,n-1);coroutine.close(c);}yield;Leaf();}\n"
        "export void CoroutineTree(){coroutine<fn(int)->void> c=coroutine.create(Chain);coroutine.resume(c,3);coroutine.resume(c,3);coroutine.close(c);}\n"
        "void Work(){Leaf();yield;Leaf();}\n"
        "export void Coroutines(){coroutine<fn()->void> c=coroutine.create(Work);coroutine.resume(c);coroutine.resume(c);coroutine.close(c);}\n",
        bindings,error,"profile.nc",debug);
    Require(program!=nullptr,"compile: "+error.message);
    if(image){std::vector<std::uint8_t> data;Require(program->SaveImage(data,error),"save");program=NeoCore::Program::LoadImage(data,bindings,error);Require(program!=nullptr,"load");}
    auto instance=program->CreateInstance(error);
    auto peer=program->CreateInstance(error);
    Require(instance && peer,"instances");
    Require(instance->Call("Root").value.asInt()==4,"unprofiled result");
    Require(instance->ReadProfile().entries.empty(),"off by default");
    Require(instance->SetProfiling(true,error),"enable: "+error.message);
    Require(instance->Call("Root").value.asInt()==4,"profiled result");
    auto p=instance->ReadProfile();
    Require(p.enabled && p.complete,"enabled complete snapshot");
    Require(Entry(p,"Root").calls==1 && Entry(p,"Leaf").calls==4 && Entry(p,"Callback").calls==1,"script call counts");
    Require(Entry(p,"Host.Hit").calls==4 && Entry(p,"Host.Nested").calls==1,"host call counts");
    Require(Entry(p,"Root").inclusiveNanoseconds>=Entry(p,"Leaf").selfNanoseconds,"parent inclusive time");
    Require(Entry(p,"Host.Nested").inclusiveNanoseconds>=Entry(p,"Callback").inclusiveNanoseconds,"host callback nesting");
    std::uint64_t self=0;for(const auto& e:p.entries){self+=e.selfNanoseconds;Require(e.inclusiveNanoseconds>=e.selfNanoseconds,"inclusive >= self");}
    Require(self==p.activeNanoseconds && self>0,"self times partition active execution");
    Require(peer->Call("Root").ok() && peer->ReadProfile().entries.empty(),"instance isolation");
    Require(instance->Call("Wait").suspended(),"suspend");
    const auto paused=instance->ReadProfile();
    Require(!instance->SetProfiling(false,error) && !instance->ResetProfile(error),"busy configuration rejected");
    Require(instance->ReadProfile().activeNanoseconds==paused.activeNanoseconds,"suspended snapshot does not accumulate time");
    Require(instance->Resume().value.asInt()==1,"resume");
    Require(Entry(instance->ReadProfile(),"Wait").calls==1,"resume does not count another call");
    Require(!instance->Call("Fail").ok() && instance->Call("Root").ok(),"failure and recovery");
    Require(!instance->Call("HostFail").ok() && instance->Call("Root").ok(),"host exception and recovery");
    Require(Entry(instance->ReadProfile(),"Host.Throws").calls==1,"failed host call counted once");
    const auto beforeRecursion=instance->ReadProfile();
    Require(instance->Call("Recurse",{NeoCore::Scalar::Int(4)}).value.asInt()==5,"recursion result");
    const auto recursive=instance->ReadProfile();
    Require(Entry(recursive,"Recurse").calls==5,"recursive call counts");
    Require(Entry(recursive,"Recurse").inclusiveNanoseconds<=recursive.activeNanoseconds-beforeRecursion.activeNanoseconds,
        "recursive invocations share one inclusive interval");
    Require(instance->Call("Coroutines").ok(),"coroutines");
    Require(instance->Call("Wait").suspended() && instance->Cancel(error),"cancel");
    NeoCore::ExecutionLimits slice;
    slice.instructionBudget=3;
    auto sliced=instance->Call("Recurse",{NeoCore::Scalar::Int(4)},slice);
    unsigned resumes=0;
    while(sliced.ok() && sliced.suspended() && resumes++<100) sliced=instance->Resume(0,3);
    Require(sliced.ok() && !sliced.suspended() && sliced.value.asInt()==5,"budget resume completes");
    Require(Entry(instance->ReadProfile(),"Recurse").calls==10,"budget resume preserves counts");
    Require(instance->Call("MutualA",{NeoCore::Scalar::Int(12)}).value.asInt()==13,"mutual recursion result");
    Require(instance->Call("Tree",{NeoCore::Scalar::Int(10)}).value.asInt()==55,"branching recursion result");
    Require(Entry(instance->ReadProfile(),"Tree").calls==177,"branching recursion counts every invocation");
    Require(instance->Call("Bounce",{NeoCore::Scalar::Int(6)}).value.asInt()==7,"recursive host reentry result");
    Require(Entry(instance->ReadProfile(),"Bounce").calls==7 && Entry(instance->ReadProfile(),"Host.Reenter").calls==6,"recursive host reentry counts");
    Require(instance->Call("CoroutineTree").ok(),"nested coroutine yield and resume");
    Require(Entry(instance->ReadProfile(),"Chain").calls==4,"coroutine resume does not recount frames");
    Require(instance->Call("Closure").value.asInt()==10 && instance->Call("Indirect").value.asInt()==1, "profiled closure and function value");
    Require(Entry(instance->ReadProfile(),"Closure").calls==1 && Entry(instance->ReadProfile(),"Indirect").calls==1, "indirect boundaries retain profiling");
    Accounting(instance->ReadProfile());
    Require(instance->SetProfiling(false,error),"disable");
    const auto frozen=instance->ReadProfile();
    // 상태 객체를 남긴 채 끈 뒤에도 재진입·클로저·중단 경로가 기록을 다시 켜면 안 된다.
    Require(instance->Call("Root").ok(),"disabled result");
    Require(instance->Call("Closure").value.asInt()==10 && instance->Call("Indirect").value.asInt()==1, "disabled indirect results");
    Require(instance->Call("Wait").suspended() && instance->Resume(0).ok(), "disabled suspension and resume");
    Require(instance->Call("Coroutines").ok() && !instance->Call("Fail").ok(), "disabled coroutine and failure");
    const auto disabled=instance->ReadProfile();
    Require(!disabled.enabled && disabled.activeNanoseconds==frozen.activeNanoseconds && disabled.entries.size()==frozen.entries.size(),"disabled counters frozen");
    for(const auto& entry:frozen.entries) {
        const auto& after=Entry(disabled,entry.name);
        Require(after.calls==entry.calls && after.selfNanoseconds==entry.selfNanoseconds && after.inclusiveNanoseconds==entry.inclusiveNanoseconds, "disabled entry frozen: "+entry.name);
    }
    Require(instance->ResetProfile(error) && instance->ReadProfile().entries.empty(),"reset counters");
    Require(instance->SetProfiling(true,error) && instance->Call("Root").ok(),"reenable");
    Require(instance->Reset(error) && !instance->ReadProfile().enabled && instance->ReadProfile().entries.empty(),"instance reset discards profiling state");
}
// 같은 제어 루프에서 시간 제한만 사용하거나 프로파일만 사용해도 다른 기능이 켜지면 안 된다.
// state 2는 한 번 수집한 뒤 꺼서 객체가 남은 상태다. 프로파일 포인터 유무로 잘못 판단하는 회귀를 잡는다.
void ControlledProfile() {
    NeoCore::Error error;
    auto program=NeoCore::Program::Compile(
        "export int Leaf(int n){return n+1;} export int Count(){int s=0;for(int i in 0,600){s+=Leaf(i);}return s;}",
        error,"profile_controls.nc",true);
    Require(program!=nullptr,"compile independent profile controls");
    for(int state:{0,1,2})for(int limit:{0,1,2}) {
        auto instance=program->CreateInstance(error);
        Require(instance!=nullptr,"create controlled profile instance");
        if(state!=0)Require(instance->SetProfiling(true,error),"enable controlled profile");
        if(state==2) {
            Require(instance->Call("Leaf",{NeoCore::Scalar::Int(0)}).ok(),"collect before disabling");
            Require(instance->SetProfiling(false,error),"disable retained collector");
        }
        const auto before=instance->ReadProfile();
        NeoCore::ExecutionLimits limits;
        if(limit==1)limits.instructionBudget=3;
        if(limit==2)limits.timeoutMs=0;
        auto result=instance->Call("Count",{},limits);
        Require(result.ok() && result.suspended()==(limit!=0),"only requested limit suspends");
        unsigned resumes=0;
        while(result.ok() && result.suspended() && resumes++<10000) result=instance->Resume(0,limits.instructionBudget);
        Require(result.ok() && !result.suspended() && result.value.asInt()==180300,"controlled execution result");
        Require(instance->Call("Count").value.asInt()==180300,"later unlimited execution result");
        const auto after=instance->ReadProfile();
        if(state==1) {
            Require(Entry(after,"Count").calls==2 && Entry(after,"Leaf").calls==1200,"slices do not inflate call counts");
            Accounting(after);
        } else {
            Require(!after.enabled && after.activeNanoseconds==before.activeNanoseconds && after.entries.size()==before.entries.size(),"limits do not enable profiling");
            for(const auto& entry:before.entries) {
                const auto& frozen=Entry(after,entry.name);
                Require(frozen.calls==entry.calls && frozen.selfNanoseconds==entry.selfNanoseconds && frozen.inclusiveNanoseconds==entry.inclusiveNanoseconds,"disabled controlled counters frozen");
            }
        }
    }
}
// 디버그 공용 루프의 프로파일·시간 제한 조합도 실제 중단·재개로 실행한다. 소스 디버그 정보만 켜는 검사는 이 경로를 타지 않는다.
void DebugProfile() {
    NeoCore::Error error;
    auto runtime=NeoCore::Runtime::Create(error);
    Require(runtime!=nullptr,"create debugger profile runtime");
    auto program=runtime->Compile("export int Leaf(int x){\nreturn x+1;\n}\nexport int Root(){int v=Leaf(2);return Leaf(v);}",error,"profile_debug.nc",true);
    Require(program!=nullptr,"compile debugger profile");
    NeoCore::Debugger* debugger=runtime->GetDebugger();
    for(bool enabled:{false,true})for(unsigned budget:{0u,3u})for(bool step:{false,true}) {
        auto instance=program->CreateInstance(error);
        Require(instance && instance->SetProfiling(enabled,error) && debugger->Attach(*instance,error),"attach profiled debugger");
        NeoCore::DebugLocation point;point.sourceName="profile_debug.nc";point.line=2;
        Require(debugger->SetBreakpoints(*instance,{point},error),"profile breakpoint");
        NeoCore::ExecutionLimits limits;limits.instructionBudget=budget;
        auto result=instance->Call("Root",{},limits);
        unsigned resumes=0,stops=0;
        while(result.ok() && result.suspended() && resumes++<100) {
            Require(!instance->SetProfiling(!enabled,error),"cannot switch profiling while paused");
            if(debugger->IsPaused(*instance)){
                ++stops;
                Require(step ? debugger->StepInto(*instance) : debugger->Continue(*instance),"continue or step profile breakpoint");
            }
            result=instance->Resume(0,budget);
        }
        Require(result.ok() && !result.suspended() && result.value.asInt()==4 && (step ? stops>=2 : stops==2),"profiled debugger execution completes");
        const auto p=instance->ReadProfile();
        if(enabled){Require(Entry(p,"Root").calls==1 && Entry(p,"Leaf").calls==2,"debugger resumes preserve call counts");Accounting(p);}
        else Require(!p.enabled && p.entries.empty(),"nonprofiled debugger does not collect");
        debugger->Detach(*instance);
    }
}
}
int main(){try{for(bool debug:{false,true})for(bool image:{false,true})Test(debug,image);ControlledProfile();DebugProfile();
std::printf("profile regression: %d checks passed\n",checks);return 0;}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
