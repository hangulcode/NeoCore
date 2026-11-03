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
    std::string text;
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
    else if (result.type == ValueType::String) result.text = call.retString().str();
    return result;
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

Value Run(IRuntime& runtime, const std::string& source, bool debug,
    bool image, const std::string& label)
{
    NeoCore::StaticCompileResult checked;
    Check(NeoCore::CompileStaticSource(source, checked), label + " frontend: " + checked.errorMessage);
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
    Check(checked.loweredSource.find(ir.returnPrefix) != std::string::npos,
        "transitional statement skeleton references an IR root");
    const char* numericSuffixes[] = {"iadd", "fmul", "itof"};
    for (const char* suffix : numericSuffixes)
        Check(checked.loweredSource.find(checked.backendIntrinsicPrefix + suffix) == std::string::npos,
            std::string("migrated expression has no textual numeric intrinsic: ") + suffix);


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

void InternalNameBoundary(IRuntime& runtime, bool debug)
{
    const char* sources[] = {"var $neocore_user=1;", "fun $neocore_test(){}"};
    for (const char* source : sources)
        for (bool prefixOnly : {false, true})
        {
            CompileDesc desc;
            desc.source = source;
            desc.sourceName = "internal_names.ns";
            desc.includeDebugInfo = debug;
            if (prefixOnly) desc.coreIntrinsicPrefix = "$neocore_op_";
            const std::string label = std::string(source) + (prefixOnly ? " with intrinsic prefix only" : " without IR");
            const CompileResult direct = runtime.Compile(desc);
            Check(!direct.program && !direct.error.ok(), label + " rejects private identifiers");
            if (direct.program) runtime.DestroyProgram(direct.program);
            std::vector<uint8_t> bytes;
            const Error error = runtime.CompileToBytecode(desc, bytes);
            Check(!error.ok() && bytes.empty(), label + " cannot produce an image");
        }

    class PrivateModuleLoader : public ILoader
    {
    public:
        std::string source;
        unsigned loads = 0;
        bool Load(StringView path, std::vector<uint8_t>& output) override
        {
            if (path.str() != "modules/private_names.ns") return false;
            ++loads;
            output.assign(source.begin(), source.end());
            return true;
        }
        StringView LibPath() const override { return "modules/"; }
    };
    NeoCore::StaticCompileResult checked;
    Check(NeoCore::CompileStaticSource("export int Probe(){int value=1;return value;}", checked),
        "private identifier import fixture frontend");
    if (!checked.success) return;
    const std::string root = "import private_names;\n" + checked.loweredSource;
    const char* modules[] = {"public var value=1;", "var $neocore_user=1;", "fun $neocore_test(){}"};
    for (unsigned index = 0; index < sizeof(modules) / sizeof(modules[0]); ++index)
    {
        PrivateModuleLoader loader;
        loader.source = modules[index];
        RuntimeDesc settings;
        settings.loader = &loader;
        IRuntime* imported = CreateRuntime(settings);
        Check(imported != nullptr, "private identifier import runtime");
        if (!imported) continue;
        imported->FreezeBindings();
        CompileDesc desc = Description(checked, debug);
        desc.source = root;
        const CompileResult result = imported->Compile(desc);
        Check(loader.loads > 0, "private identifier fixture actually loads its imported source");
        if (index == 0)
            Check(result.program && result.error.ok(), "checked root may import ordinary backend identifiers: " + result.error.message);
        else
            Check(!result.program && !result.error.ok(), "imported source does not inherit private identifier privileges");
        if (result.program) imported->DestroyProgram(result.program);
        DestroyRuntime(imported);
    }
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
    const std::size_t symbol = FindOp(valid.typedIR, NeoCore::IROp::Symbol);
    Check(symbol < valid.typedIR.nodes.size() && !valid.typedIR.functions.empty(), "internal identifier metadata fixture");
    if (symbol == valid.typedIR.nodes.size() || valid.typedIR.functions.empty()) return;
    const char* invalidNames[] = {"$other", "$neocore_bad/name", "$neocore_"};
    for (const char* name : invalidNames)
        for (int field = 0; field < 4; ++field)
        {
            NeoCore::StaticCompileResult altered = valid;
            if (field == 0) altered.typedIR.prefix = name;
            else if (field == 1) altered.typedIR.returnPrefix = name;
            else if (field == 2) altered.typedIR.nodes[symbol].backendName = name;
            else altered.typedIR.functions[0].backendName = name;
            RejectIR(runtime, altered, debug, std::string("invalid private metadata name ") + name + " field " + std::to_string(field));
        }
}

void MapMetadata(IRuntime& runtime, bool debug)
{
    NeoCore::StaticCompileResult valid;
    Check(NeoCore::CompileStaticSource("export int Probe(map<string,int> m){int x=1;x+=m[\"key\"];return x;}", valid),
        "numeric map IR frontend");
    if (!valid.success) return;
    const std::size_t read = FindOp(valid.typedIR, NeoCore::IROp::MapRead);
    const std::size_t key = FindOp(valid.typedIR, NeoCore::IROp::StringLiteral);
    Check(read < valid.typedIR.nodes.size() && key < valid.typedIR.nodes.size(), "map and constant key retain semantic IR");
    if (read == valid.typedIR.nodes.size() || key == valid.typedIR.nodes.size()) return;
    Check(valid.typedIR.nodes[key].text == "key", "literal key has no unknown legacy-expression effects");
    for (int change = 0; change < 4; ++change)
    {
        NeoCore::StaticCompileResult altered = valid;
        NeoCore::IRNode& node = altered.typedIR.nodes[read];
        if (change == 0) node.inputs.pop_back();
        else if (change == 1) node.inputs[0] = key;
        else if (change == 2) node.inputs[1] = node.inputs[0];
        else { node.type = NeoCore::IRValueType::Float; node.typeId = altered.types.Float(); }
        RejectIR(runtime, altered, debug, "map read type contract " + std::to_string(change));
    }
}

void StorageStructure()
{
    NeoCore::StaticCompileResult checked;
    const char* source = "struct S{int x;};export int Probe(){int value=1;S object=S(2);int direct=3;"
        "direct=7;value+=4;int old=value++;++object.x;bool flag=false;flag=true;string text=\"before\";text=\"after\";"
        "return direct+old+value+object.x;}";
    Check(NeoCore::CompileStaticSource(source, checked), "storage metadata frontend: " + checked.errorMessage);
    if (!checked.success) return;
    bool variable = false, field = false, boolean = false, string = false, read = false;
    for (const auto& node : checked.typedIR.nodes)
    {
        if (node.op == NeoCore::IROp::Assign)
        {
            variable = variable || node.storage.kind == NeoCore::IRStorageKind::Variable;
            boolean = boolean || node.type == NeoCore::IRValueType::Bool;
            string = string || node.type == NeoCore::IRValueType::String;
        }
        if (node.op == NeoCore::IROp::PreIncrement)
            field = field || (node.storage.kind == NeoCore::IRStorageKind::Variable && node.storage.fields.size() == 1);
        if (node.op == NeoCore::IROp::LoadStorage)
            read = read || (node.storage.kind == NeoCore::IRStorageKind::Variable && !node.storage.fields.empty());
        if (node.storage.kind != NeoCore::IRStorageKind::Invalid)
        {
            Check(!node.storage.backendName.empty(), "storage node retains a resolved root binding");
            Check(node.legacySource.empty(), "storage node does not reparse its source expression");
            Check(node.typeId != NeoCore::InvalidType && node.source.line > 0,
                "storage node retains checked type and source position");
        }
    }
    Check(variable && field && read, "IR distinguishes direct storage and struct field paths");
    Check(boolean && string, "boolean and string assignments use storage IR");
}

void InvalidStorageMetadata(IRuntime& runtime, bool debug)
{
    NeoCore::StaticCompileResult valid;
    Check(NeoCore::CompileStaticSource("export int Probe(){int value=2;value+=3;return value;}", valid),
        "malformed storage fixture frontend");
    if (!valid.success) return;
    const std::size_t write = FindOp(valid.typedIR, NeoCore::IROp::AddAssign);
    Check(write < valid.typedIR.nodes.size(), "malformed storage fixture contains typed compound assignment");
    if (write == valid.typedIR.nodes.size()) return;
    for (int change = 0; change < 5; ++change)
    {
        NeoCore::StaticCompileResult altered = valid;
        NeoCore::IRNode& node = altered.typedIR.nodes[write];
        if (change == 0) node.storage.kind = static_cast<NeoCore::IRStorageKind>(255);
        else if (change == 1) node.storage.backendName.clear();
        else if (change == 2) node.inputs.clear();
        else if (change == 3) node.storage.kind = NeoCore::IRStorageKind::Invalid;
        else
        {
            NeoCore::IRNode& input = altered.typedIR.nodes[node.inputs[0]];
            input.op = NeoCore::IROp::BoolLiteral;
            input.type = NeoCore::IRValueType::Bool;
            input.typeId = altered.types.Bool();
        }
        RejectIR(runtime, altered, debug, "bad storage metadata variant " + std::to_string(change));
    }
    for (const char* name : {"$other", "$neocore_bad/name"})
    {
        NeoCore::StaticCompileResult altered = valid;
        altered.typedIR.nodes[write].storage.backendName = name;
        RejectIR(runtime, altered, debug, std::string("invalid private storage name ") + name);
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
        {"int x=3;int Bump(){x=8;return 2;}export int Probe(){return x+(Bump()*4);}", 11,
            "global value snapshot precedes nested mutation"},
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
            const Value direct = Run(runtime, item.source, debug, image, std::string(item.label) + " IR");
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

// 같은 저장소를 여러 식이 바꾸더라도 각 식의 결과와 우변 평가 순서는 보존한다.
void ScalarWrites(IRuntime& runtime, bool debug)
{
    struct IntCase { const char* source; std::int64_t expected; const char* label; };
    const IntCase integers[] = {
        {"export int Probe(){int x=9;int total=(x+=3);total+=(x-=2);total+=(x*=4);"
            "total+=(x/=5);total+=(x%=3);return total*10+x;}", 722, "integer compound arithmetic"},
        {"export int Probe(){int x=13;int total=(x&=11);total+=(x|=16);total+=(x^=3);"
            "total+=(x<<=2);total+=(x>>=3);return total+x;}", 190, "integer compound bit operations"},
        {"int Rewrite(int x){return (x=7)+(x+=2);}export int Probe(){int x=3;return Rewrite(x)*10+x;}",
            163, "value parameter updates remain local to the callee"},
        {"export int Probe(){int x=1;int y=9;int a=(x=y);y=2;x+=3;return x*100+y*10+a;}",
            1229, "assignment result is independent of later source and target writes"},
        {"int g=1;int Replace(){g=10;return 3;}export int Probe(){int result=(g+=Replace());return result*100+g;}",
            1313, "compound target is read after RHS changes the global"},
        {"export int Probe(){int a=1;int b=2;int c=3;int result=(a=b=c=7);return result*1000+a*100+b*10+c;}",
            7777, "chained assignments return the stored value"},
        {"export int Probe(){int x=1;int result=(x=2)+(x=3)+(x=4);return result*10+x;}",
            94, "assignment results survive later writes to the same variable"},
        {"export int Probe(){int x=3;int result=x++ + ++x + x-- + --x;return result*10+x;}",
            163, "prefix and postfix results snapshot their respective values"},
        {"int Pair(int a,int b){return a*10+b;}export int Probe(){int x=3;int result=Pair(x++,++x);return result*10+x;}",
            355, "increment results in call arguments preserve left to right order"},
        {"int Mutate(int x){return x++ + ++x;}export int Probe(){int x=3;int result=Mutate(x);return result*10+x;}",
            83, "parameter increments preserve expression results and caller value"},
        {"struct S{int x;};S g=S(2);int Replace(){g=S(7);return 3;}"
            "export int Probe(){int result=(g.x+=Replace());return result*100+g.x;}",
            1010, "field target is resolved after whole struct replacement"},
        {"struct I{int x;};struct O{I inner;};O g=O(I(2));int Replace(){g=O(I(7));return 3;}"
            "export int Probe(){int old=g.inner.x++;int result=(g.inner.x+=Replace());return old*1000+result*10+g.inner.x;}",
            2110, "nested field target and postfix value survive replacement"},
        {"export int Probe(){int x=9;bool a=false&&((x/=0)>0);bool b=true||((x<<=64)>0);return x;}",
            9, "short circuit skips failing writes"},
        {"export int Probe(){bool a=false;bool b=true;bool value=(a=b);b=false;"
            "if(value&&a&&!b)return 1;return 0;}", 1, "boolean assignment result"},
        {"export int Probe(){string a=\"a\";string b=\"b\";string value=(a=b);b=\"c\";a=\"d\";"
            "if(value==\"b\"&&a==\"d\"&&b==\"c\")return 1;return 0;}", 1, "string assignment retains its result ownership"},
        {"int Pair(string a,string b){if(a==\"first\"&&b==\"second\")return 1;return 0;}"
            "export int Probe(){string text=\"old\";return Pair(text=\"first\",text=\"second\");}",
            1, "string assignment argument snapshots"},
        {"export int Probe(){int x=9223372036854775807;int a=x++;int b=--x;"
            "if(a==9223372036854775807&&b==a&&x==a)return 1;return 0;}",
            1, "integer increments preserve wrapping behavior"},
        {"export int Probe(){Vector3 value=Vector3(16777216.0,0.0,0.0);value.x+=-16777215.5;if(value.x==0.5)return 1;return 0;}",
            1, "local vector component compound computes in binary64 before float32 storage"},
        {"struct S{Vector3 value;};export int Probe(){S object=S(Vector3(16777216.0,0.0,0.0));object.value.x+=-16777215.5;if(object.value.x==0.5)return 1;return 0;}",
            1, "struct vector component compound computes in binary64 before float32 storage"},
        {"export int Probe(){array<Vector3> values=array<Vector3>(1,Vector3(16777216.0,0.0,0.0));values[0].x+=-16777215.5;if(values[0].x==0.5)return 1;return 0;}",
            1, "array vector component compound computes in binary64 before float32 storage"}
    };
    for (const auto& item : integers)
        for (bool image : {false, true})
        {
            const Value direct = Run(runtime, item.source, debug, image, std::string(item.label) + " IR");
            Check(direct.status == RunStatus::Completed && direct.type == ValueType::Int && direct.integer == item.expected,
                std::string(item.label) + " expected value");
        }

    struct FloatCase { const char* source; double expected; const char* label; };
    const FloatCase floats[] = {
        {"export float Probe(){float x=2;float total=(x=7);total+=(x+=0.5);total+=(x-=2);"
            "total+=(x*=3);total+=(x/=2);return total+x;}", 53.0, "floating compound arithmetic widens integer RHS"},
        {"export float Probe(){float x=1.5;float result=x++ + ++x + x-- + --x;return result*10+x;}",
            101.5, "floating prefix and postfix values"},
        {"export float Probe(){float x=0;int source=9007199254740993;return (x=source);}",
            9007199254740992.0, "assignment returns the converted binary64 value"},
        {"struct S{float x;};export float Probe(){S value=S(0);int source=9007199254740993;return (value.x=source);}",
            9007199254740992.0, "field assignment returns the converted binary64 value"},
        {"export float Probe(){float x=1;return (x=-0.0);}",
            -0.0, "assignment retains the sign of zero"}
    };
    for (const auto& item : floats)
        for (bool image : {false, true})
        {
            const Value direct = Run(runtime, item.source, debug, image, std::string(item.label) + " IR");
            Check(direct.status == RunStatus::Completed && direct.type == ValueType::Float &&
                std::memcmp(&direct.number, &item.expected, sizeof(double)) == 0, std::string(item.label) + " expected value");
        }
}

// 쓰기 자체가 실패해도 완료된 우변 효과는 남고, 계산 전 대상 값은 덮어쓰지 않는다.
void ScalarWriteErrors(IRuntime& runtime, bool debug)
{
    struct Case { const char* source; const char* error; std::int64_t expected; };
    const Case cases[] = {
        {"int target=9;int calls=0;\nint RHS(){calls+=1;return 0;}\nexport int Probe(){\nreturn target/=RHS();\n}"
            "\nexport int State(){return target*10+calls;}", "integer division by zero", 91},
        {"int target=9;int calls=0;\nint RHS(){calls+=1;target=7;return 0;}\nexport int Probe(){\nreturn target%=RHS();\n}"
            "\nexport int State(){return target*10+calls;}", "integer division by zero", 71},
        {"int target=-9223372036854775808;int calls=0;\nint RHS(){calls+=1;return -1;}\nexport int Probe(){\nreturn target/=RHS();\n}"
            "\nexport int State(){if(target==-9223372036854775808)return 10+calls;return -1;}", "integer division overflow", 11},
        {"struct S{int x;};S target=S(9);int calls=0;\nint RHS(){calls+=1;return 64;}\nexport int Probe(){\nreturn target.x<<=RHS();\n}"
            "\nexport int State(){return target.x*10+calls;}", "shift count must be between 0 and 63", 91}
    };
    for (std::size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index)
            for (bool image : {false, true})
            {
                const std::string label = "scalar write failure " + std::to_string(index) + " IR";
                NeoCore::StaticCompileResult checked;
                Check(NeoCore::CompileStaticSource(cases[index].source, checked), label + " frontend");
                if (!checked.success) continue;
                ProgramHandle program = Compile(runtime, checked, debug, image, label);
                if (!program) continue;
                InstanceHandle instance = runtime.CreateInstance(program);
                Check(bool(instance), label + " instance");
                if (instance)
                {
                    const Value value = Invoke(runtime, instance, "Probe");
                    Check(value.status == RunStatus::Failed && value.error.message.find(cases[index].error) == 0,
                        label + " expected runtime error: " + value.error.message);
                    const Value state = Invoke(runtime, instance, "State");
                    Check(state.status == RunStatus::Completed && state.type == ValueType::Int && state.integer == cases[index].expected,
                        label + " retains completed RHS effects and allows the next call");
                    if (debug)
                    {
                        Check(value.error.line == 4, label + " reports source write line: " + value.error.message);
                        Check(value.error.message.find("Probe") != std::string::npos, label + " retains the caller frame");
                        if (!image) Check(value.error.sourceName == "ir-regression.nc", label + " retains source filename");
                    }
                    runtime.DestroyInstance(instance);
                }
                runtime.DestroyProgram(program);
            }
}

// 문장 위치의 대입·증감은 결과를 버리므로 임시 슬롯 없이 변수에 바로 저장한다.
// 결과를 소비하는 대입, 실패하는 쓰기, 구조체 필드 경로는 기존 규칙을 그대로 지켜야 한다.
void DiscardedWrites(IRuntime& runtime, bool debug)
{
    struct IntCase { const char* source; std::int64_t expected; const char* label; };
    const IntCase cases[] = {
        {"export int Probe(){int sum=0;int bias=1;for(int i in 0,10,1){sum+=i+bias;}return sum;}",
            55, "discarded compound assignment accumulates"},
        {"export int Probe(){int sum=0;for(int i in 0,10,1){sum=sum+i;}return sum;}",
            45, "discarded assignment reading its own target"},
        {"export int Probe(){int t=0;int u=5;t=u;u=9;return t*10+u;}",
            59, "discarded copy from a variable does not alias it"},
        {"export int Probe(){int v=6;v=v*2;v+=v;v-=4;v*=2;v/=5;v%=5;return v;}",
            3, "discarded compound chain"},
        {"export int Probe(){int c=0;c++;++c;c--;c++;return c;}",
            2, "discarded increments"},
        {"int a=0;int b=0;export int Probe(){int i=3;a=(b=i+1);return a*10+b;}",
            44, "nested assignment stores both targets"},
        {"int x=0;export int Probe(){int r=(x=1)+(x=2);return r*10+x;}",
            32, "assignment results inside an expression are preserved"},
        {"export int Probe(){int x=1;int y=9;int a=(x=y);y=2;x+=3;return x*100+y*10+a;}",
            1229, "consumed assignment result still independent of later writes"},
        {"int g=0;export int Probe(){int local=4;g=local;local=1;g+=local;return g*10+local;}",
            51, "discarded writes to a global"},
        {"struct S{int x;};export int Probe(){S s=S(1);s.x+=5;s.x=s.x*2;return s.x;}",
            12, "discarded struct field writes keep the field path"},
        {"export int Probe(){float f=1.5;f*=2.0;f+=f;f-=1;int c=0;c++;return toint(f)*10+c;}",
            51, "discarded float compound assignment"},
        {"export int Probe(){int x=9;x=x;x+=0;return x;}",
            9, "self assignment"},
        {"int Inc(int v){return v+1;}export int Probe(){int x=1;x=Inc(x);x+=Inc(x);return x;}",
            5, "discarded assignment from call results"}
    };
    for (const auto& item : cases)
        for (bool image : {false, true})
        {
            const Value direct = Run(runtime, item.source, debug, image, std::string(item.label) + " IR");
            Check(direct.status == RunStatus::Completed && direct.type == ValueType::Int && direct.integer == item.expected,
                std::string(item.label) + " expected value");
        }

    // 실패한 쓰기는 변수를 덮어쓰지 않는다. 임시 슬롯이 없으므로 핸들러가 저장 전에 검사해야 한다.
    struct ErrorCase { const char* source; const char* error; std::int64_t expected; };
    const ErrorCase errors[] = {
        {"int target=9;export int Probe(){\nint z=0;\ntarget/=z;\nreturn 0;}\nexport int State(){return target;}",
            "integer division by zero", 9},
        {"int target=9;export int Probe(){\nint z=0;\ntarget%=z;\nreturn 0;}\nexport int State(){return target;}",
            "integer division by zero", 9},
        {"int target=9;export int Probe(){\nint n=64;\ntarget<<=n;\nreturn 0;}\nexport int State(){return target;}",
            "shift count must be between 0 and 63", 9},
        {"int target=-9223372036854775808;export int Probe(){\nint m=-1;\ntarget/=m;\nreturn 0;}"
            "\nexport int State(){if(target==-9223372036854775808)return 1;return 0;}", "integer division overflow", 1},
        {"int target=9;int Zero(){return 0;}export int Probe(){\nint z=0;\ntarget=target/Zero();\nreturn 0;}\nexport int State(){return target;}",
            "integer division by zero", 9}
    };
    for (std::size_t index = 0; index < sizeof(errors) / sizeof(errors[0]); ++index)
        for (bool image : {false, true})
        {
            const std::string label = "discarded write failure " + std::to_string(index) + " IR";
            NeoCore::StaticCompileResult checked;
            Check(NeoCore::CompileStaticSource(errors[index].source, checked), label + " frontend");
            if (!checked.success) continue;
            ProgramHandle program = Compile(runtime, checked, debug, image, label);
            if (!program) continue;
            InstanceHandle instance = runtime.CreateInstance(program);
            Check(bool(instance), label + " instance");
            if (instance)
            {
                const Value value = Invoke(runtime, instance, "Probe");
                Check(value.status == RunStatus::Failed && value.error.message.find(errors[index].error) == 0,
                    label + " expected runtime error: " + value.error.message);
                const Value state = Invoke(runtime, instance, "State");
                Check(state.status == RunStatus::Completed && state.type == ValueType::Int && state.integer == errors[index].expected,
                    label + " leaves the target unchanged");
                if (debug) Check(value.error.line == 3, label + " reports source write line: " + value.error.message);
                runtime.DestroyInstance(instance);
            }
            runtime.DestroyProgram(program);
        }
}

// 결과 폐기 표시는 문장 루트의 대입·증감에만 붙고, 식 안의 대입에는 붙지 않는다.
void DiscardStructure()
{
    const char* source = "int a=0;int b=0;export int Probe(){int i=1;a=(b=i+1);i+=2;i++;int r=(a=3)+i;return r;}";
    NeoCore::StaticCompileResult checked;
    Check(NeoCore::CompileStaticSource(source, checked), "discard metadata frontend: " + checked.errorMessage);
    if (!checked.success) return;
    int discarded = 0, consumed = 0;
    for (const auto& node : checked.typedIR.nodes)
    {
        const bool mutation = node.op >= NeoCore::IROp::Assign && node.op <= NeoCore::IROp::PostDecrement;
        if (!mutation) { Check(!node.discardResult, "non-mutation nodes carry no discard metadata"); continue; }
        if (node.discardResult) ++discarded; else ++consumed;
    }
    Check(discarded == 3, "statement-level assignment and increments are marked discarded: " + std::to_string(discarded));
    Check(consumed == 2, "nested and expression assignments keep their results: " + std::to_string(consumed));
}

// 명령 삭제만이 아니라 실제 프레임의 임시 슬롯도 줄어드는지 확인한다.
void DirectDestinationStructure(IRuntime& runtime, bool debug)
{
    NeoCore::StaticCompileResult checked;
    Check(NeoCore::CompileStaticSource(
        "int Twice(int value){return value+value;}"
        "export int Arithmetic(int value){int sum=1;sum+=value+2;return sum;}"
        "export int Postfix(int value){value++;return value;}"
        "export int Negate(int value){value=-value;return value;}"
        "export int Convert(float value){int result=0;result=int(value);return result;}"
        "export int Call(int value){value=Twice(value);return value;}"
        "export void Store(list<int> target,int index,int value){target[index]=value;}",checked),
        "direct destination shape frontend: "+checked.errorMessage);
    if(!checked.success) return;
    std::vector<uint8_t> bytes;
    const Error compiled=runtime.CompileToBytecode(Description(checked,debug),bytes);
    Check(compiled.ok(),"direct destination shape image: "+compiled.message);
    if(!compiled.ok()) return;
    std::string message;
    CNeoVMProgram* program=CNeoVMProgram::Create(bytes.data(),static_cast<int>(bytes.size()),&message);
    Check(program!=nullptr,"direct destination shape load: "+message);
    if(!program) return;
    const struct Case { const char* name; int temps; } cases[]={
        {"Arithmetic",1},{"Postfix",0},{"Negate",0},{"Convert",0},{"Call",debug?1:0},{"Store",0}
    };
    for(const auto& test:cases)
    {
        const int function=program->FindFunction(test.name);
        Check(function>=0,"direct destination function exists");
        if(function<0) continue;
        const SFunctionTable& frame=program->functions[function];
        Check(frame._localTempMax==test.temps,std::string(test.name)+" only allocates live temporaries: "+std::to_string(frame._localTempMax));
        int end=program->GetCodeSize();
        for(const auto& other:program->functions)
            if(other._codePtr>frame._codePtr && other._codePtr<end) end=other._codePtr;
        for(int offset=frame._codePtr;offset<end;offset+=sizeof(SVMOperation))
        {
            const auto& operation=program->code[static_cast<std::size_t>(offset)/sizeof(SVMOperation)];
            if(std::strcmp(test.name,"Call")==0 && (operation.op==NOP_CALL || operation.op==NOP_CALL_L))
                Check(debug?operation.n3!=1:operation.n3==1,"script return uses the argument destination only outside debug mode");
            if(std::strcmp(test.name,"Store")==0)
                Check(operation.op!=NOP_MOV && operation.op!=NOP_MOV_L,"pure list store has no snapshot MOV");
        }
    }
    program->Release();
}

void CallStructure()
{
    const char* source = "struct Box{int x;};\nBox Copy(Box value){return value;}\n"
        "string Optional(string value){return value;}\nvoid Empty(){}\n"
        "export float Probe(){int x=1;Box box=Copy(Box(2));Touch(x,box.x);Empty();string n=Optional(null);return Sum(x,2);}\n"
        "void Touch(int left,int right){left+=right;return;}\nfloat Sum(float left,float right){return left+right;}";
    NeoCore::StaticCompileResult checked;
    Check(NeoCore::CompileStaticSource(source, checked), "call metadata frontend: " + checked.errorMessage);
    if (!checked.success) return;
    const NeoCore::TypedIR& ir = checked.typedIR;
    Check(!ir.returnPrefix.empty() && checked.loweredSource.find(ir.returnPrefix) != std::string::npos,
        "return statements reference structured return metadata");
    int calls = 0, copies = 0, nullable = 0;
    for (const auto& node : ir.nodes)
    {
        if (node.op == NeoCore::IROp::ScriptCall)
        {
            ++calls;
            Check(node.legacySource.empty(), "script call has no reparsed source expression");
            Check(node.functionIndex < ir.functions.size(), "call resolves a checked function signature");
            if (node.functionIndex >= ir.functions.size()) continue;
            const NeoCore::IRFunction& function = ir.functions[node.functionIndex];
            Check(!function.backendName.empty() && node.typeId == function.returnType,
                "call retains resolved callee and exact result type");
            Check(node.inputs.size() == function.parameterTypes.size(), "call retains every formal argument");
            for (std::size_t argument = 0; argument < node.inputs.size() && argument < function.parameterTypes.size(); ++argument)
                Check(node.inputs[argument] < ir.nodes.size() &&
                    ir.nodes[node.inputs[argument]].typeId == function.parameterTypes[argument],
                    "call argument conversion is explicit before transfer");
        }
        else if (node.op == NeoCore::IROp::NullableConvert)
        {
            ++nullable;
            Check(ir.types.AllowsNull(node.typeId) && node.inputs.size() == 1,
                "null transfer into a reference type is represented explicitly");
        }
        else if (node.op == NeoCore::IROp::CopyStruct)
        {
            ++copies;
            Check(node.type == NeoCore::IRValueType::Value && ir.types.Get(node.typeId).kind == NeoCore::TypeKind::Struct &&
                node.inputs.size() == 1 && !node.backendName.empty(), "struct value transfer keeps its copy helper");
        }
    }
    Check(calls == 5 && copies >= 2 && nullable >= 1,
        "calls retain scalar widening, struct copies and null transfers");
    bool voidReturn = false, valueReturn = false;
    for (const auto& statement : ir.returns)
    {
        Check(statement.functionIndex < ir.functions.size(), "return resolves its owning function");
        if (statement.functionIndex >= ir.functions.size()) continue;
        const NeoCore::IRFunction& function = ir.functions[statement.functionIndex];
        Check(statement.source.line > 0, "return retains its source position");
        if (function.returnType == ir.types.Void())
        {
            voidReturn = true;
            Check(statement.valueIndex == static_cast<std::size_t>(-1), "void return carries no value root");
        }
        else
        {
            valueReturn = true;
            Check(statement.valueIndex < ir.nodes.size() && ir.nodes[statement.valueIndex].typeId == function.returnType,
                "return transfers the exact declared result type");
        }
    }
    Check(voidReturn && valueReturn, "IR retains void and value returns");
}

void InvalidCallMetadata(IRuntime& runtime, bool debug)
{
    NeoCore::StaticCompileResult valid;
    Check(NeoCore::CompileStaticSource("int Read(int first,int value){return first+value;}"
        "export int Probe(){int x=2;return Read(x,3);}", valid), "malformed call fixture frontend");
    if (!valid.success) return;
    const std::size_t call = FindOp(valid.typedIR, NeoCore::IROp::ScriptCall);
    Check(call < valid.typedIR.nodes.size() && !valid.typedIR.returns.empty(),
        "malformed fixture contains a call and return");
    if (call == valid.typedIR.nodes.size() || valid.typedIR.returns.empty()) return;
    for (int change = 0; change < 9; ++change)
    {
        NeoCore::StaticCompileResult altered = valid;
        NeoCore::TypedIR& ir = altered.typedIR;
        NeoCore::IRNode& node = ir.nodes[call];
        if (change == 0) node.functionIndex = ir.functions.size();
        else if (change == 1) node.inputs.clear();
        else if (change == 2) ir.functions[node.functionIndex].parameterTypes[0] = ir.types.Bool();
        else if (change == 3) ir.functions[node.functionIndex].backendName.clear();
        else if (change == 4) { node.type = NeoCore::IRValueType::Float; node.typeId = ir.types.Float(); }
        else if (change == 5) ir.returns[0].functionIndex = ir.functions.size();
        else if (change == 6) ir.returns[0].valueIndex = ir.nodes.size();
        else if (change == 7) ir.returns[0].valueIndex = static_cast<std::size_t>(-1);
        else
        {
            NeoCore::IRNode wrong;
            wrong.op = NeoCore::IROp::BoolLiteral;
            wrong.type = NeoCore::IRValueType::Bool;
            wrong.typeId = ir.types.Bool();
            ir.returns[0].valueIndex = ir.nodes.size();
            ir.nodes.push_back(wrong);
        }
        RejectIR(runtime, altered, debug, "bad call metadata variant " + std::to_string(change));
    }
}

void RejectReferences()
{
    const char* sources[] = {
        "export int Probe(){int x=1;int& alias=x;return alias;}",
        "export float Probe(){float x=1;float & alias=x;return alias;}",
        "export bool Probe(){bool x=false;bool& alias=x;return alias;}",
        "export string Probe(){string x=\"x\";string& alias=x;return alias;}",
        "export void Probe(){string x=null;string& alias=x;}",
        "void Change(int& value){value=2;}export void Probe(){int value=1;Change(value);}",
        "int value=1;int& Read(){return value;}",
        "struct S{int value;};export void Probe(){S value=S(1);S& alias=value;}",
        "struct S{int value;};export void Probe(){S value=S(1);int& alias=value.value;}",
        "struct S{int value;};void Change(S& value){}",
        "struct S{int value;};S value=S(1);S& Read(){return value;}",
        "export void Probe(){Vector3 value=Vector3();Vector3& alias=value;}",
        "void Change(Vector3& value){}",
        "export void Probe(){array<int> value=array<int>();array<int>& alias=value;}",
        "void Change(list<int>& value){}",
        "void Change(map<string,int>& value){}"
    };
    for (std::size_t index = 0; index < sizeof(sources) / sizeof(sources[0]); ++index)
        {
            NeoCore::StaticCompileResult checked;
            Check(!NeoCore::CompileStaticSource(sources[index], checked) && !checked.success &&
                !checked.errorMessage.empty() && checked.errorLine > 0,
                "reference syntax is rejected " + std::to_string(index));
        }
}

// 호출 경계마다 인자 순서와 값 소유권을 확인하고, 이미지에서도 같은 결과를 요구한다.
void CallTransfers(IRuntime& runtime, bool debug)
{
    struct Case { const char* source; std::int64_t expected; const char* label; };
    const Case cases[] = {
        {"int Fib(int n){if(n<2)return n;return Fib(n-1)+Fib(n-2);}export int Probe(){return Fib(10);}",
            55, "recursive call results survive the second recursive call"},
        {"export int Probe(){return Later(6);}int Later(int x){return x*7;}",
            42, "forward call resolves the checked function"},
        {"bool Even(int n){if(n==0)return true;return Odd(n-1);}bool Odd(int n){if(n==0)return false;return Even(n-1);}"
            "export int Probe(){if(Even(12)&&Odd(9)&&!Odd(12))return 1;return 0;}",
            1, "mutual calls preserve boolean results"},
        {"int order=0;int Mark(int x){order=order*10+x;return x;}int Pack(int a,int b,int c){return a*100+b*10+c;}"
            "export int Probe(){int value=Pack(Mark(1),Pack(Mark(2),Mark(3),Mark(4)),Mark(5));return order*10000+value;}",
            123452445, "nested arguments execute once from left to right"},
        {"int target=2;int Step(){target=7;return 3;}int Compose(int x,int y){target+=y;return x*100+target;}"
            "export int Probe(){int value=Compose(target,Step());return value*10+target;}",
            2110, "value argument snapshots before a later argument mutates its source"},
        {"int source=1;int Change(){source=9;return 0;}float Read(float first,int unused){return first;}"
            "export int Probe(){return int(Read(source,Change())*10)+source;}",
            19, "widened argument snapshots before a later call"},
        {"struct S{int x;};S source=S(1);int Replace(){source=S(9);return 0;}int Read(S copy,int unused){source.x+=1;return copy.x*100+source.x;}"
            "export int Probe(){return Read(source,Replace())*10+source.x;}",
            1110, "struct argument snapshot survives source replacement and later field mutation"},
        {"struct Inner{int x;};struct Outer{Inner value;string name;};Outer source=Outer(Inner(2),\"old\");"
            "Outer Read(){return source;}Outer Relay(Outer value){value.value.x+=1;return value;}"
            "export int Probe(){Outer copy=Relay(Read());source=Outer(Inner(9),\"new\");if(copy.name!=\"old\")return -1;return source.value.x*10+copy.value.x;}",
            93, "nested struct return copies survive source replacement"},
        {"Vector3 source=Vector3(1,2,3);int Change(){source.x=9;return 0;}Vector3 Read(Vector3 value,int unused){value.z=7;return value;}"
            "export int Probe(){Vector3 result=Read(source,Change());return int(result.x*100+result.z*10+source.x);}",
            179, "inline vector argument and return preserve value copies"},
        {"Vector4 source=Vector4(1,2,3,4);Vector4 Read(){return source;}Vector4 Relay(Vector4 value){value.w+=1;return value;}"
            "export int Probe(){Vector4 result=Relay(Read());source.w=9;return int(result.w*10+source.w);}",
            59, "heap vector argument and return preserve copy on write"},
        {"string source=\"old\";string Read(){return source;}int Change(){source=\"new\";return 0;}string Relay(string text,int unused){return text..\"!\";}"
            "export int Probe(){string result=Relay(Read(),Change());if(result==\"old!\"&&source==\"new\")return 1;return 0;}",
            1, "string return survives later call and concatenation"},
        {"list<int> Maybe(bool present){if(present)return [7];return null;}string Text(bool present){if(present)return \"ok\";return null;}"
            "export int Probe(){list<int> number=Maybe(true);string text=Text(true);if(number[0]==7&&Maybe(false)==null&&text==\"ok\"&&Text(false)==null)return 1;return 0;}",
            1, "reference result transfers retain null and nonnull alternatives"},
        {"list<string> Make(){list<string> value=[\"old\"];return value;}list<string> Relay(list<string> value){value[0]=\"new\";return value;}"
            "export int Probe(){list<string> a=Make();list<string> b=Relay(a);b.resize(2,\"tail\");if(a[0]==\"new\"&&a[1]==\"tail\")return a.len();return 0;}",
            2, "returned list lifetime and shared identity survive local release"},
        {"map<string,int> Make(){map<string,int> value={};value[\"x\"]=3;return value;}map<string,int> Relay(map<string,int> value){value[\"x\"]=7;return value;}"
            "export int Probe(){map<string,int> a=Make();map<string,int> b=Relay(a);b[\"y\"]=9;return a[\"x\"]*10+a[\"y\"];}",
            79, "returned map lifetime and shared identity survive local release"},
        {"array<Vector4> Make(){return array<Vector4>(1,Vector4(1,2,3,4));}array<Vector4> Relay(array<Vector4> value){value[0].w=7;return value;}"
            "export int Probe(){array<Vector4> a=Make();array<Vector4> b=Relay(a);b[0].w=9;return int(a[0].w);}",
            9, "returned vector array retains its element storage"},
        {"int state=0;void Explicit(int value){state=state*10+value;return;}void Implicit(int value){state=state*10+value;}"
            "void Relay(){Explicit(1);Implicit(2);return;}export int Probe(){Relay();Implicit(3);return state;}",
            123, "void calls support explicit and implicit returns"},
        {"int saved=0;int Next(){return 7;}int Assign(){saved=Next();return saved;}export int Probe(){int result=Assign();return result*10+saved;}",
            77, "return after call assignment retains observable global write"},
        {"int calls=0;int Ignored(){calls+=1;return calls;}export int Probe(){Ignored();Ignored();return calls;}",
            2, "discarded scalar call results retain effects"}
    };
    for (const auto& item : cases)
        for (bool image : {false, true})
        {
            const Value direct = Run(runtime, item.source, debug, image, std::string(item.label) + " IR");
            Check(direct.status == RunStatus::Completed && direct.type == ValueType::Int && direct.integer == item.expected,
                std::string(item.label) + " expected value");
        }
    for (const char* source : {"export void Probe(){return;}", "export void Probe(){}"})
        for (bool image : {false, true})
        {
            const Value direct = Run(runtime, source, debug, image, "void host result IR");
            Check(direct.status == RunStatus::Completed && direct.type == ValueType::None, "void host call returns no value");
        }
}

void CallReturnKinds(IRuntime& runtime, bool debug)
{
    struct FloatCase { const char* source; double expected; const char* label; };
    const FloatCase floats[] = {
        {"float Echo(float value){return value;}export float Probe(){return Echo(-0.0);}",
            -0.0, "call and return retain signed zero"},
        {"float Widen(int value){return value;}export float Probe(){return Widen(9007199254740993);}",
            9007199254740992.0, "return widens integer to binary64 once"},
        {"float Echo(float value){return value;}export float Probe(){float zero=0.0;return Echo(zero/zero);}",
            (std::numeric_limits<double>::quiet_NaN)(), "call and return retain NaN"}
    };
    for (const auto& item : floats)
        for (bool image : {false, true})
        {
            const Value direct = Run(runtime, item.source, debug, image, std::string(item.label) + " IR");
            Check(direct.status == RunStatus::Completed && direct.type == ValueType::Float &&
                ((std::isnan(item.expected) && std::isnan(direct.number)) ||
                    std::memcmp(&direct.number, &item.expected, sizeof(double)) == 0), std::string(item.label) + " expected value");
        }
    for (bool image : {false, true})
    {
        const char* text = "string Make(){return \"ret\"..\"urn\";}string Relay(string value){return value;}export string Probe(){return Relay(Make());}";

        const Value direct = Run(runtime, text, debug, image, "owned string return IR");
        Check(direct.status == RunStatus::Completed && direct.type == ValueType::String && direct.text == "return",
            "returned string remains alive until copied across the host boundary");
        const char* nullable = "string Maybe(){return null;}string Relay(){return Maybe();}export string Probe(){return Relay();}";

        const Value directNull = Run(runtime, nullable, debug, image, "null return IR");
        Check(directNull.status == RunStatus::Completed && directNull.type == ValueType::None,
            "null reference return is null at the host boundary");
    }
}

// 앞선 인자나 함수 본문이 실패하면 뒤 인자와 호출자 후속 문장이 실행되지 않아야 한다.
void CallErrors(IRuntime& runtime, bool debug)
{
    struct Case { const char* source; std::int64_t expected; const char* frame; };
    const Case cases[] = {
        {"int state=0;\nint Fail(){\nstate=1;int zero=0;\nreturn 1/zero;\n}\n"
            "int Later(){state=2;return 2;}int Take(int a,int b){state=3;return a+b;}\n"
            "export int Probe(){int result=Take(Fail(),Later());state=4;return result;}\nexport int State(){return state;}", 1, "Fail"},
        {"int state=0;\nint Take(int a,int b){\nstate=3;int zero=0;\nreturn 1/zero;\n}\n"
            "int Mark(int value){state=state*10+value;return value;}\n"
            "export int Probe(){int result=Take(Mark(1),Mark(2));state=4;return result;}\nexport int State(){return state;}", 3, "Take"},
        {"int state=0;int target=2;\nint Fail(){\ntarget=8;state=1;int zero=0;\nreturn 1/zero;\n}\n"
            "int Later(){state=2;return 2;}int Take(int a,int b){state=3;return a+b;}\n"
            "export int Probe(){return Take(Fail(),Later());}\nexport int State(){return target*10+state;}", 81, "Fail"}
    };
    for (std::size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index)
            for (bool image : {false, true})
            {
                const std::string label = "call failure " + std::to_string(index) + " IR";
                NeoCore::StaticCompileResult checked;
                Check(NeoCore::CompileStaticSource(cases[index].source, checked), label + " frontend");
                if (!checked.success) continue;
                ProgramHandle program = Compile(runtime, checked, debug, image, label);
                if (!program) continue;
                InstanceHandle instance = runtime.CreateInstance(program);
                Check(bool(instance), label + " instance");
                if (instance)
                {
                    const Value value = Invoke(runtime, instance, "Probe");
                    Check(value.status == RunStatus::Failed && value.error.message.find("integer division by zero") == 0,
                        label + " expected runtime error: " + value.error.message);
                    const Value state = Invoke(runtime, instance, "State");
                    Check(state.status == RunStatus::Completed && state.type == ValueType::Int && state.integer == cases[index].expected,
                        label + " retains completed argument effects and allows the next call");
                    if (debug)
                    {
                        Check(value.error.line == 4, label + " reports the failing callee source line: " + value.error.message);
                        Check(value.error.message.find(cases[index].frame) != std::string::npos &&
                            value.error.message.find("Probe") != std::string::npos, label + " retains callee and caller frames");
                        if (!image) Check(value.error.sourceName == "ir-regression.nc", label + " retains source filename");
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
    StorageStructure();
    CallStructure();
    RejectReferences();
    DiscardStructure();
    for (bool debug : {false, true})
    {
        InternalNameBoundary(*runtime, debug);
        MutationAndLifetime(*runtime, debug);
        InvalidMetadata(*runtime, debug);
        MapMetadata(*runtime, debug);
        InvalidStorageMetadata(*runtime, debug);
        InvalidCallMetadata(*runtime, debug);
        EvaluationOrder(*runtime, debug);
        ErrorOrder(*runtime, debug);
        ScalarWrites(*runtime, debug);
        ScalarWriteErrors(*runtime, debug);
        DiscardedWrites(*runtime, debug);
        DirectDestinationStructure(*runtime, debug);
        CallTransfers(*runtime, debug);
        CallReturnKinds(*runtime, debug);
        CallErrors(*runtime, debug);
    }
    DestroyRuntime(runtime);
    std::printf("NeoCore IR regression: %d/%d passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
