#include "NeoCoreHostSchemaIO.h"
#include "NeoCoreCompiler.h"
#include "NeoCoreScriptHost.h"
#include "NeoCoreTextFile.h"
#include "../ThirdParty/yyjson/yyjson.h"
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace NeoCore
{
namespace
{
const char* KindNames[] = {"Invalid","Void","Int","Float","Bool","String","Null","Vector2","Vector3","Vector4","Quaternion",
    "Array","Array32","List","Map","Set","Function","Struct","Native","BuiltinObject","Coroutine"};
std::size_t TypeCount(const TypeRegistry& types)
{ TypeId i = 1; while (types.Get(i).kind != TypeKind::Invalid) ++i; return i; }
using Doc = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
using MutDoc = std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)>;
void Require(bool condition, const std::string& error)
{ if (!condition) throw std::runtime_error(error); }
void ValidateDefines(const std::vector<CompileDefine>& defines)
{
    std::vector<StaticDefineReplacement> replacements;
    std::string error;
    int line=1,column=1;
    Require(ExpandStaticDefines("",defines,replacements,error,line,column),error);
}
yyjson_val* Member(yyjson_val* object, const char* name)
{
    yyjson_val* value = yyjson_obj_get(object, name);
    Require(value != nullptr, std::string("missing host schema field: ") + name);
    return value;
}
std::string Text(yyjson_val* value)
{ Require(yyjson_is_str(value), "host schema string expected"); return std::string(yyjson_get_str(value), yyjson_get_len(value)); }
std::uint32_t Integer(yyjson_val* value)
{
    Require(yyjson_is_uint(value) && yyjson_get_uint(value) <= (std::numeric_limits<std::uint32_t>::max)(), "host schema unsigned integer expected");
    return static_cast<std::uint32_t>(yyjson_get_uint(value));
}
bool Flag(yyjson_val* value)
{ Require(yyjson_is_bool(value), "host schema bool expected"); return yyjson_get_bool(value); }
yyjson_val* Array(yyjson_val* value)
{ Require(yyjson_is_arr(value), "host schema array expected"); return value; }
std::string Bits(std::uint64_t bits)
{ std::ostringstream text; text << std::hex << std::setw(16) << std::setfill('0') << bits; return text.str(); }
std::uint64_t Bits(yyjson_val* value)
{
    const std::string text = Text(value);
    Require(text.size() == 16, "host schema bit pattern must contain 16 hexadecimal digits");
    std::uint64_t bits = 0;
    for (char c : text)
    {
        const unsigned n = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : 16;
        Require(n < 16, "invalid host schema hexadecimal digit"); bits = (bits << 4) | n;
    }
    return bits;
}
void Put(yyjson_mut_doc* doc, yyjson_mut_val* object, const char* name, const std::string& value)
{ Require(yyjson_mut_obj_add_strncpy(doc, object, name, value.data(), value.size()), "cannot write host schema string"); }
void Put(yyjson_mut_doc* doc, yyjson_mut_val* object, const char* name, std::uint32_t value)
{ Require(yyjson_mut_obj_add_uint(doc, object, name, value), "cannot write host schema integer"); }
void Put(yyjson_mut_doc* doc, yyjson_mut_val* object, const char* name, bool value)
{ Require(yyjson_mut_obj_add_bool(doc, object, name, value), "cannot write host schema bool"); }
void WriteTypes(yyjson_mut_doc* doc, yyjson_mut_val* array, const TypeRegistry& types)
{
    const std::size_t count = TypeCount(types);
    for (TypeId id = 0; id < count; ++id)
    {
        const TypeDesc& type = types.Get(id);
        yyjson_mut_val* item = yyjson_mut_obj(doc);
        Put(doc,item,"kind",std::string(KindNames[static_cast<unsigned>(type.kind)]));
        Put(doc,item,"name",type.name); Put(doc,item,"element",type.element); Put(doc,item,"key",type.key);
        Put(doc,item,"value",type.value); Put(doc,item,"returnType",type.returnType); Put(doc,item,"defined",type.fieldsDefined);
        yyjson_mut_val* parameters = yyjson_mut_arr(doc);
        yyjson_mut_obj_add_val(doc,item,"parameters",parameters);
        for (TypeId parameter : type.parameters) yyjson_mut_arr_add_uint(doc,parameters,parameter);
        yyjson_mut_val* fields = yyjson_mut_arr(doc);
        yyjson_mut_obj_add_val(doc,item,"fields",fields);
        for (const StructField& field : type.fields)
        {
            yyjson_mut_val* f = yyjson_mut_obj(doc);
            Put(doc,f,"name",field.name); Put(doc,f,"type",field.type); Put(doc,f,"hasDefault",field.hasDefault);
            if (field.hasDefault)
            {
                yyjson_mut_obj_add_sint(doc,f,"integer",field.defaultInteger);
                std::uint64_t number; std::memcpy(&number,&field.defaultNumber,sizeof(number));
                Put(doc,f,"numberBits",Bits(number)); Put(doc,f,"text",field.defaultText);
                yyjson_mut_val* vector = yyjson_mut_arr(doc); yyjson_mut_obj_add_val(doc,f,"vectorBits",vector);
                for (float component : field.defaultVector)
                { std::uint32_t bits; std::memcpy(&bits,&component,sizeof(bits)); yyjson_mut_arr_add_uint(doc,vector,bits); }
            }
            yyjson_mut_arr_append(fields,f);
        }
        yyjson_mut_arr_append(array,item);
    }
}
void ReadTypes(yyjson_val* array, HostSchema& schema)
{
    const std::size_t count = yyjson_arr_size(Array(array)), builtins = TypeCount(schema.Types());
    Require(count >= builtins && count <= (std::numeric_limits<TypeId>::max)(), "invalid host schema type count");
    std::vector<TypeDesc> descriptors;
    for (std::size_t i = 0; i < count; ++i)
    {
        yyjson_val* item = yyjson_arr_get(array,i);
        const std::string kind = Text(Member(item,"kind"));
        TypeDesc type;
        unsigned kindIndex = 0;
        while (kindIndex < sizeof(KindNames)/sizeof(KindNames[0]) && kind != KindNames[kindIndex]) ++kindIndex;
        Require(kindIndex < sizeof(KindNames)/sizeof(KindNames[0]), "unknown host schema type kind");
        type.kind = static_cast<TypeKind>(kindIndex); type.name = Text(Member(item,"name"));
        type.element = Integer(Member(item,"element")); type.key = Integer(Member(item,"key"));
        type.value = Integer(Member(item,"value")); type.returnType = Integer(Member(item,"returnType"));
        type.fieldsDefined = Flag(Member(item,"defined"));
        yyjson_val* parameters = Array(Member(item,"parameters"));
        for (std::size_t p = 0; p < yyjson_arr_size(parameters); ++p) type.parameters.push_back(Integer(yyjson_arr_get(parameters,p)));
        yyjson_val* fields = Array(Member(item,"fields"));
        for (std::size_t f = 0; f < yyjson_arr_size(fields); ++f)
        {
            yyjson_val* data = yyjson_arr_get(fields,f);
            StructField field(Text(Member(data,"name")), Integer(Member(data,"type")));
            field.hasDefault = Flag(Member(data,"hasDefault"));
            if (field.hasDefault)
            {
                yyjson_val* integer = Member(data,"integer");
                Require(yyjson_is_int(integer) && (!yyjson_is_uint(integer) || yyjson_get_uint(integer) <= INT64_MAX), "invalid host field integer default");
                field.defaultInteger = yyjson_get_sint(integer);
                const std::uint64_t bits = Bits(Member(data,"numberBits"));
                std::memcpy(&field.defaultNumber,&bits,sizeof(bits)); field.defaultText = Text(Member(data,"text"));
                yyjson_val* vector = Array(Member(data,"vectorBits")); Require(yyjson_arr_size(vector)==4,"invalid host vector default");
                for (int c=0;c<4;++c) { const std::uint32_t component = Integer(yyjson_arr_get(vector,c)); std::memcpy(&field.defaultVector[c],&component,sizeof(component)); }
            }
            type.fields.push_back(field);
        }
        TypeId created = InvalidType;
        if (i < builtins) created = static_cast<TypeId>(i);
        else
        {
            switch(type.kind)
            {
            case TypeKind::Array: created = schema.Types().ArrayOf(type.element); break;
            case TypeKind::Array32: created = schema.Types().Array32Of(type.element); break;
            case TypeKind::List: created = schema.Types().ListOf(type.element); break;
            case TypeKind::Map: created = schema.Types().MapOf(type.key,type.value); break;
            case TypeKind::Set: created = schema.Types().SetOf(type.element); break;
            case TypeKind::Function: created = schema.Types().FunctionOf(type.returnType,type.parameters); break;
            case TypeKind::Struct: created = schema.AddStruct(type.name); break;
            case TypeKind::Native: created = schema.AddObject(type.name); break;
            case TypeKind::BuiltinObject: created = schema.Types().DeclareBuiltinObject(type.name); break;
            case TypeKind::Coroutine: created = schema.Types().CoroutineOf(type.element); break;
            default: break;
            }
        }
        Require(created == i && (i==0 || created!=InvalidType), "invalid or noncanonical host schema type reference");
        const TypeDesc& actual = schema.Types().Get(created);
        Require(actual.kind==type.kind && actual.name==type.name && actual.element==type.element && actual.key==type.key &&
            actual.value==type.value && actual.returnType==type.returnType && actual.parameters==type.parameters,
            "inconsistent host schema type descriptor");
        Require(type.kind==TypeKind::Struct || (!type.fieldsDefined && type.fields.empty()), "fields require a struct type");
        descriptors.push_back(type);
    }
    for (std::size_t i = 0; i < count; ++i)
    {
        const TypeDesc& type = descriptors[i];
        if (type.kind != TypeKind::Struct) continue;
        Require(type.fieldsDefined && schema.DefineStruct(static_cast<TypeId>(i),type.fields), schema.LastError().empty()?"invalid host struct fields":schema.LastError());
        for (std::size_t f=0;f<type.fields.size();++f)
            if(type.fields[f].hasDefault)
            {
                std::string error;
                Require(schema.Types().SetStructFieldDefault(static_cast<TypeId>(i),f,type.fields[f],&error),error);
            }
    }
}
}

