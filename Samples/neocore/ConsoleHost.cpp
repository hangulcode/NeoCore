#include "ConsoleHost.h"
#include "NeoCoreTextFile.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace
{
std::string Normalize(std::string path)
{
    std::replace(path.begin(),path.end(),'\\','/');
#ifdef _WIN32
    std::transform(path.begin(),path.end(),path.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
#endif
    return path;
}
void Require(bool success, const std::string& message)
{ if (!success) throw std::runtime_error(message.empty()?"cannot build console host schema":message); }
void AddPaths(const NeoCore::HostSchema& schema, const std::vector<NeoCore::HostBinding>& members,
    NeoCore::TypeId type, const std::string& prefix, std::vector<NeoCore::TypeId> ancestors,
    std::map<std::string, NeoCore::HostBindingId>& paths)
{
    using namespace NeoCore;
    if (std::find(ancestors.begin(),ancestors.end(),type)!=ancestors.end()) return;
    ancestors.push_back(type);
    for (const HostBinding& member:members)
    {
        if (member.ownerType!=type) continue;
        const std::string name=prefix+"."+member.name;
        if (member.kind==HostBindingKind::Method)
            paths.emplace(name+(member.eventName.empty()?"":"."+member.eventName),member.id);
        else
        {
            paths.emplace(name+(member.kind==HostBindingKind::Getter?".get":".set"),member.id);
            const HostProperty* property=schema.FindProperty(type,member.name);
            if (member.kind==HostBindingKind::Getter && property && !property->setter) paths.emplace(name,member.id);
            if (member.kind==HostBindingKind::Getter && property && schema.Types().Get(property->type).kind==TypeKind::Native)
                AddPaths(schema,members,property->type,name,ancestors,paths);
        }
    }
}
}

NeoCore::ObjectHandle ConsoleHost::NewObject(NeoCore::TypeId type)
{
    if (nextId_ == 0) throw std::runtime_error("console object ID space exhausted");
    const std::uint64_t id = nextId_++;
    objects_.emplace(id,Object{type});
    return NeoCore::ObjectHandle(id);
}
void* ConsoleHost::Resolve(void* context, NeoCore::TypeId type, NeoCore::ObjectHandle object)
{
    ConsoleHost& host = *static_cast<ConsoleHost*>(context);
    const auto found = host.objects_.find(object.value);
    return found!=host.objects_.end() && found->second.type==type ? &found->second : nullptr;
}

ConsoleHost::ConsoleHost(const NeoCore::HostSchemaDocument& document, const std::string& implementationSource,
    const std::string& resourceRoot) : schema_(document.schema), resourceRoot_(resourceRoot)
{
    using namespace NeoCore;
    const std::vector<HostBinding> members = schema_.Bindings();
    const std::vector<HostGlobal> globals = schema_.Globals();
    std::vector<TypeId> objects;
    for (const auto& named:schema_.Types().NamedTypes())
        if (schema_.Types().Get(named.second).kind==TypeKind::Native) objects.push_back(named.second);
    std::map<HostBindingId,HostFunction> functions;
    if (!implementationSource.empty())
    {
        Require(!schema_.FindGlobal("Headless") && schema_.Types().LookupNamed("Headless")==InvalidType,
            "host schema already contains the console implementation API name 'Headless'");
        const TypeId api = schema_.AddObject("HeadlessApi"); Require(api!=InvalidType,schema_.LastError());
        Require(schema_.AddGlobal("Headless",api),schema_.LastError());
        config_.reset(new ScriptHostCompileConfig()); config_->apiGlobal="Headless";
        const std::string allowed=Normalize(implementationSource);
        config_->allowsSource=[allowed](const std::string& source){return Normalize(source)==allowed;};
        for (const HostBinding& member:members)
            config_->members.emplace(HostMemberPath(schema_,member),member.id);
        for (const HostGlobal& global:globals)
            AddPaths(schema_,members,global.type,global.name,{},config_->members);
        for (const auto& entry:config_->members)
        {
            const std::string& path=entry.first;
            const HostBinding& member=members[entry.second-1];
            // 스크립트 함수는 Out 매개변수를 선언할 수 없다. 그런 멤버는 스크립트로 구현할 수 없으므로 Bind 를 만들지 않는다.
            if (std::find(member.parameterModes.begin(),member.parameterModes.end(),ApiParameterMode::Out)!=member.parameterModes.end()) continue;
            std::vector<TypeId> parameters(1,member.ownerType);
            parameters.insert(parameters.end(),member.parameters.begin(),member.parameters.end());
            const TypeId callback=schema_.Types().FunctionOf(member.returnType,parameters);
            const HostBindingId bind=schema_.AddEvent(api,path,callback,"Bind"); Require(bind!=0,schema_.LastError());
            functions[bind]=[](HostCall&){return true;};
        }
        for (TypeId type:objects)
        {
            const std::string name=schema_.Types().DisplayName(type);
            const HostBindingId create=schema_.AddMethod(api,"New_"+name,type,{}); Require(create!=0,schema_.LastError());
            functions[create]=[this,type](HostCall& call){call.ReturnObject(NewObject(type));return true;};
            const HostBindingId id=schema_.AddMethod(api,"Id_"+name,schema_.Types().Int(),{type}); Require(id!=0,schema_.LastError());
            functions[id]=[](HostCall& call){call.ReturnInt(call.IsNull(0)?0:static_cast<std::int64_t>(call.ObjectId(0).value));return true;};
            const HostBindingId remove=schema_.AddMethod(api,"Destroy_"+name,schema_.Types().Void(),{type}); Require(remove!=0,schema_.LastError());
            functions[remove]=[this](HostCall& call){if(!call.IsNull(0))objects_.erase(call.ObjectId(0).value);return true;};
            const HostBindingId validate=schema_.AddMethod(api,"Validate_"+name,schema_.Types().Void(),{type}); Require(validate!=0,schema_.LastError());
            functions[validate]=[](HostCall& call){if(call.IsNull(0)){call.Fail("cannot access a null host object");return false;}return true;};
            config_->validators[type]=validate;
        }
        for (const HostGlobal& global:globals)
        {
            const HostBindingId set=schema_.AddMethod(api,"SetGlobal_"+global.name,schema_.Types().Void(),{global.type}); Require(set!=0,schema_.LastError());
            const std::string name=global.name;
            functions[set]=[name](HostCall& call){
                Error error; if(!call.CallerInstance().SetHostObject(name,call.IsNull(0)?ObjectHandle():call.ObjectId(0),error)){call.Fail(error.message);return false;}return true;
            };
        }
        const HostBindingId read=schema_.AddMethod(api,"ReadText",schema_.Types().String(),{schema_.Types().String()}); Require(read!=0,schema_.LastError());
        functions[read]=[this](HostCall& call){
            if(call.IsNull(0)){call.Fail("text file path cannot be null");return false;}
            std::string path=call.String(0); std::replace(path.begin(),path.end(),'\\','/');
            if(path.empty() || path[0]=='/' || path.find(':')!=std::string::npos){fileError_="resource path must be relative: "+path;call.ReturnNull();return true;}
            const std::string full=resourceRoot_+(resourceRoot_.empty() || resourceRoot_.back()=='/'?"":"/")+path;
            std::string text; if(!TextFile::Read(full,text,fileError_)){call.ReturnNull();return true;}call.ReturnString(text);return true;
        };
        const HostBindingId lastError=schema_.AddMethod(api,"LastError",schema_.Types().String(),{}); Require(lastError!=0,schema_.LastError());
        functions[lastError]=[this](HostCall& call){call.ReturnString(fileError_);return true;};
    }
    bindings_.reset(new HostBindings(schema_,&Resolve,this));
    for (const HostGlobal& global:schema_.Globals()) Require(bindings_->BindGlobal(global.name,NewObject(global.type)),bindings_->LastError());
    for (const HostBinding& member:schema_.Bindings())
    {
        const auto found=functions.find(member.id);
        const std::string path=HostMemberPath(schema_,member);
        HostFunction function=found!=functions.end()?found->second:HostFunction([path](HostCall& call){call.Fail("no host implementation: "+path);return false;});
        Require(bindings_->Bind(member.id,function),bindings_->LastError());
    }
}
