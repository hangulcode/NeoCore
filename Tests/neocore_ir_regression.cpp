#include "NeoCoreCompiler.h"
#include "NeoCoreIR.h"
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
    if (!condition && ++failures <= 60) std::fprintf(stderr, "FAIL: %s\n", label.c_str());
}

struct Value
{
    RunStatus status = RunStatus::Failed;
    ValueType type = ValueType::None;
    std::int64_t integer = 0;
    double number = 0;
    bool boolean = false;
    Error error;
};

Value Invoke(IRuntime& runtime, InstanceHandle instance, const char* name)
{
    Value result;
    auto call = runtime.Call(instance, name);
    result.status = call.invoke();
    if (result.status != RunStatus::Completed) { result.error = call.error(); return result; }
    result.type = call.retType();
    if (result.type == ValueType::Int) result.integer = call.retInt();
    else if (result.type == ValueType::Float) result.number = call.retFloat();
    else if (result.type == ValueType::Bool) result.boolean = call.retBool();
    return result;
}

bool Same(const Value& a, const Value& b)
{
    if (a.status != b.status || a.status != RunStatus::Completed || a.type != b.type) return false;
    if (a.type == ValueType::Int) return a.integer == b.integer;
    if (a.type == ValueType::Bool) return a.boolean == b.boolean;
    if (a.type == ValueType::Float)
        return (std::isnan(a.number) && std::isnan(b.number)) ||
            std::memcmp(&a.number, &b.number, sizeof(double)) == 0;
    return a.type == ValueType::None;
}

CompileDesc Description(const NeoCore::StaticCompileResult& checked, bool debug)
{
    CompileDesc desc;
    desc.source = checked.loweredSource;
    desc.sourceName = "ir-regression.nc";
    desc.includeDebugInfo = debug;
    desc.coreIntrinsicPrefix = checked.backendIntrinsicPrefix;
    desc.coreTypedIR = &checked.typedIR;
    return desc;
}

ProgramHandle Compile(IRuntime& runtime, const NeoCore::StaticCompileResult& checked,
    bool debug, bool image, const std::string& label)
{
    CompileDesc desc = Description(checked, debug);
    Error error;
    ProgramHandle program;
    if (image)
    {
        std::vector<uint8_t> bytes;
        error = runtime.CompileToBytecode(desc, bytes);
        if (error.ok()) program = runtime.LoadProgram(bytes, &error);
    }
    else
    {
        const CompileResult compiled = runtime.Compile(desc);
        program = compiled.program;
        error = compiled.error;
    }
    Check(bool(program) && error.ok(), label + " compile/load: " + error.message);
    return program;
}

Value Run(IRuntime& runtime, const std::string& source, bool typed, bool debug,
    bool image, const std::string& label)
{
    NeoCore::StaticCompileResult checked;
    Check(NeoCore::CompileStaticSource(source, checked, typed), label + " frontend: " + checked.errorMessage);
    if (!checked.success) return Value();
    ProgramHandle program = Compile(runtime, checked, debug, image, label);
    if (!program) return Value();
    InstanceHandle instance = runtime.CreateInstance(program);
    Check(bool(instance), label + " instance");
    Value value;
    if (instance)
    {
        value = Invoke(runtime, instance, "Probe");
        runtime.DestroyInstance(instance);
    }
    runtime.DestroyProgram(program);
    return value;
}

std::size_t FindOp(const NeoCore::TypedIR& ir, NeoCore::IROp op)
{
    for (std::size_t i = 0; i < ir.nodes.size(); ++i)
        if (ir.nodes[i].op == op) return i;
    return ir.nodes.size();
}

