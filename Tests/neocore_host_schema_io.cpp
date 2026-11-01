#include "NeoCoreHostSchemaIO.h"
#include "NeoCoreHost.h"
#include "NeoCoreCompiler.h"
#include <cstdio>
#include <cmath>
#include <limits>

namespace
{
int checks=0, failures=0;
void Check(bool condition,const std::string& label)
{ ++checks; if(!condition){++failures;std::fprintf(stderr,"FAIL: %s\n",label.c_str());} }
NeoCore::HostSchemaDocument Fixture()
{
    using namespace NeoCore;
    HostSchemaDocument document;
    HostSchema& schema=document.schema;
    TypeRegistry& types=schema.Types();
    const TypeId services=schema.AddObject("Services"), data=schema.AddObject("UserData"), resource=schema.AddObject("Resource"), node=schema.AddObject("GameObject");
    schema.AddGlobal("Services",services); schema.AddGlobal("this",node);
    schema.AddProperty(services,"UserData",data,true,false); schema.AddProperty(services,"Resource",resource,true,false);
    schema.AddMethod(services,"Log",types.Void(),{types.String()});
    schema.AddMethod(data,"SetInt",types.Void(),{types.String(),types.Int()});
    schema.AddMethod(data,"GetInt",types.Int(),{types.String(),types.Int()});
    schema.AddMethod(resource,"LoadText",types.String(),{types.String()});
    schema.AddProperty(node,"Id",types.Int(),true,false); schema.AddProperty(node,"Position",types.Vector3());
    schema.AddMethod(node,"FindChildByRID",node,{types.String()});
    schema.AddMethod(node,"Missing",types.Void(),{});
    schema.AddEvent(node,"OnClick",types.FunctionOf(types.Void(),{types.Int()}));
    const TypeId record=schema.AddStruct("HostRecord");
    schema.DefineStruct(record,{{"id",types.Int()},{"weight",types.Float()},{"name",types.String()},{"values",types.ListOf(types.Int())}});
    StructField field("id",types.Int());field.hasDefault=true;field.defaultInteger=(std::numeric_limits<std::int64_t>::max)();
    types.SetStructFieldDefault(record,0,field);
    field=StructField("weight",types.Float());field.hasDefault=true;field.defaultNumber=-0.0;types.SetStructFieldDefault(record,1,field);
    field=StructField("name",types.String());field.hasDefault=true;field.defaultText="a\nb";types.SetStructFieldDefault(record,2,field);
    const TypeId function=types.FunctionOf(record,{types.MapOf(types.String(),types.ListOf(record))});
    schema.AddMethod(services,"Transform",record,{function,types.ArrayOf(types.Float()),types.Array32Of(types.Int()),types.SetOf(types.String())});
    document.defines.push_back(CompileDefine("KEY_TEST",CompileDefineKind::Int,"17"));
    return document;
}
}

int main(int argc,char** argv)
{
    using namespace NeoCore;
    HostSchemaDocument document=Fixture(),restored;
    std::string json,error,reencoded;
    Check(document.ToJson(json,error),"encode schema: "+error);
    Check(HostSchemaDocument::FromJson(json,restored,error),"decode schema: "+error);
    Check(restored.ToJson(reencoded,error) && reencoded==json,"canonical schema roundtrip: "+error);
    const auto record=restored.schema.Types().LookupNamed("HostRecord");
    const auto& fields=restored.schema.Types().Get(record).fields;
    Check(fields.size()==4 && fields[0].defaultInteger==(std::numeric_limits<std::int64_t>::max)() && std::signbit(fields[1].defaultNumber)
        && fields[2].defaultText=="a\nb","lossless int64, negative zero and escaped string defaults");
    Check(restored.defines.size()==1 && restored.defines[0].name=="KEY_TEST" && restored.defines[0].text=="17","compile defines survive roundtrip");
    HostSchemaDocument invalidDefines=document;
    invalidDefines.defines.push_back(document.defines[0]);
    std::string invalidJson;
    Check(!invalidDefines.ToJson(invalidJson,error),"reject duplicate compile defines at export");
    std::string invalidDocument=json;
    const std::size_t valuePosition=invalidDocument.find("\"text\": \"17\"");
    if(valuePosition!=std::string::npos) invalidDocument.replace(valuePosition,12,"\"text\": \"no\"");
    Check(valuePosition!=std::string::npos && !HostSchemaDocument::FromJson(invalidDocument,invalidDefines,error),"reject malformed define values at import");
    const char* source="HostRecord value;export int Probe(){return value.id;}";
    StaticCompileResult a,b;
    Check(CompileStaticSource(source,a,&document.schema) && CompileStaticSource(source,b,&restored.schema) && a.loweredSource==b.loweredSource,"original and restored schemas compile identically");
    for(const std::string& invalid:{std::string("{}"),std::string("not json"),std::string("{\"format\":\"NeoCore.HostSchema\",\"version\":999}")})
    {
        Check(!HostSchemaDocument::FromJson(invalid,restored,error) && !error.empty(),"invalid schema is rejected");
        std::string after;Check(restored.ToJson(after,error) && after==json,"failed load preserves the previous schema");
    }
    if(argc==3 && std::string(argv[1])=="--write") Check(document.SaveFile(argv[2],error),"write fixture: "+error);
    std::printf("NeoCore host schema IO: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