bool HostSchemaDocument::ToJson(std::string& json, std::string& error) const
{
    error.clear();
    try
    {
        Require(schema.Validate(error),error);
        ValidateDefines(defines);
        MutDoc doc(yyjson_mut_doc_new(nullptr),&yyjson_mut_doc_free); Require(bool(doc),"cannot allocate host schema document");
        yyjson_mut_val* root = yyjson_mut_obj(doc.get()); yyjson_mut_doc_set_root(doc.get(),root);
        Put(doc.get(),root,"format",std::string("NeoCore.HostSchema")); Put(doc.get(),root,"version",std::uint32_t(1));
        yyjson_mut_val* types = yyjson_mut_arr(doc.get()); yyjson_mut_obj_add_val(doc.get(),root,"types",types); WriteTypes(doc.get(),types,schema.Types());
        yyjson_mut_val* members = yyjson_mut_arr(doc.get()); yyjson_mut_obj_add_val(doc.get(),root,"members",members);
        for (const HostBinding& binding : schema.Bindings())
        {
            const HostProperty* property = schema.FindProperty(binding.ownerType,binding.name);
            if(property && binding.id != (property->getter ? property->getter : property->setter)) continue;
            yyjson_mut_val* member = yyjson_mut_obj(doc.get());
            Put(doc.get(),member,"owner",binding.ownerType); Put(doc.get(),member,"name",binding.name);
            if(property)
            {
                Put(doc.get(),member,"kind",std::string("property")); Put(doc.get(),member,"type",property->type);
                Put(doc.get(),member,"readable",property->getter!=0); Put(doc.get(),member,"writable",property->setter!=0);
            }
            else
            {
                Put(doc.get(),member,"kind",std::string(binding.eventName.empty()?"method":"event"));
                Put(doc.get(),member,"returnType",binding.returnType); Put(doc.get(),member,"event",binding.eventName);
                yyjson_mut_val* args=yyjson_mut_arr(doc.get()); yyjson_mut_obj_add_val(doc.get(),member,"parameters",args);
                for(TypeId type:binding.parameters) yyjson_mut_arr_add_uint(doc.get(),args,type);
                if(!binding.parameterNames.empty())
                {
                    yyjson_mut_val* names=yyjson_mut_arr(doc.get()); yyjson_mut_obj_add_val(doc.get(),member,"parameterNames",names);
                    for(const std::string& parameterName:binding.parameterNames) yyjson_mut_arr_add_strncpy(doc.get(),names,parameterName.data(),parameterName.size());
                }
                if(!binding.parameterModes.empty())
                {
                    // 0 = in, 1 = out
                    yyjson_mut_val* modes=yyjson_mut_arr(doc.get()); yyjson_mut_obj_add_val(doc.get(),member,"parameterModes",modes);
                    for(ApiParameterMode mode:binding.parameterModes) yyjson_mut_arr_add_uint(doc.get(),modes,mode==ApiParameterMode::Out?1u:0u);
                }
            }
            yyjson_mut_arr_append(members,member);
        }
        yyjson_mut_val* globals=yyjson_mut_arr(doc.get()); yyjson_mut_obj_add_val(doc.get(),root,"globals",globals);
        for(const HostGlobal& global:schema.Globals())
        { yyjson_mut_val* item=yyjson_mut_obj(doc.get()); Put(doc.get(),item,"name",global.name); Put(doc.get(),item,"type",global.type); yyjson_mut_arr_append(globals,item); }
        yyjson_mut_val* values=yyjson_mut_arr(doc.get()); yyjson_mut_obj_add_val(doc.get(),root,"defines",values);
        for(const CompileDefine& define:defines)
        { yyjson_mut_val* item=yyjson_mut_obj(doc.get()); Put(doc.get(),item,"name",define.name); Put(doc.get(),item,"kind",static_cast<std::uint32_t>(define.kind)); Put(doc.get(),item,"text",define.text); yyjson_mut_arr_append(values,item); }
        std::size_t length=0; char* text=yyjson_mut_write(doc.get(),YYJSON_WRITE_PRETTY,&length);
        Require(text!=nullptr,"cannot encode host schema JSON");
        std::unique_ptr<char,decltype(&std::free)> output(text,&std::free); json.assign(text,length); return true;
    }
    catch(const std::exception& failure) { error=failure.what(); return false; }
}