void Structure()
{
    const char* source = "export float Probe(int first,float second){\nreturn (first+3)*second;\n}";
    NeoCore::StaticCompileResult checked;
    Check(NeoCore::CompileStaticSource(source, checked), "structured numeric frontend");
    if (!checked.success) return;
    const NeoCore::TypedIR& ir = checked.typedIR;
    Check(!ir.prefix.empty() && !ir.nodes.empty(), "numeric expressions retain structured IR");
    const std::size_t add = FindOp(ir, NeoCore::IROp::Add);
    const std::size_t multiply = FindOp(ir, NeoCore::IROp::Multiply);
    const std::size_t widen = FindOp(ir, NeoCore::IROp::ToFloat);
    Check(add < ir.nodes.size() && multiply < ir.nodes.size() && widen < ir.nodes.size(),
        "mixed expression retains add, explicit widening and multiply");
    if (add < ir.nodes.size())
    {
        const NeoCore::IRNode& node = ir.nodes[add];
        Check(node.type == NeoCore::IRValueType::Int && node.typeId == checked.types.Int(),
            "IR integer result carries checked type identity");
        Check(node.source.line == 2 && node.source.column > 0 && node.source.begin < node.source.end,
            "IR operation retains source span");
        Check(node.inputs.size() == 2, "IR binary operands are node references");
    }
    if (multiply < ir.nodes.size())
        Check(ir.nodes[multiply].type == NeoCore::IRValueType::Float &&
            ir.nodes[multiply].typeId == checked.types.Float(), "IR multiplication keeps binary64 result type");
    int symbols = 0;
    for (const auto& node : ir.nodes)
    {
        if (node.op == NeoCore::IROp::Symbol)
        {
            ++symbols;
            Check(!node.backendName.empty(), "resolved symbols carry backend storage names");
        }
        Check(node.op != NeoCore::IROp::LegacyExpression, "plain numeric expression does not retain expression text");
    }
    Check(symbols >= 2, "IR retains both checked parameter bindings");
    Check(checked.loweredSource.find(ir.prefix) != std::string::npos,
        "transitional statement skeleton references an IR root");
    const char* numericSuffixes[] = {"iadd", "fmul", "itof"};
    for (const char* suffix : numericSuffixes)
        Check(checked.loweredSource.find(checked.backendIntrinsicPrefix + suffix) == std::string::npos,
            std::string("migrated expression has no textual numeric intrinsic: ") + suffix);

    NeoCore::StaticCompileResult generic;
    Check(NeoCore::CompileStaticSource(source, generic, false), "generic control frontend");
    Check(generic.typedIR.nodes.empty(), "generic differential control does not consume typed IR");
}

// 바이트코드가 원래 식 문자열이 아니라 IR 상수에서 생성되는지 직접 확인한다.
void MutationAndLifetime(IRuntime& runtime, bool debug)
{
    for (bool image : {false, true})
    {
        ProgramHandle program;
        std::vector<uint8_t> bytes;
        {
            NeoCore::StaticCompileResult checked;
            Check(NeoCore::CompileStaticSource("export int Probe(){int seed=7;return seed+11;}", checked),
                "IR constant mutation frontend");
            if (!checked.success) continue;
            bool changed = false;
            for (auto& node : checked.typedIR.nodes)
                if (node.op == NeoCore::IROp::IntLiteral && node.integer == 11)
                { node.integer = 13; changed = true; }
            Check(changed, "numeric literal is retained as an IR value");
            if (!changed) continue;
            if (image)
            {
                const Error error = runtime.CompileToBytecode(Description(checked, debug), bytes);
                Check(error.ok(), "mutated IR image compiles: " + error.message);
            }
            else program = Compile(runtime, checked, debug, false, "mutated IR");
        }
        // checked와 IR을 파괴한 뒤 로드·실행해 컴파일 데이터 수명 의존을 막는다.
        if (image && !bytes.empty())
        {
            Error error;
            program = runtime.LoadProgram(bytes, &error);
            Check(bool(program) && error.ok(), "image loads after its IR is destroyed");
        }
        if (!program) continue;
        InstanceHandle instance = runtime.CreateInstance(program);
        Check(bool(instance), "mutated IR instance");
        if (instance)
        {
            const Value value = Invoke(runtime, instance, "Probe");
            Check(value.status == RunStatus::Completed && value.type == ValueType::Int && value.integer == 20,
                "IR mutation changes execution independently of original source text");
            runtime.DestroyInstance(instance);
        }
        runtime.DestroyProgram(program);
    }
}

