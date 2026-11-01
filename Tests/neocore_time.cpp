#include "NeoCore.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
int checks=0;
void Check(bool condition,const std::string& label)
{
    ++checks;
    if(!condition) throw std::runtime_error(label);
}

void SetZone(const char* zone)
{
#ifdef _WIN32
    Check(_putenv_s("TZ",zone?zone:"")==0,"set test timezone");
    _tzset();
#else
    Check((zone?setenv("TZ",zone,1):unsetenv("TZ"))==0,"set test timezone");
    tzset();
#endif
}

struct ZoneScope
{
    bool existed;
    std::string original;
    ZoneScope():existed(std::getenv("TZ")!=nullptr),original(existed?std::getenv("TZ"):"") {}
    ~ZoneScope()
    {
#ifdef _WIN32
        _putenv_s("TZ",existed?original.c_str():"");
        _tzset();
#else
        if(existed) setenv("TZ",original.c_str(),1); else unsetenv("TZ");
        tzset();
#endif
    }
};

void Text(NeoCore::Program& program,const char* function,std::int64_t time,const char* expected)
{
    const auto result=program.Call(function,{NeoCore::Scalar::Int(time)});
    Check(result.ok() && result.value.kind()==NeoCore::Scalar::Kind::String && result.value.asString()==expected,
        std::string(function)+": "+result.error.message+(result.ok()?" got "+result.value.asString():""));
}

void Run(bool debug)
{
    NeoCore::Error error;
    auto program=NeoCore::Program::Compile(
        "import system;\n"
        "export string Local(int seconds){return system.date(\"%Y-%m-%d %H:%M:%S\",seconds);}\n"
        "export string Utc(int seconds){return system.utc_date(\"%Y-%m-%d %H:%M:%S\",seconds);}\n"
        "export string Format(string format,int seconds){return system.date(format,seconds);}\n",
        error,"local-date.nc",debug);
    Check(bool(program),"compile local date: "+error.message);
    std::vector<std::uint8_t> image;
    Check(program->SaveImage(image,error),"save date image");
    for(int pass=0;pass<2;++pass)
    {
        if(pass) program=NeoCore::Program::LoadImage(image,error);
        Check(bool(program),"load date image: "+error.message);
        SetZone("UTC0");
        Text(*program,"Local",1704067200,"2024-01-01 00:00:00");
        Text(*program,"Utc",1704067200,"2024-01-01 00:00:00");
        SetZone("PST8PDT");
        Text(*program,"Local",1704067200,"2023-12-31 16:00:00");
        Text(*program,"Local",1719792000,"2024-06-30 17:00:00");
        Text(*program,"Utc",1719792000,"2024-07-01 00:00:00");
        const auto empty=program->Call("Format",{NeoCore::Scalar::String(""),NeoCore::Scalar::Int(0)});
        Check(empty.ok() && empty.value.asString().empty(),"empty local date format");
        const auto zone=program->Call("Format",{NeoCore::Scalar::String("%z %Z"),NeoCore::Scalar::Int(1704067200)});
        Check(zone.ok() && zone.value.asString().find("-0800")!=std::string::npos,"local format uses timestamp timezone offset");
        for(const std::string& format:{std::string("%Q"),std::string(80,'x'),std::string("a\0b",3)})
        {
            const auto failed=program->Call("Format",{NeoCore::Scalar::String(format),NeoCore::Scalar::Int(0)});
            Check(!failed.ok() && failed.error.message.find("invalid system.date format")!=std::string::npos &&
                (!debug || failed.error.line==4),"invalid local format reports a runtime error at the caller");
        }
        for(std::int64_t seconds:{(std::numeric_limits<std::int64_t>::min)(),(std::numeric_limits<std::int64_t>::max)()})
        {
            const auto failed=program->Call("Local",{NeoCore::Scalar::Int(seconds)});
            Check(!failed.ok() && failed.error.message.find("outside the host local calendar range")!=std::string::npos,
                "wide timestamps fail before narrowing");
        }
    }
    for(const char* source:{"system.date(\"%Y\",0);","import system;system.date(\"%Y\");",
        "import system;system.date(1,0);","import system;system.date(\"%Y\",0.0);"})
        Check(!NeoCore::Program::Compile(source,error),"date signature is checked before execution");
}
}

int main()
{
    ZoneScope restore;
    try
    {
        Run(false);
        Run(true);
        std::printf("NeoCore local date: %d checks passed\n",checks);
        return 0;
    }
    catch(const std::exception& failure)
    {
        std::fprintf(stderr,"NeoCore local date: %s\n",failure.what());
        return 1;
    }
}
