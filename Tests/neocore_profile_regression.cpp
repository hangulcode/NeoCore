#include "NeoCoreHost.h"
#include <cstdio>
#include <stdexcept>

namespace {
int checks=0;
void Require(bool value,const std::string& label){++checks;if(!value)throw std::runtime_error(label);}
const NeoCore::ProfileEntry& Entry(const NeoCore::ProfileSnapshot& p,const std::string& name) {
    for(const auto& e:p.entries)if(e.name==name)return e;
    throw std::runtime_error("missing profile entry: "+name);
}
void Test(bool debug,bool image) {
    NeoCore::HostSchema schema;
    const auto obj=schema.AddObject("Host");
    const auto hit=schema.AddMethod(obj,"Hit",schema.Types().Int(),{});
    const auto nested=schema.AddMethod(obj,"Nested",schema.Types().Int(),{});
    const auto throws=schema.AddMethod(obj,"Throws",schema.Types().Int(),{});
    schema.AddGlobal("host",obj);
    int live=1;
    NeoCore::HostBindings bindings(schema,[](void* p,NeoCore::TypeId,NeoCore::ObjectHandle)->void* {return p;},&live);
    bindings.BindGlobal("host",NeoCore::ObjectHandle(1));
    bindings.Bind(hit,[](NeoCore::HostCall& c){c.ReturnInt(1);return true;});
    bindings.Bind(throws,[](NeoCore::HostCall&)->bool {throw std::runtime_error("host failure");});
    bindings.Bind(nested,[](NeoCore::HostCall& c){const auto r=c.CallScript("Callback");c.ReturnInt(r.value.asInt());return true;});
    NeoCore::Error error;
    auto program=NeoCore::Program::Compile(
        "import coroutine;\n"
        "export int Leaf(){return host.Hit();}\n"
        "export int Callback(){return Leaf();}\n"
        "export int Root(){int v=0;for(int i in 0,3)v+=Leaf();return v+host.Nested();}\n"
        "export int Wait(){sleep(0);return Leaf();}\n"
        "export int Fail(){int z=0;return 1/z;}\n"
        "export int HostFail(){return host.Throws();}\n"
        "export int Recurse(int n){if(n==0)return 1;return Recurse(n-1)+1;}\n"
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
    Require(instance->Call("Recurse",{NeoCore::Scalar::Int(4)}).value.asInt()==5,"recursion result");
    Require(Entry(instance->ReadProfile(),"Recurse").calls==5,"recursive call counts");
    Require(instance->Call("Coroutines").ok(),"coroutines");
    Require(instance->Call("Wait").suspended() && instance->Cancel(error),"cancel");
    NeoCore::ExecutionLimits slice;
    slice.instructionBudget=3;
    auto sliced=instance->Call("Recurse",{NeoCore::Scalar::Int(4)},slice);
    unsigned resumes=0;
    while(sliced.ok() && sliced.suspended() && resumes++<100) sliced=instance->Resume(0,3);
    Require(sliced.ok() && !sliced.suspended() && sliced.value.asInt()==5,"budget resume completes");
    Require(Entry(instance->ReadProfile(),"Recurse").calls==10,"budget resume preserves counts");
    Require(instance->SetProfiling(false,error),"disable");
    const auto frozen=instance->ReadProfile();
    Require(instance->Call("Root").ok(),"disabled result");
    Require(instance->ReadProfile().activeNanoseconds==frozen.activeNanoseconds,"disabled counters frozen");
    Require(instance->ResetProfile(error) && instance->ReadProfile().entries.empty(),"reset counters");
    Require(instance->SetProfiling(true,error) && instance->Call("Root").ok(),"reenable");
    Require(instance->Reset(error) && !instance->ReadProfile().enabled && instance->ReadProfile().entries.empty(),"instance reset discards profiling state");
}
}
int main(){try{for(bool debug:{false,true})for(bool image:{false,true})Test(debug,image);
std::printf("profile regression: %d checks passed\n",checks);return 0;}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