void RejectIR(IRuntime& runtime, const NeoCore::StaticCompileResult& checked,
    bool debug, const std::string& label, bool missing = false)
{
    CompileDesc desc = Description(checked, debug);
    if (missing) desc.coreTypedIR = nullptr;
    const CompileResult direct = runtime.Compile(desc);
    Check(!direct.program && !direct.error.ok(), label + " direct compiler rejects malformed IR");
    if (direct.program) runtime.DestroyProgram(direct.program);
    std::vector<uint8_t> bytes;
    const Error error = runtime.CompileToBytecode(desc, bytes);
    Check(!error.ok() && bytes.empty(), label + " image compiler rejects malformed IR");
}

void InvalidMetadata(IRuntime& runtime, bool debug)
{
    NeoCore::StaticCompileResult valid;
    Check(NeoCore::CompileStaticSource("export int Probe(int a,int b){return a+b;}", valid),
        "malformed IR fixture frontend");
    if (!valid.success) return;
    const std::size_t add = FindOp(valid.typedIR, NeoCore::IROp::Add);
    Check(add < valid.typedIR.nodes.size(), "malformed IR fixture contains add");
    if (add == valid.typedIR.nodes.size()) return;
    RejectIR(runtime, valid, debug, "missing metadata", true);
    for (int change = 0; change < 5; ++change)
    {
        NeoCore::StaticCompileResult altered = valid;
        NeoCore::IRNode& node = altered.typedIR.nodes[add];
        if (change == 0) node.inputs[0] = altered.typedIR.nodes.size();
        else if (change == 1) node.inputs.clear();
        else if (change == 2) node.inputs[0] = add;
        else if (change == 3)
        {
            NeoCore::IRNode& input = altered.typedIR.nodes[node.inputs[0]];
            input.type = NeoCore::IRValueType::Bool;
            input.typeId = altered.types.Bool();
        }
        else node.op = static_cast<NeoCore::IROp>(255);
        RejectIR(runtime, altered, debug, "bad metadata variant " + std::to_string(change));
    }
}

void EvaluationOrder(IRuntime& runtime, bool debug)
{
    struct Case { const char* source; std::int64_t expected; const char* label; };
    const Case cases[] = {
        {"export int Probe(){int x=1;return x+((x=4)+2);}", 7,
            "outer symbol snapshot sees hidden assignment in IR"},
        {"export int Probe(){int x=1;return (x+2)+((x=9)+3);}", 15,
            "nested completed left expression precedes assignment"},
        {"export int Probe(){int x=1;return x+int(float(x=5)+0.5);}", 6,
            "conversion IR retains nested assignment effects"},
        {"int Pair(int a,int b){return a*10+b;}export int Probe(){int x=3;return Pair(x,(x=7)+2);}", 39,
            "argument snapshot sees assignment hidden behind IR root"},
        {"int x=1;int Replace(){x=8;return 2;}export int Probe(){return x+(Replace()*3);}", 7,
            "global snapshot precedes nested call"},
        {"int Bump(int& x){x=8;return 2;}export int Probe(){int x=3;return x+(Bump(x)*4);}", 11,
            "reference snapshot precedes nested call"},
        {"export int Probe(){int x=2;int total=x+3;{float x=1.5;total+=int(x*4.0);}return total+x;}", 13,
            "shadowed symbols keep distinct numeric types and storage"},
        {"export int Probe(){int x=1;int y=2;return (x+=3)+(y*=4);}", 12,
            "compound assignments execute once in left to right order"},
        {"int trace=0;int A(){trace=trace*10+1;return 3;}int B(){trace=trace*10+2;return 2;}"
            "export int Probe(){if(A()>B())trace=trace*10+3;return trace;}", 123,
            "reversed comparison retains source evaluation order"},
        {"int count=0;int Tick(){count+=1;return count;}export int Probe(){"
            "bool a=false&&(Tick()+1>0);bool b=true||(Tick()*2>0);return count;}", 0,
            "short circuit skips numeric IR roots"},
        {"export int Probe(){int i=0;int sum=0;while(i+1<6){i+=1;if(i==2)continue;sum+=i;}return sum;}", 13,
            "IR condition reevaluates on loop backedges"}
    };
    for (const auto& item : cases)
        for (bool image : {false, true})
        {
            const Value generic = Run(runtime, item.source, false, debug, image, std::string(item.label) + " generic");
            const Value direct = Run(runtime, item.source, true, debug, image, std::string(item.label) + " IR");
            Check(Same(generic, direct), std::string(item.label) + " matches generic execution");
            Check(direct.status == RunStatus::Completed && direct.type == ValueType::Int && direct.integer == item.expected,
                std::string(item.label) + " expected value");
        }
}