bool HostSchemaDocument::FromJson(const std::string& json, HostSchemaDocument& output, std::string& error)
{
    error.clear();
    try
    {
        std::string input=json; if(input.compare(0,3,"\xef\xbb\xbf")==0) input.erase(0,3);
        yyjson_read_err failure; Doc doc(yyjson_read_opts(&input[0],input.size(),0,nullptr,&failure),&yyjson_doc_free);
        Require(bool(doc),std::string("invalid host schema JSON: ")+(failure.msg?failure.msg:"parse failed"));
        yyjson_val* root=yyjson_doc_get_root(doc.get());
        Require(Text(Member(root,"format"))=="NeoCore.HostSchema" && Integer(Member(root,"version"))==1,"unsupported host schema format/version");
        HostSchemaDocument result; ReadTypes(Array(Member(root,"types")),result.schema);
        yyjson_val* members=Array(Member(root,"members"));
        for(std::size_t i=0;i<yyjson_arr_size(members);++i)
        {
            yyjson_val* member=yyjson_arr_get(members,i); const std::string kind=Text(Member(member,"kind"));
            const TypeId owner=Integer(Member(member,"owner")); const std::string name=Text(Member(member,"name"));
            bool ok=false;
            if(kind=="property") ok=result.schema.AddProperty(owner,name,Integer(Member(member,"type")),Flag(Member(member,"readable")),Flag(Member(member,"writable")));
            else
            {
                std::vector<TypeId> parameters; yyjson_val* args=Array(Member(member,"parameters"));
                for(std::size_t p=0;p<yyjson_arr_size(args);++p) parameters.push_back(Integer(yyjson_arr_get(args,p)));
                const TypeId returns=Integer(Member(member,"returnType")); const std::string event=Text(Member(member,"event"));
                if(kind=="method")
                {
                    Require(event.empty(),"ordinary host method has event metadata");
                    // 이름·방향은 선택 필드다. 없는 파일은 모두 이름 없는 입력 매개변수다.
                    std::vector<std::string> parameterNames; std::vector<ApiParameterMode> parameterModes;
                    if(yyjson_val* names=yyjson_obj_get(member,"parameterNames"))
                        for(std::size_t p=0;p<yyjson_arr_size(Array(names));++p) parameterNames.push_back(Text(yyjson_arr_get(names,p)));
                    if(yyjson_val* modes=yyjson_obj_get(member,"parameterModes"))
                        for(std::size_t p=0;p<yyjson_arr_size(Array(modes));++p)
                        {
                            const std::uint32_t mode=Integer(yyjson_arr_get(modes,p)); Require(mode<=1,"invalid host parameter mode");
                            parameterModes.push_back(mode?ApiParameterMode::Out:ApiParameterMode::In);
                        }
                    ok=(parameterNames.empty() && parameterModes.empty()
                        ? result.schema.AddMethod(owner,name,returns,parameters)
                        : result.schema.AddMethod(owner,name,returns,parameters,parameterNames,parameterModes))!=0;
                }
                else if(kind=="event")
                { Require(parameters.size()==2 && parameters[0]==result.schema.Types().String() && returns==result.schema.Types().Void(),"invalid host event signature"); ok=result.schema.AddEvent(owner,event,parameters[1],name)!=0; }
            }
            Require(ok,result.schema.LastError().empty()?"unknown host member kind":result.schema.LastError());
        }
        yyjson_val* globals=Array(Member(root,"globals"));
        for(std::size_t i=0;i<yyjson_arr_size(globals);++i)
        { yyjson_val* item=yyjson_arr_get(globals,i); Require(result.schema.AddGlobal(Text(Member(item,"name")),Integer(Member(item,"type"))),result.schema.LastError()); }
        yyjson_val* values=Array(Member(root,"defines"));
        for(std::size_t i=0;i<yyjson_arr_size(values);++i)
        {
            yyjson_val* item=yyjson_arr_get(values,i); const std::uint32_t kind=Integer(Member(item,"kind"));
            Require(kind<=static_cast<std::uint32_t>(CompileDefineKind::Null),"invalid compile define kind");
            result.defines.push_back(CompileDefine(Text(Member(item,"name")),static_cast<CompileDefineKind>(kind),Text(Member(item,"text"))));
        }
        Require(result.schema.Validate(error),error); ValidateDefines(result.defines);
        output=std::move(result); return true;
    }
    catch(const std::exception& failure) { error=failure.what(); return false; }
}
bool HostSchemaDocument::SaveFile(const std::string& path, std::string& error) const
{ std::string json; return ToJson(json,error) && TextFile::Write(path,json,error); }
bool HostSchemaDocument::LoadFile(const std::string& path, HostSchemaDocument& output, std::string& error)
{ std::string json; return TextFile::Read(path,json,error) && FromJson(json,output,error); }
std::string HostMemberPath(const HostSchema& schema, const HostBinding& binding)
{
    std::string path = schema.Types().DisplayName(binding.ownerType) + "." + binding.name;
    if (binding.kind == HostBindingKind::Getter) path += ".get";
    else if (binding.kind == HostBindingKind::Setter) path += ".set";
    else if (!binding.eventName.empty()) path += "." + binding.eventName;
    return path;
}
}
