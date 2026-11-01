#include "NeoCore.h"
#include "NeoCoreCompiler.h"
#include <cstdio>
#include <set>

namespace
{
int checks=0,failures=0;
void Check(bool condition,const std::string& label)
{++checks;if(!condition){++failures;std::fprintf(stderr,"FAIL: %s\n",label.c_str());}}
}
int main()
{
    using namespace NeoCore;
    const std::string source="void First(){\nint a=\"wrong\";\nfloat b=false;\na+1;\n}\nvoid Second(){\nmissing();\nint c=true;\n}\n";
    StaticCompileResult result;
    Check(!CompileStaticSource(source,result),"reject invalid program");
    std::set<int> lines;for(const auto& error:result.diagnostics)lines.insert(error.line);
    Check(lines.count(2) && lines.count(3) && lines.count(7) && lines.count(8),"collect independent errors across statements and functions");
    Check(!lines.count(4),"suppress a dependent use of a failed declaration");
    Check(result.loweredSource.empty() && result.typedIR.nodes.empty(),"invalid program has no executable output");
    Check(!CompileStaticSource(source,result,nullptr,nullptr,2) && result.diagnostics.size()==2 && result.diagnosticsTruncated,"diagnostic cap stops recovery");
    Check(!CompileStaticSource("void F(){int a=;int b=;int c=true;}void G(){int d=false;}",result) && result.diagnostics.size()>=4,"recover syntax and type errors in independent statements");
    Error error;CompileOptions options;
    options.moduleLoader=[](const std::string& name,const std::string&,ModuleSource& out,Error&){
        out.sourceName=name+".nc";out.source="public void F(){\nint x=\"bad\";\nfloat y=false;\n}";return true;
    };
    auto program=Program::Compile("import one;import two;int bad=true;",error,"root.nc",false,options);
    Check(!program && error.diagnostics.size()>=5,"C++ API returns diagnostics from the complete import bundle");
    std::set<std::string> files;for(const auto& item:error.diagnostics)files.insert(item.sourceName);
    Check(files.count("one.nc") && files.count("two.nc") && files.count("root.nc"),"diagnostics retain original module files");
    Check(!error.ok() && error.message==error.diagnostics.front().message,"legacy Error keeps the first diagnostic");
    options.moduleLoader=[](const std::string&,const std::string&,ModuleSource&,Error&){return false;};
    program=Program::Compile("import missing1;import missing2;",error,"root.nc",false,options);
    Check(!program && error.diagnostics.size()==2,"report independent missing imports together");
    options.diagnosticLimit=1;
    program=Program::Compile("import missing1;import missing2;",error,"root.nc",false,options);
    Check(!program && error.diagnostics.size()==1,"module diagnostics respect the configured limit");
    std::printf("NeoCore diagnostics: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