void ErrorOrder(IRuntime& runtime, bool debug)
{
    const char* sources[] = {
        "int calls=0;\nint F(){calls+=1;return 2;}\nexport int Probe(){\nreturn (1/0)+F();\n}\nexport int State(){return calls;}",
        "int calls=0;\nint F(){calls+=1;return 2;}\nexport int Probe(){int zero=0;\nreturn (1/zero)+F();\n}\nexport int State(){return calls;}",
        "int calls=0;\nint F(){calls+=1;return 2;}\nexport int Probe(){int zero=0;\nreturn 7 +\n((8/zero)+F());\n}\nexport int State(){return calls;}",
        "int calls=0;\nint F(){calls+=1;return 2;}\nexport int Probe(){\nreturn F()+(1/0);\n}\nexport int State(){return calls;}"
    };
    for (int index = 0; index < 4; ++index)
        for (bool image : {false, true})
        {
            const std::string label = "early IR error " + std::to_string(index);
            NeoCore::StaticCompileResult checked;
            Check(NeoCore::CompileStaticSource(sources[index], checked), label + " frontend");
            if (!checked.success) continue;
            ProgramHandle program = Compile(runtime, checked, debug, image, label);
            if (!program) continue;
            InstanceHandle instance = runtime.CreateInstance(program);
            Check(bool(instance), label + " instance");
            if (instance)
            {
                const Value result = Invoke(runtime, instance, "Probe");
                Check(result.status == RunStatus::Failed && result.error.message.find("integer division by zero") == 0,
                    label + " reports division failure");
                const Value state = Invoke(runtime, instance, "State");
                Check(state.status == RunStatus::Completed && state.type == ValueType::Int && state.integer == (index == 3 ? 1 : 0),
                    label + " preserves effects completed before the failure and permits recovery");
                if (debug)
                {
                    Check(result.error.line == static_cast<uint32_t>(index == 2 ? 5 : 4),
                        label + " reports the failing inner operator source line: " + result.error.message);
                    Check(result.error.message.find("Probe") != std::string::npos,
                        label + " reports the source function frame");
                    if (!image) Check(result.error.sourceName == "ir-regression.nc", label + " retains source filename");
                }
                runtime.DestroyInstance(instance);
            }
            runtime.DestroyProgram(program);
        }
}
}

int main()
{
    IRuntime* runtime = CreateRuntime();
    Check(runtime != nullptr, "runtime creation");
    if (!runtime) return 1;
    runtime->FreezeBindings();
    Structure();
    for (bool debug : {false, true})
    {
        MutationAndLifetime(*runtime, debug);
        InvalidMetadata(*runtime, debug);
        EvaluationOrder(*runtime, debug);
        ErrorOrder(*runtime, debug);
    }
    DestroyRuntime(runtime);
    std::printf("NeoCore IR regression: %d/%d passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
