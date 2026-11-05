#include "NeoCore.h"
#include "NeoCoreCompiler.h"
#include "NeoCoreConsoleAccess.h"
#include "NeoCoreHostSchema.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
int checks = 0, failures = 0;
void Check(bool condition, const std::string& label)
{
    ++checks;
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label.c_str()); }
}

bool Has(const std::vector<NeoCore::CompileDiagnostic>& warnings, const std::string& text)
{
    for (const auto& warning : warnings) if (warning.message == text) return true;
    return false;
}

void Analyze(const std::string& source, const std::vector<std::string>& expected,
    const NeoCore::HostSchema* schema = nullptr)
{
    NeoCore::StaticCompileResult regular, analyzed;
    std::vector<NeoCore::CompileDiagnostic> warnings;
    const bool normal = NeoCore::CompileStaticSource(source, regular, schema);
    Check(normal, "regular compilation: " + regular.errorMessage);
    const bool success = NeoCore::CompileStaticSource(source, analyzed, schema, nullptr, 100,
        nullptr, std::function<bool(int)>(), nullptr, &warnings);
    Check(success, "analyzed compilation: " + analyzed.errorMessage);
    Check(analyzed.success && analyzed.diagnostics.empty(), "warnings do not become errors");
    Check(regular.loweredSource == analyzed.loweredSource, "unused analysis leaves lowering unchanged");
    Check(warnings.size() == expected.size(), "warning count: " + std::to_string(warnings.size()));
    for (const std::string& item : expected) Check(Has(warnings, item), item);
    for (const auto& warning : warnings) Check(warning.line > 0 && warning.column > 0, "declaration location");
}

void Images(bool debug)
{
    using namespace NeoCore;
    const std::string source = "int effects=0;int Init(){effects+=1;return 7;}int unused=Init();"
        "const int DEAD=9;export int Probe(){return effects;}";
    Error error;
    CompileOptions options;
    auto regular = Program::Compile(source, error, "image.nc", debug, options);
    Check(bool(regular), "ordinary image compilation: " + error.message);
    std::vector<CompileDiagnostic> warnings;
    auto analyzed = ConsoleCompileAccess::Check(source, nullptr, error, "image.nc", debug, options, nullptr, warnings);
    Check(bool(analyzed) && error.ok() && error.diagnostics.empty(), "analyzed image compilation: " + error.message);
    Check(warnings.size() == 2 && Has(warnings, "unused global variable 'unused'") && Has(warnings, "unused constant 'DEAD'"), "separate warning output");
    if (!regular || !analyzed) return;
    std::vector<std::uint8_t> a, b;
    Check(regular->SaveImage(a, error) && analyzed->SaveImage(b, error) && a == b,
        "serialized bytecode identical with warnings off/on");
    const auto before = regular->Call("Probe");
    const auto after = analyzed->Call("Probe");
    Check(before.ok() && after.ok() && before.value.asInt() == 1 && after.value.asInt() == 1,
        "unused initializer side effects preserved");
    analyzed = ConsoleCompileAccess::Check("int bad=true;", nullptr, error, "bad.nc", debug, options, nullptr, warnings);
    Check(!analyzed && !error.ok() && warnings.empty(), "failed compilation discards partial warnings");
}
}

int main()
{
    Analyze("const int DEAD=1;int untouched;int written;written=2;void Never(){}",
        {"unused constant 'DEAD'", "unused global variable 'untouched'",
         "global variable 'written' is written but never read", "unused function 'Never'"});
    Analyze("const int BASE=2;const int LABEL=BASE+1;const int FIELD=4;struct Item{int value=FIELD;};"
        "int global=3;int Read(){return global;}void Used(int ignored){int local=0;Read();}"
        "Used(0);Item item;print(item.value);switch(3){case LABEL:break;default:break;}", {});
    Analyze("int shadowed;void Local(int argument){int shadowed=4;print(shadowed);int unused;}Local(0);",
        {"unused global variable 'shadowed'"});
    Analyze("int Value(){return 4;}fn()->int saved=Value;print(saved());"
        "int Nested(){return 3;}fn()->int factory=fn()->int{return Nested();};print(factory());", {});
    Analyze("int Recursive(int n){if(n==0)return 0;return Recursive(n-1);}"
        "export int Entry(){return 1;}export int external;public void Library(){}",
        {"unused function 'Recursive'", "unused function 'Library'"});
    Analyze("int Recursive(){fn()->int again=fn()->int{return Recursive();};return again();}",
        {"unused function 'Recursive'"});
    Analyze("int count;count+=1;int other;other++;list<int> items=[1];items[0]=2;", {});
    Analyze("int Hidden(){return 1;}print(\"Hidden\");", {"unused function 'Hidden'"});
    NeoCore::HostSchema schema;
    const auto object = schema.AddObject("Object");
    schema.AddGlobal("this", object);
    schema.AddEvent(object, "OnClick", schema.Types().FunctionOf(schema.Types().Void(), {schema.Types().Int()}));
    Analyze("int hits;void Click(int value){hits+=value;}this.AddEvent(\"OnClick\",Click);", {}, &schema);
    Images(false);
    Images(true);
    std::printf("NeoCore unused declarations: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
