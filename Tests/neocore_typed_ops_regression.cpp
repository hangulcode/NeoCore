#include "NeoCoreCompiler.h"
#include "NeoScript.h"
#include "NeoVMProgram.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace NeoScript;


static int checks = 0;
static int failures = 0;

static void Check(bool condition, const std::string& label)
{
    ++checks;
    if (!condition && ++failures <= 60)
        std::fprintf(stderr, "FAIL: %s\n", label.c_str());
}

struct Outcome
{
    bool valid = false;
    RunStatus status = RunStatus::Failed;
    ValueType type = ValueType::None;
    std::int64_t integer = 0;
    double number = 0;
    bool boolean = false;
    std::string text;
};

static Outcome Int(std::int64_t value)
{
    Outcome result; result.valid = true; result.status = RunStatus::Completed;
    result.type = ValueType::Int; result.integer = value; return result;
}

static Outcome Float(double value)
{
    Outcome result; result.valid = true; result.status = RunStatus::Completed;
    result.type = ValueType::Float; result.number = value; return result;
}

static Outcome Bool(bool value)
{
    Outcome result; result.valid = true; result.status = RunStatus::Completed;
    result.type = ValueType::Bool; result.boolean = value; return result;
}

// 명령 수가 달라도 오류의 종류·원본 줄·호출 스택은 같아야 한다.
static std::string NormalizeIP(std::string text)
{
    std::size_t at = 0;
    while ((at = text.find("IP(", at)) != std::string::npos)
    {
        const std::size_t end = text.find(')', at);
        if (end == std::string::npos) break;
        text.replace(at + 3, end - at - 3, "*");
        at += 5;
    }
    return text;
}

static bool Same(const Outcome& left, const Outcome& right)
{
    if (!left.valid || !right.valid || left.status != right.status) return false;
    if (left.status != RunStatus::Completed)
        return NormalizeIP(left.text) == NormalizeIP(right.text);
    if (left.type != right.type) return false;
    if (left.type == ValueType::Int) return left.integer == right.integer;
    if (left.type == ValueType::Bool) return left.boolean == right.boolean;
    if (left.type == ValueType::String) return left.text == right.text;
    if (left.type == ValueType::Float)
    {
        if (std::isnan(left.number) || std::isnan(right.number))
            return std::isnan(left.number) && std::isnan(right.number);
        return std::memcmp(&left.number, &right.number, sizeof(double)) == 0;
    }
    return left.type == ValueType::None;
}

static Outcome Invoke(IRuntime* runtime, InstanceHandle instance, const char* name)
{
    Outcome result;
    auto call = runtime->Call(instance, name);
    result.status = call.invoke();
    result.valid = true;
    if (result.status != RunStatus::Completed)
    {
        result.text = call.error().message;
        return result;
    }
    result.type = call.retType();
    if (result.type == ValueType::Int) result.integer = call.retInt();
    else if (result.type == ValueType::Float) result.number = call.retFloat();
    else if (result.type == ValueType::Bool) result.boolean = call.retBool();
    else if (result.type == ValueType::String) result.text = call.retString().str();
    return result;
}

static std::vector<SVMOperation> Operations(const std::vector<uint8_t>& image)
{
    std::vector<SVMOperation> result;
    if (image.size() < sizeof(SNeoVMHeader)) return result;
    SNeoVMHeader header;
    std::memcpy(&header, image.data(), sizeof(header));
    if (header._iCodeSize < 0 || header._iCodeSize % sizeof(SVMOperation) != 0 ||
        static_cast<std::size_t>(header._iCodeSize) > image.size() - sizeof(header))
        return result;
    result.resize(static_cast<std::size_t>(header._iCodeSize) / sizeof(SVMOperation));
    if (!result.empty()) std::memcpy(result.data(), image.data() + sizeof(header), header._iCodeSize);
    return result;
}

static int CountTyped(const std::vector<SVMOperation>& operations, OperationCode only = NOP_NONE)
{
    int count = 0;
    for (const auto& operation : operations)
        if (GetTypedOpInfo(operation.Code()) && (only == NOP_NONE || only == ArithmeticMeaning(operation.Code()))) ++count;
    return count;
}

static std::string DescribeOperations(const std::vector<SVMOperation>& operations)
{
    std::string result;
    for (std::size_t i = 0; i < operations.size(); ++i)
    {
        const SVMOperation& operation = operations[i];
        const TypedOpInfo* info = GetTypedOpInfo(operation.Code());
        result += " " + std::to_string(i) + ":" + (info ? info->name : std::to_string(operation.Code())) +
            "[" + std::to_string(operation.n1) + "," + std::to_string(operation.n2) + "," +
            std::to_string(operation.n3) + ";flags=" + std::to_string(operation.argFlag) + "]";
    }
    return result;
}

static Outcome RunBackend(IRuntime* runtime, const std::string& source,
    const std::string& prefix, bool debug, bool image, const std::string& label,
    const NeoCore::TypedIR* ir = nullptr)
{
    CompileDesc desc;
    desc.source = source;
    desc.sourceName = "typed-ops.nc";
    desc.includeDebugInfo = debug;
    desc.coreIntrinsicPrefix = prefix;
    desc.coreTypedIR = ir;
    Error error;
    ProgramHandle program;
    if (image)
    {
        std::vector<uint8_t> bytes;
        error = runtime->CompileToBytecode(desc, bytes);
        if (error.ok()) program = runtime->LoadProgram(bytes, &error);
    }
    else
    {
        const CompileResult compiled = runtime->Compile(desc);
        program = compiled.program;
        error = compiled.error;
    }
    Check(bool(program) && error.ok(), label + " backend compile/load: " + error.message);
    if (!program) return Outcome();
    InstanceHandle instance = runtime->CreateInstance(program);
    Check(bool(instance), label + " instance");
    Outcome result;
    if (instance)
    {
        result = Invoke(runtime, instance, "Probe");
        Check(result.status == RunStatus::Completed || result.status == RunStatus::Failed,
            label + " terminal result");
        const Outcome recovery = Invoke(runtime, instance, "Recovery");
        Check(Same(recovery, Int(29)), label + " subsequent call works");
        runtime->DestroyInstance(instance);
    }
    runtime->DestroyProgram(program);
    return result;
}

static Outcome RunChecked(IRuntime* runtime, const std::string& source,
    bool debug, bool image, const std::string& label)
{
    NeoCore::StaticCompileResult checked;
    const bool accepted = NeoCore::CompileStaticSource(source, checked);
    Check(accepted, label + " frontend: " + checked.errorMessage);
    if (!accepted) return Outcome();
    Check(!checked.backendIntrinsicPrefix.empty(), label + " typed backend is required");
    return RunBackend(runtime, checked.loweredSource, checked.backendIntrinsicPrefix,
        debug, image, label, &checked.typedIR);
}

static void Compare(IRuntime* runtime, const std::string& source, const Outcome& expected,
    const char* reason, int sourceLine, bool debug, const std::string& label)
{
    const std::string complete = source + "\nexport int Recovery(){return 29;}";
    for (bool image : {false, true})
    {

        const Outcome typed = RunChecked(runtime, complete, debug, image, label + " typed");

        if (reason)
        {
            Check(typed.valid && typed.status == RunStatus::Failed && typed.text.find(reason) == 0,
                label + " expected error: " + typed.text);
            if (debug && sourceLine > 0)
                Check(typed.text.find("Line(" + std::to_string(sourceLine) + ")") != std::string::npos,
                    label + " original source line: " + typed.text);
        }
        else Check(Same(typed, expected), label + " expected scalar value and representation");
    }
}

static void Arithmetic(IRuntime* runtime, bool debug)
{
    struct Case
    {
        const char* source;
        Outcome expected;
        const char* label;
    };
    const std::int64_t minimum = (std::numeric_limits<std::int64_t>::min)();
    const std::int64_t maximum = (std::numeric_limits<std::int64_t>::max)();
    const Case cases[] = {
        {"export int Probe(){int a=9223372036854775807;int b=1;return a+b;}", Int(minimum), "integer addition wraps"},
        {"export int Probe(){int a=-9223372036854775808;int b=1;return a-b;}", Int(maximum), "integer subtraction wraps"},
        {"export int Probe(){int a=9223372036854775807;int b=2;return a*b;}", Int(-2), "integer multiplication wraps"},
        {"export int Probe(){int a=-7;int b=3;return a/b;}", Int(-2), "integer division truncates"},
        {"export int Probe(){int a=-9223372036854775808;return -a;}", Int(minimum), "integer negation wraps minimum"},
        {"export float Probe(){float a=0.0;return -a;}", Float(-0.0), "float negation creates negative zero"},
        {"export float Probe(){float a=-0.0;return -a;}", Float(0.0), "float negation restores positive zero"},
        {"export bool Probe(){bool a=false;return !a;}", Bool(true), "logical not remains bool"},
        {"export bool Probe(){bool a=true;return !a;}", Bool(false), "logical not reverses true"},
        {"export int Probe(){int a=-9223372036854775808;int b=1;return a/b;}", Int(minimum), "minimum division boundary"},
        {"export int Probe(){int a=0;int b=-1;return a/b;}", Int(0), "zero integer quotient"},
        {"export float Probe(){float a=7.5;float b=2.0;return a+b;}", Float(9.5), "float addition"},
        {"export float Probe(){float a=7.5;float b=2.0;return a-b;}", Float(5.5), "float subtraction"},
        {"export float Probe(){float a=7.5;float b=2.0;return a*b;}", Float(15.0), "float multiplication"},
        {"export float Probe(){float a=7.5;float b=2.0;return a/b;}", Float(3.75), "float division"},
        {"export float Probe(){float a=-0.0;float b=2.0;return a*b;}", Float(-0.0), "multiplication preserves signed zero"},
        {"export float Probe(){float a=0.0;float b=-2.0;return a/b;}", Float(-0.0), "division preserves signed zero"},
        {"export float Probe(){float a=1.0;float b=0.0;return a/b;}", Float((std::numeric_limits<double>::infinity)()), "float positive infinity"},
        {"export float Probe(){float a=0.0;return a/a;}", Float((std::numeric_limits<double>::quiet_NaN)()), "float NaN"},
        {"export float Probe(){int a=9007199254740993;return tofloat(a);}", Float(9007199254740992.0), "integer promotion rounds binary64"},
        {"export float Probe(){int a=5;float b=0.5;return a+b;}", Float(5.5), "mixed operation widens input"},
        {"export int Probe(){float a=7.75;return int(a);}", Int(7), "explicit float conversion truncates"},
        {"export int Probe(){float a=-7.75;return toint(a);}", Int(-7), "negative float conversion truncates"},
        {"export int Probe(){float a=1e300;return toint(a);}", Int(maximum), "positive conversion saturates"},
        {"export int Probe(){float a=-1e300;return toint(a);}", Int(minimum), "negative conversion saturates"},
        {"export int Probe(){float z=0.0;return toint(z/z);}", Int(0), "NaN conversion is zero"},
        {"export int Probe(){float z=0.0;return toint(1.0/z);}", Int(maximum), "infinity conversion saturates"},
        {"export int Probe(){int a=7;int b=3;a=a-b;b=a*b;return b;}", Int(12), "destination aliases inputs"},
        {"export float Probe(){float a=7.5;float b=2.0;a=b+a;b=a/b;return b;}", Float(4.75), "float destination aliases inputs"},
        {"int x=1;int Change(){x=5;return 2;}export int Probe(){int value=x+Change();return value*10+x;}", Int(35), "left operand snapshot"},
        {"int x=3;int Change(){x=8;return 2;}int Pair(int a,int b){return a*10+b;}export int Probe(){return Pair(x,Change());}", Int(32), "argument snapshot before global mutation"},
        {"int count=0;bool Bump(){count=count+1;return true;}export bool Probe(){bool a=false&&Bump();bool b=true||Bump();return count==0&&!a&&b;}", Bool(true), "short circuit excludes typed operations"},
        {"struct S{int x;};S s=S(3);int Change(){s=S(8);return 2;}export int Probe(){int value=s.x+Change();return value*10+s.x;}", Int(58), "field value snapshot before struct replacement"},
        {"export int Probe(){list<int> a=[3];int x=a[0];return x*4+1;}", Int(13), "collection read feeds typed arithmetic"},
        {"int __neocore_iadd=3;export int Probe(){int a=2;return __neocore_iadd+a;}", Int(5), "source identifier does not hijack intrinsic prefix"}
    };
    for (const auto& item : cases)
        Compare(runtime, item.source, item.expected, nullptr, 0, debug, item.label);

    Compare(runtime,
        "int saved=11;export int Probe(){int zero=0;saved=7/zero;return saved;}",
        Outcome(), "integer division by zero", 0, debug, "integer zero divisor");
    Compare(runtime,
        "export int Probe(){int a=-9223372036854775808;int b=-1;return a/b;}",
        Outcome(), "integer division overflow", 0, debug, "integer division overflow");
    Compare(runtime,
        "int zero=0;\nexport int Probe(){\nreturn 1 +\n  (8 / zero);\n}",
        Outcome(), "integer division by zero", 4, debug, "multiline operator error position");
}

// 기대 순서를 고정해 두어 비교 구현을 시험 코드 안에서 다시 작성하지 않는다.
static void Comparisons(IRuntime* runtime, bool debug)
{
    enum Order { Less, Equal, Greater, Unordered };
    struct Pair { const char* declarations; Order order; const char* label; };
    const Pair pairs[] = {
        {"int a=-2;int b=3;", Less, "integer less"},
        {"int a=7;int b=7;", Equal, "integer equal"},
        {"int a=3;int b=-2;", Greater, "integer greater"},
        {"int a=-9223372036854775808;int b=9223372036854775807;", Less, "integer limits"},
        {"int a=9007199254740993;int b=9007199254740992;", Greater, "positive integer precision"},
        {"int a=-9007199254740993;int b=-9007199254740992;", Less, "negative integer precision"},
        {"int a=9223372036854775807;int b=9223372036854775806;", Greater, "adjacent maximum integers"},
        {"float a=1.5;float b=2.5;", Less, "float less"},
        {"float a=2.5;float b=1.5;", Greater, "float greater"},
        {"float a=0.0;float b=-0.0;", Equal, "signed zeros compare equal"},
        {"float a=-0.0;float b=0.0;", Equal, "reverse signed zeros"},
        {"float z=0.0;float a=z/z;float b=1.0;", Unordered, "left NaN"},
        {"float z=0.0;float a=1.0;float b=z/z;", Unordered, "right NaN"},
        {"float z=0.0;float a=z/z;float b=z/z;", Unordered, "two NaNs"},
        {"float z=0.0;float a=1.0/z;float b=0.0;", Greater, "positive infinity"},
        {"float z=0.0;float a=-1.0/z;float b=0.0;", Less, "negative infinity"},
        {"float z=0.0;float a=1.0/z;float b=1.0/z;", Equal, "same positive infinities"},
        {"float z=0.0;float a=-1.0/z;float b=-1.0/z;", Equal, "same negative infinities"},
        {"float z=0.0;float a=-1.0/z;float b=1.0/z;", Less, "opposite infinities"},
        {"float a=1.0000000000000002;float b=1.0;", Greater, "adjacent binary64 values"},
        {"int a=9007199254740993;float b=9007199254740992.0;", Equal, "mixed integer rounds to float"},
        {"float a=9007199254740992.0;int b=9007199254740993;", Equal, "reverse mixed rounding"},
        {"int a=9223372036854775807;float b=9223372036854775808.0;", Equal, "mixed maximum integer"},
        {"int a=-9223372036854775808;float b=-9223372036854775808.0;", Equal, "mixed minimum integer"},
        {"int a=-9223372036854775807;float b=-9223372036854775808.0;", Equal, "mixed negative rounding"},
        {"int a=1;float b=1.5;", Less, "mixed integer less"},
        {"float a=1.5;int b=1;", Greater, "mixed float greater"},
        {"float z=0.0;int a=-9223372036854775808;float b=z/z;", Unordered, "mixed NaN"}
    };
    const char* operators[] = {"==", "!=", "<", "<=", ">", ">="};
    const bool expected[4][6] = {
        {false, true, true, true, false, false},
        {true, false, false, true, false, true},
        {false, true, false, false, true, true},
        {false, true, false, false, false, false}
    };
    for (const auto& pair : pairs)
    {
        for (int op = 0; op < 6; ++op)
        {
            const std::string condition = "a" + std::string(operators[op]) + "b";
            const std::string source = "export int Probe(){" + std::string(pair.declarations) +
                "int result=0;bool value=" + condition + ";if(value)result+=32;" +
                "if(" + condition + ")result+=1;else result+=2;" +
                "while(" + condition + "){result+=4;break;}" +
                "if(!(" + condition + "))result+=8;if(!!(" + condition + "))result+=16;return result;}";
            Compare(runtime, source, Int(expected[pair.order][op] ? 53 : 10), nullptr, 0, debug,
                std::string(pair.label) + " " + operators[op] + " values and branches");
            if (&pair == &pairs[0] || &pair == &pairs[1] || &pair == &pairs[2] ||
                &pair == &pairs[7] || &pair == &pairs[9] || &pair == &pairs[11] || &pair == &pairs[21])
                Compare(runtime, "export bool Probe(){" + std::string(pair.declarations) +
                    "return " + condition + ";}", Bool(expected[pair.order][op]), nullptr, 0, debug,
                    std::string(pair.label) + " " + operators[op] + " returns bool");
        }
    }

    Compare(runtime,
        "int trace=0;int A(){trace=trace*10+1;return 3;}int B(){trace=trace*10+2;return 2;}"
        "export int Probe(){bool first=A()>B();bool second=A()>=B();if(!first||!second)return -1;return trace;}",
        Int(1212), nullptr, 0, debug, "reversed comparison preserves call order");
    Compare(runtime,
        "export int Probe(){int x=0;bool first=(x=2)>(x=3);bool second=(x=4)>=(x=5);"
        "if(first||second)return -1;return x;}",
        Int(5), nullptr, 0, debug, "reversed comparison snapshots assignment operands");
    Compare(runtime,
        "int g=3;int Change(){g=1;return 2;}export bool Probe(){return g>Change();}",
        Bool(true), nullptr, 0, debug, "comparison snapshots global before side effect");
    Compare(runtime,
        "list<int> values=[3];int Change(){values[0]=1;return 2;}"
        "export bool Probe(){return values[0]>Change();}",
        Bool(true), nullptr, 0, debug, "comparison snapshots collection element before side effect");
    Compare(runtime,
        "int count=0;bool Bump(){count=count+1;return true;}"
        "export int Probe(){int a=1;int b=2;bool x=a>b&&Bump();bool y=a<b||Bump();"
        "bool z=a<b&&Bump();bool w=a>b||Bump();if(x||!y||!z||!w)return -1;return count;}",
        Int(2), nullptr, 0, debug, "typed comparisons retain short circuit effects");
    Compare(runtime,
        "export bool Probe(){float z=0.0;float nan=z/z;return !(nan<1.0)&&!(nan>=1.0)&&nan!=nan;}",
        Bool(true), nullptr, 0, debug, "negated unordered branches are not opposite comparisons");
    Compare(runtime,
        "export int Probe(){int i=0;int total=0;while(i<4){i=i+1;if(i==2)continue;total=total+i;}return total;}",
        Int(8), nullptr, 0, debug, "comparison backedge and continue target");
    Compare(runtime,
        "int trace=0;int A(){trace=trace*10+1;return 3;}int B(){trace=trace*10+2;return 2;}"
        "export int Probe(){if(A()>B())trace=trace*10+3;return trace;}",
        Int(123), nullptr, 0, debug, "fused reversed branch preserves call order");
    Compare(runtime,
        "export bool Probe(){bool a=true;bool b=false;string x=\"a\";string y=\"b\";string n=null;"
        "return a!=b&&x<y&&n==null;}",
        Bool(true), nullptr, 0, debug, "nonnumeric comparisons remain generic");
    Compare(runtime,
        "export bool Probe(){\nint zero=0;\nreturn (8 / zero) > 1;\n}",
        Outcome(), "integer division by zero", 3, debug, "comparison operand error preserves original line");

    struct RangeCase { const char* values; const char* condition; bool expected; };
    const RangeCase ranges[] = {
        {"float x=nan;float lo=0.0;float hi=1.0;", "!(x<lo)&&!(x>hi)", true},
        {"float x=nan;float lo=0.0;float hi=1.0;", "!(x>=lo)||!(x<=hi)", true},
        {"float x=nan;float lo=0.0;float hi=1.0;", "!(x<lo)&&x<hi", false},
        {"float x=nan;float lo=0.0;float hi=1.0;", "x>lo||!(x>hi)", true},
        {"float x=0.5;float lo=nan;float hi=1.0;", "!(x<lo)&&!(x>hi)", true},
        {"float x=0.5;float lo=0.0;float hi=nan;", "!(x<lo)&&!(x>hi)", true},
        {"float x=0.5;float lo=nan;float hi=nan;", "!(x>=lo)||!(x<=hi)", true},
        {"float x=0.5;float lo=nan;float hi=1.0;", "!(x<lo)&&x<=hi", true},
        {"float x=0.5;float lo=0.0;float hi=nan;", "x>=lo&&!(x>hi)", true},
        {"float x=0.5;float lo=nan;float hi=1.0;", "!(x>=lo)||x>hi", true},
        {"float x=0.5;float lo=0.0;float hi=nan;", "x<lo||!(x<=hi)", true},
        {"float x=0.0;float lo=0.0;float hi=1.0;", "!(x<lo)&&!(x>hi)", true},
        {"float x=1.0;float lo=0.0;float hi=1.0;", "!(x<lo)&&!(x>hi)", true},
        {"float x=2.0;float lo=0.0;float hi=1.0;", "!(x<lo)&&!(x>hi)", false},
        {"float x=nan;float lo=0.0;float hi=1.0;", "x>=lo&&x<=hi", false},
        {"float x=nan;float lo=0.0;float hi=1.0;", "x<lo||x>hi", false},
        {"float x=0.5;float lo=nan;float hi=1.0;", "x>=lo&&x<=hi", false},
        {"float x=0.5;float lo=0.0;float hi=nan;", "x<lo||x>hi", false}
    };
    for (const auto& range : ranges)
    {
        const std::string source = "export int Probe(){float z=0.0;float nan=z/z;" +
            std::string(range.values) + "if(" + range.condition + ")return 1;return 0;}";
        Compare(runtime, source, Int(range.expected ? 1 : 0), nullptr, 0, debug,
            "unordered range: " + std::string(range.condition) + " " + range.values);
    }

    // 괄호를 줄이는 렌더링이 논리 그룹의 경계나 호출 생략 순서를 바꾸면 안 된다.
    struct GroupCase { const char* condition; unsigned trueMask; int traces[8]; };
    const GroupCase groups[] = {
        {"(A()>0||B()>0)&&C()>0", 0xa8, {12, 12, 123, 123, 13, 13, 13, 13}},
        {"A()>0||(B()>0&&C()>0)", 0xf8, {12, 12, 123, 123, 1, 1, 1, 1}},
        {"(A()>0&&B()>0)||C()>0", 0xea, {13, 13, 13, 13, 123, 123, 12, 12}},
        {"A()>0&&(B()>0||C()>0)", 0xe0, {1, 1, 1, 1, 123, 123, 12, 12}},
        {"!(A()>0||B()>0)&&C()>0", 0x02, {123, 123, 12, 12, 1, 1, 1, 1}},
        {"!(A()>0&&B()>0)||C()>0", 0xbf, {1, 1, 1, 1, 12, 12, 123, 123}}
    };
    for (const auto& group : groups)
    {
        for (unsigned values = 0; values < 8; ++values)
        {
            const std::string declarations = "int trace=0;int A(){trace=trace*10+1;return " +
                std::to_string((values >> 2) & 1) + ";}int B(){trace=trace*10+2;return " +
                std::to_string((values >> 1) & 1) + ";}int C(){trace=trace*10+3;return " +
                std::to_string(values & 1) + ";}";
            const int expected = group.traces[values] * 10 + ((group.trueMask >> values) & 1);
            for (bool loop : {false, true})
            {
                const std::string body = loop ? "while(" + std::string(group.condition) + "){chosen=1;break;}"
                    : "if(" + std::string(group.condition) + ")chosen=1;else chosen=0;";
                Compare(runtime, declarations + "export int Probe(){int chosen=0;" + body +
                    "return trace*10+chosen;}", Int(expected), nullptr, 0, debug,
                    std::string(loop ? "while" : "if") + " grouped short circuit " + group.condition +
                    " values=" + std::to_string(values));
            }
        }
    }
    Compare(runtime,
        "int calls=0;bool Step(){calls=calls+1;return true;}export int Probe(){int i=0;"
        "while((i<2||i<4)&&Step()){i=i+1;}return i*10+calls;}",
        Int(44), nullptr, 0, debug, "nested grouped while reevaluates and short circuits at exit");
}

static void Shapes(IRuntime* runtime, bool debug)
{
    struct Shape { const char* body; OperationCode expected; };
    const Shape shapes[] = {
        {"export int Probe(int a,int b){return a+b;}", OP_I64_ADD},
        {"export int Probe(int a,int b){return a-b;}", OP_I64_SUB},
        {"export int Probe(int a,int b){return a*b;}", OP_I64_MUL},
        {"export int Probe(int a,int b){return a/b;}", OP_I64_DIV},
        {"export float Probe(float a,float b){return a+b;}", OP_F64_ADD},
        {"export float Probe(float a,float b){return a-b;}", OP_F64_SUB},
        {"export float Probe(float a,float b){return a*b;}", OP_F64_MUL},
        {"export float Probe(float a,float b){return a/b;}", OP_F64_DIV},
        {"export int Probe(int a){return -a;}", OP_I64_NEG},
        {"export float Probe(float a){return -a;}", OP_F64_NEG},
        {"export float Probe(int a){return float(a);}", OP_I64_TO_F64},
        {"export int Probe(float a){return int(a);}", OP_F64_TO_I64}
    };
    for (const auto& shape : shapes)
    {
        {
            NeoCore::StaticCompileResult checked;
            Check(NeoCore::CompileStaticSource(shape.body, checked), "shape frontend");
            if (!checked.success) continue;
            CompileDesc desc;
            desc.source = checked.loweredSource;
            desc.sourceName = "typed-shape.nc";
            desc.includeDebugInfo = debug;
            desc.coreIntrinsicPrefix = checked.backendIntrinsicPrefix;
            desc.coreTypedIR = &checked.typedIR;
            std::vector<uint8_t> bytes;
            const Error error = runtime->CompileToBytecode(desc, bytes);
            Check(error.ok(), std::string("shape image: ") + error.message);
            if (!error.ok()) continue;
            const auto operations = Operations(bytes);
            Check(CountTyped(operations, shape.expected) == 1 && CountTyped(operations) == 1,
                "typed opcode emitted exactly once");
            Error loadError;
            ProgramHandle loaded = runtime->LoadProgram(bytes, &loadError);
            Check(bool(loaded) && loadError.ok(), "typed opcode image accepted");
            if (loaded) runtime->DestroyProgram(loaded);
            {
                for (std::size_t index = 0; index < operations.size(); ++index)
                {
                    if (ArithmeticMeaning(operations[index].Code()) != shape.expected) continue;
                    for (int change = 0; change < 3; ++change)
                    {
                        std::vector<SVMOperation> altered = operations;
                        if (change == 0) altered[index].argFlag |= NEOS_OP_SUB_MASK;
                        else if (change == 1)
                        {
                            altered[index].argFlag &= ~NEOS_ARG_N1_LOCAL;
                            altered[index].n1 = 0;
                        }
                        else
                        {
                            altered[index].argFlag |= NEOS_ARG_N2_LOCAL;
                            altered[index].n2 = 32767;
                        }
                        std::vector<uint8_t> malformed = bytes;
                        std::memcpy(malformed.data() + sizeof(SNeoVMHeader), altered.data(),
                            altered.size() * sizeof(SVMOperation));
                        Error rejected;
                        ProgramHandle invalid = runtime->LoadProgram(malformed, &rejected);
                        Check(!invalid && !rejected.ok(), "typed image rejects invalid operand metadata");
                        if (invalid) runtime->DestroyProgram(invalid);
                    }
                    break;
                }
            }
        }
    }

    NeoCore::StaticCompileResult checked;
    Check(NeoCore::CompileStaticSource("export int Probe(int a,int b){return a+b;}", checked),
        "disabled-intrinsic frontend");
    CompileDesc disabled;
    disabled.source = checked.loweredSource;
    disabled.sourceName = "disabled-intrinsic.ns";
    const CompileResult result = runtime->Compile(disabled);
    Check(!result.program && !result.error.ok(), "ordinary backend cannot call gated numeric intrinsics");
    if (result.program) runtime->DestroyProgram(result.program);
    std::vector<uint8_t> image;
    const Error imageError = runtime->CompileToBytecode(disabled, image);
    Check(!imageError.ok() && image.empty(), "ordinary image compiler rejects gated intrinsics");
}

static void ComparisonShapes(IRuntime* runtime, bool debug)
{
    struct Shape { const char* type; const char* relation; OperationCode op; bool inverted; };
    const Shape shapes[] = {
        {"int", "==", OP_I64_EQ, false}, {"int", "!=", OP_I64_NE, false},
        {"int", "<", OP_I64_LT, false}, {"int", "<=", OP_I64_LE, false},
        {"int", ">", OP_I64_GT, false}, {"int", ">=", OP_I64_GE, false},
        {"float", "==", OP_F64_EQ, false}, {"float", "!=", OP_F64_NE, false},
        {"float", "<", OP_F64_LT, false}, {"float", "<=", OP_F64_LE, false},
        {"float", ">", OP_F64_GT, false}, {"float", ">=", OP_F64_GE, false}
    };
    for (const auto& shape : shapes)
    {
        const TypedOpInfo* valueInfo = GetTypedOpInfo(shape.op);
        Check(valueInfo && valueInfo->comparison && !valueInfo->jump && valueInfo->writes == 1,
            "comparison result descriptor writes bool destination");
        if (!valueInfo) continue;
        const TypedOpInfo* jumpInfo = GetTypedOpInfo(valueInfo->branchOp);
        Check(jumpInfo && jumpInfo->comparison && jumpInfo->jump && jumpInfo->slots == 6 && jumpInfo->writes == 0,
            "comparison branch descriptor treats n1 as offset");
        if (!jumpInfo) continue;

        for (int form = 0; form < 6; ++form)
        {
            const bool branch = form >= 2;
            const bool negated = form == 1 || form == 3;
            const std::string label = std::string(shape.type) + " " + shape.relation +
                " form=" + std::to_string(form) + " debug=" + std::to_string(debug);
            std::string condition = "a" + std::string(shape.relation) + "b";
            if (negated) condition = "!(" + condition + ")";
            else if (form == 4) condition = "!!(" + condition + ")";
            std::string source = "export " + std::string(branch ? "int" : "bool") + " Probe(" +
                shape.type + " a," + shape.type + " b){";
            if (!branch) source += "return " + condition + ";}";
            else if (form == 5) source += "while(" + condition + "){return 1;}return 0;}";
            else source += "if(" + condition + ")return 1;return 0;}";

            NeoCore::StaticCompileResult checked;
            Check(NeoCore::CompileStaticSource(source, checked), "comparison shape frontend");
            if (!checked.success) continue;
            CompileDesc desc;
            desc.source = checked.loweredSource;
            desc.sourceName = "comparison-shape.nc";
            desc.includeDebugInfo = debug;
            desc.coreIntrinsicPrefix = checked.backendIntrinsicPrefix;
            desc.coreTypedIR = &checked.typedIR;
            std::vector<uint8_t> bytes;
            const Error error = runtime->CompileToBytecode(desc, bytes);
            Check(error.ok(), std::string("comparison shape image: ") + error.message);
            if (!error.ok()) continue;
            const auto operations = Operations(bytes);
            const OperationCode expected = branch ? valueInfo->branchOp : shape.op;
            Check(CountTyped(operations, expected) == 1 && CountTyped(operations) == 1,
                label + " comparison executes as one typed value or fused branch" + DescribeOperations(operations));
            int booleanBranches = 0;
            for (const auto& operation : operations)
                if (operation.Code() == NOP_JMP_FALSE || operation.Code() == NOP_JMP_TRUE) ++booleanBranches;
            Check(booleanBranches == 0, "direct numeric condition has no bool-result branch");

            Error loadError;
            ProgramHandle loaded = runtime->LoadProgram(bytes, &loadError);
            Check(bool(loaded) && loadError.ok(), "typed comparison image accepted");
            if (loaded) runtime->DestroyProgram(loaded);
            for (std::size_t index = 0; index < operations.size(); ++index)
            {
                if (operations[index].Code() != expected) continue;
                const bool invert = (operations[index].argFlag & NEOS_OP_COMPARE_INVERT) != 0;
                // if는 거짓일 때 건너뛰고, while의 뒤쪽 비교는 참일 때 본문으로 돌아간다.
                const bool jumpWhenFalse = branch && form != 5;
                Check(invert == (shape.inverted != (negated != jumpWhenFalse)),
                    label + " branch inversion preserves comparison including unordered values" + DescribeOperations(operations));
                for (int change = 0; change < (branch ? 5 : 3); ++change)
                {
                    std::vector<SVMOperation> altered = operations;
                    if (change == 0) altered[index].argFlag |= NEOS_OP_SUB_MASK;
                    else if (change == 1)
                    {
                        altered[index].argFlag |= NEOS_ARG_N2_LOCAL;
                        altered[index].n2 = 32767;
                    }
                    else if (change == 2)
                    {
                        if (branch) altered[index].argFlag |= NEOS_ARG_N1_LOCAL;
                        else
                        {
                            altered[index].argFlag &= ~NEOS_ARG_N1_LOCAL;
                            altered[index].n1 = 0;
                        }
                    }
                    else altered[index].n1 = change == 3 ? 32767 : -32768;
                    std::vector<uint8_t> malformed = bytes;
                    std::memcpy(malformed.data() + sizeof(SNeoVMHeader), altered.data(),
                        altered.size() * sizeof(SVMOperation));
                    Error rejected;
                    ProgramHandle invalid = runtime->LoadProgram(malformed, &rejected);
                    Check(!invalid && !rejected.ok(), "typed comparison rejects malformed slots or jump target");
                    if (invalid) runtime->DestroyProgram(invalid);
                }
                break;
            }
        }
    }

    const char* genericSources[] = {
        "export bool Probe(bool a,bool b){return a==b;}",
        "export bool Probe(string a,string b){return a<b;}",
        "export bool Probe(list<int> a,list<int> b){return a==b||a==null;}"
    };
    for (const char* source : genericSources)
    {
        NeoCore::StaticCompileResult checked;
        Check(NeoCore::CompileStaticSource(source, checked), "generic comparison shape frontend");
        if (!checked.success) continue;
        CompileDesc desc;
        desc.source = checked.loweredSource;
        desc.sourceName = "generic-comparison.nc";
        desc.includeDebugInfo = debug;
        desc.coreIntrinsicPrefix = checked.backendIntrinsicPrefix;
        desc.coreTypedIR = &checked.typedIR;
        std::vector<uint8_t> image;
        const Error error = runtime->CompileToBytecode(desc, image);
        Check(error.ok() && CountTyped(Operations(image)) == 0,
            "bool string and reference equality retain generic operations");
    }

    struct RangeShape { const char* condition; bool fused; };
    const RangeShape ranges[] = {
        {"x>=lo&&x<=hi", true}, {"x<lo||x>hi", true},
        {"!(x<lo)&&!(x>hi)", false}, {"!(x>=lo)||!(x<=hi)", false},
        {"!(x<lo)&&x<=hi", false}, {"x>=lo&&!(x>hi)", false}
    };
    for (const auto& range : ranges)
    {
        const std::string source = "export int Probe(float x,float lo,float hi){if(" +
            std::string(range.condition) + ")return 1;return 0;}";
        NeoCore::StaticCompileResult checked;
        Check(NeoCore::CompileStaticSource(source, checked), "range shape frontend");
        if (!checked.success) continue;
        CompileDesc desc;
        desc.source = checked.loweredSource;
        desc.sourceName = "range-comparison.nc";
        desc.includeDebugInfo = debug;
        desc.coreIntrinsicPrefix = checked.backendIntrinsicPrefix;
        desc.coreTypedIR = &checked.typedIR;
        std::vector<uint8_t> image;
        const Error error = runtime->CompileToBytecode(desc, image);
        Check(error.ok(), "range shape image");
        int rangesFound = 0;
        int typedBranches = 0;
        const auto operations = Operations(image);
        for (const auto& operation : operations)
        {
            if (operation.Code() == NOP_JMP_RANGE_INSIDE || operation.Code() == NOP_JMP_RANGE_OUTSIDE ||
                operation.Code() == NOP_JMP_RANGE_INSIDE_L || operation.Code() == NOP_JMP_RANGE_OUTSIDE_L)
                ++rangesFound;
            const TypedOpInfo* info = GetTypedOpInfo(operation.Code());
            if (info && info->comparison && info->jump) ++typedBranches;
        }
        Check(range.fused ? rangesFound == 1 && typedBranches == 0 : rangesFound == 0 && typedBranches == 2,
            std::string(range.condition) + " debug=" + std::to_string(debug) +
            " ordinary ranges fuse while source-negated bounds retain IEEE-safe branches: ranges=" +
            std::to_string(rangesFound) + " typedBranches=" + std::to_string(typedBranches) + DescribeOperations(operations));
    }
}

// 정적 소스는 변수 타입을 바꾸지 않는다. 여기서는 신뢰된 백엔드 입력으로 예전 참조 값이 남은 목적지를 강제로 만든다.
static void ReferenceCleanup(IRuntime* runtime, bool debug)
{
    struct Cleanup { const char* suffix; const char* a; const char* b; Outcome expected; };
    const Cleanup cases[] = {
        {"iadd", "7", "2", Int(12)}, {"isub", "7", "2", Int(8)},
        {"imul", "7", "2", Int(17)}, {"idiv", "7", "2", Int(6)},
        {"fadd", "7.0", "2.0", Float(12.0)}, {"fsub", "7.0", "2.0", Float(8.0)},
        {"fmul", "7.0", "2.0", Float(17.0)}, {"fdiv", "7.0", "2.0", Float(6.5)},
        {"ineg", "7", nullptr, Int(-4)}, {"fneg", "7.0", nullptr, Float(-4.0)},
        {"itof", "7", nullptr, Float(10.0)}, {"ftoi", "7.9", nullptr, Int(10)}
    };
    const std::string prefix = "__typed_cleanup_";
    for (const auto& item : cases)
    {
        const std::string arguments = std::string("a") + (item.b ? ",b" : "");
        const std::string source = "export fun Probe(){var a=" + std::string(item.a) +
            ";var b=" + (item.b ? item.b : "0") + ";var cell=[3];var witness=cell;" +
            "for(var i in 0,32){cell=[i];cell=" + prefix + item.suffix + "_L1(" + arguments +
            ");}return " + prefix + (item.expected.type == ValueType::Float ? "fadd" : "iadd")
            + "_L1(cell," + (item.expected.type == ValueType::Float ? prefix + "itof_L1(witness[0])" : "witness[0]")
            + ");}export fun Recovery(){return 29;}";
        AllocStats before; GetAllocStats(before);
        const Outcome result = RunBackend(runtime, source, prefix, debug, true, item.suffix);
        AllocStats after; GetAllocStats(after);
        Check(Same(result, item.expected), std::string(item.suffix) + " ref-bearing destination and retained alias");
        Check(before.lists == after.lists && before.maps == after.maps && before.arrays == after.arrays &&
            before.closures == after.closures && before.vectors == after.vectors,
            std::string(item.suffix) + " overwritten references are released");
    }

    struct CompareCleanup { const char* suffix; bool expected; };
    const CompareCleanup comparisons[] = {
        {"ieq", false}, {"ine", true}, {"ilt", false}, {"ile", false}, {"igt", true}, {"ige", true},
        {"feq", false}, {"fne", true}, {"flt", false}, {"fle", false}, {"fgt", true}, {"fge", true}
    };
    for (const auto& item : comparisons)
    {
        const bool integer = item.suffix[0] == 'i';
        const std::string source = "var witness=null;export fun Probe(){var a=" +
            std::string(integer ? "3" : "3.0") + ";var b=" + (integer ? "2" : "2.0") +
            ";var cell=[3];witness=cell;for(var i in 0,32){cell=[i];cell=" + prefix + item.suffix +
            "_L1(a,b);}return cell;}export fun Recovery(){return " + prefix + "iadd_L1(witness[0],26);}";
        AllocStats before; GetAllocStats(before);
        const Outcome result = RunBackend(runtime, source, prefix, debug, true, item.suffix);
        AllocStats after; GetAllocStats(after);
        Check(Same(result, Bool(item.expected)), std::string(item.suffix) + " stores bool over ref-bearing destination");
        Check(before.lists == after.lists, std::string(item.suffix) + " comparison releases overwritten references");
    }

    // 나눗셈 실패는 목적지의 기존 참조를 해제하거나 값을 바꾸기 전에 발생해야 한다.
    for (bool overflow : {false, true})
    {
        const std::string a = overflow ? "-9223372036854775808" : "7";
        const std::string b = overflow ? "-1" : "0";
        const std::string source = "var cell=[29];export fun Probe(){var a=" + a +
            ";var b=" + b + ";cell=" + prefix + "idiv_L1(a,b);return 0;}" +
            "export fun Recovery(){return cell[0];}";
        AllocStats before; GetAllocStats(before);
        const Outcome result = RunBackend(runtime, source, prefix, debug, true, "division preserves destination");
        AllocStats after; GetAllocStats(after);
        const char* reason = overflow ? "integer division overflow" : "integer division by zero";
        Check(result.valid && result.status == RunStatus::Failed && result.text.find(reason) == 0,
            "failed typed division retains ref-bearing destination");
        Check(before.lists == after.lists, "failed typed division releases references on final destruction");
    }
}

static void StringConversionShapes(IRuntime* runtime, bool debug)
{
    struct Shape { const char* source; int optimizedConversions; };
    const Shape cases[] = {
        {"export string Probe(int x){return \"v\"..tostring(x);}", 0},
        {"export string Probe(int x){return tostring(x)..\"v\";}", 0},
        {"export string Probe(int x){string s=tostring(x);return s..\"v\";}", 1},
        {"int x=1;string Change(){x=7;return \"v\";}export string Probe(){return tostring(x)..Change();}", 1},
        {"export string Probe(int x,bool test){string s=\"before\";if(test)s=tostring(x);return s..\"v\";}", 1}
    };
    for (const auto& item : cases)
    {
        NeoCore::StaticCompileResult checked;
        Check(NeoCore::CompileStaticSource(item.source, checked), "string conversion shape frontend");
        if (!checked.success) continue;
        CompileDesc desc;
        desc.source = checked.loweredSource;
        desc.sourceName = "string-conversion.nc";
        desc.includeDebugInfo = debug;
        desc.coreIntrinsicPrefix = checked.backendIntrinsicPrefix;
        desc.coreTypedIR = &checked.typedIR;
        std::vector<uint8_t> image;
        const Error error = runtime->CompileToBytecode(desc, image);
        Check(error.ok(), "string conversion shape image");
        int conversions = 0, concatenations = 0;
        for (const auto& operation : Operations(image))
        {
            if (operation.Code() == NOP_TOSTRING) ++conversions;
            if (operation.Code() == NOP_STR_ADD || operation.Code() == NOP_STR_ADD_L) ++concatenations;
        }
        Check(conversions == (debug ? 1 : item.optimizedConversions) && concatenations == 1,
            std::string(item.source) + " only immediate temporary conversion is folded");
    }
}

static void StaticProfile(IRuntime* runtime)
{
    CompileDesc dynamic;
    dynamic.source = "export fun Probe(var x){return x+1;}";
    const CompileResult rejected = runtime->Compile(dynamic);
    Check(!rejected.program && !rejected.error.ok() &&
        rejected.error.message.find("numeric expressions require typed IR") != std::string::npos,
        "static backend rejects dynamic arithmetic instead of emitting a generic opcode");
    if (rejected.program) runtime->DestroyProgram(rejected.program);

    NeoCore::StaticCompileResult checked;
    Check(NeoCore::CompileStaticSource("export int Probe(){return 1+2;}", checked), "constant static frontend");
    CompileDesc desc;
    desc.source = checked.loweredSource;
    desc.coreIntrinsicPrefix = checked.backendIntrinsicPrefix;
    desc.coreTypedIR = &checked.typedIR;
    std::vector<uint8_t> bytes;
    const Error error = runtime->CompileToBytecode(desc, bytes);
    Check(error.ok() && bytes.size() >= sizeof(SNeoVMHeader), "static profile image compilation");
    if (!error.ok() || bytes.size() < sizeof(SNeoVMHeader)) return;
    SNeoVMHeader header;
    std::memcpy(&header, bytes.data(), sizeof(header));
    Check(header._dwNeoVersion == NEO_VER, "static image records its own opcode profile");
    header._dwNeoVersion = ('L' << 24) | ('1' << 16) | ('4' << 8) | '9';
    std::memcpy(bytes.data(), &header, sizeof(header));
    Error loadError;
    ProgramHandle invalid = runtime->LoadProgram(bytes, &loadError);
    Check(!invalid && !loadError.ok(), "static loader rejects legacy opcode-profile images");
    if (invalid) runtime->DestroyProgram(invalid);
}

// 기본 opcode는 같아도 정의되지 않은 세부 연산을 이미지 로더가 받으면 안 된다.
static void SubOperationImages(IRuntime* runtime)
{
    const char* source =
        "export int I(int a,int b){return a-b;}"
        "export float F(float a,float b){return a/b;}"
        "export bool C(float a,float b){return a>=b;}"
        "export int J(int a,int b){if(a!=b)return 1;return 0;}"
        "export bool S(string a,string b){return a!=b;}"
        "export int SJ(string a,string b){if(a==b)return 1;return 0;}"
        "export Vector3 V(Vector3 a,float b){return a*b;}"
        "export float Convert(int a){return float(a);}"
        "export int Loop(){int s=0;for(int i in 0,3){s+=i;}list<int> a=[4,5];foreach(int x in a){s+=x;}return s;}"
        "export float Immediate(){float x=1.5;return x;}";
    NeoCore::StaticCompileResult checked;
    Check(NeoCore::CompileStaticSource(source, checked), "sub-operation image frontend: " + checked.errorMessage);
    if (!checked.success) return;
    CompileDesc desc; desc.source=checked.loweredSource; desc.coreTypedIR=&checked.typedIR;
    desc.coreIntrinsicPrefix=checked.backendIntrinsicPrefix; desc.includeDebugInfo=true;
    std::vector<uint8_t> bytes;
    Check(runtime->CompileToBytecode(desc,bytes).ok(), "sub-operation image compilation");
    const auto operations=Operations(bytes);
    bool sawFor=false, sawForeach=false, sawImmediate=false;
    for (std::size_t i=2;i<operations.size();++i)
    {
        const SVMOperation& op=operations[i];
        sawFor |= op.op==NOP_JMP_FOR; sawForeach |= op.op==NOP_JMP_FOREACH;
        sawImmediate |= op.op==NOP_MOVF;
        for(unsigned sub=0;sub<16;++sub)
        {
            const OperationCode code=MakeOperation(op.op,sub);
            const bool known=GetTypedOpInfo(code)||GetAggregateOpInfo(code)||
                (op.op==NOP_SLOT_COPY ? sub>=static_cast<unsigned>(SlotKind::Int) &&
                    sub<static_cast<unsigned>(SlotKind::Function) :
                    (op.op==NOP_COMPARE||op.op==NOP_JMP_COMPARE) ? sub<6 : sub==0);
            if(known)continue;
            auto malformed=bytes;
            SVMOperation changed=op; changed.SetCode(code);
            Check(changed.op==op.op && changed.n23==op.n23 && changed.n1==op.n1 &&
                (changed.argFlag & ~NEOS_OP_SUB_MASK)==(op.argFlag & ~NEOS_OP_SUB_MASK),
                "setting sub-operation preserves operand addresses and control flags");
            std::memcpy(malformed.data()+sizeof(SNeoVMHeader)+i*sizeof(SVMOperation),&changed,sizeof(changed));
            Error error; auto invalid=runtime->LoadProgram(malformed,&error);
            Check(!invalid&&!error.ok(), "undefined sub-operation rejected");
            if(invalid)runtime->DestroyProgram(invalid);
        }
    }
    Check(sawFor&&sawForeach, "for and foreach retain standalone primary opcodes");
    Check(sawImmediate, "float immediate survives flag layout change");
    Error error; auto program=runtime->LoadProgram(bytes,&error);
    Check(bool(program)&&error.ok(), "sub-operation image round trip");
    if(program)
    {
        auto instance=runtime->CreateInstance(program);
        Check(Same(Invoke(runtime,instance,"Loop"),Int(12)), "for and foreach execute after image round trip");
        runtime->DestroyInstance(instance);runtime->DestroyProgram(program);
    }
}

int main()
{
    IRuntime* runtime = CreateRuntime();
    Check(runtime != nullptr, "runtime creation");
    if (!runtime) return 1;
    runtime->FreezeBindings();
    StaticProfile(runtime);
    SubOperationImages(runtime);
    for (bool debug : {false, true})
    {
        Arithmetic(runtime, debug);
        Comparisons(runtime, debug);
        Shapes(runtime, debug);
        ComparisonShapes(runtime, debug);
        ReferenceCleanup(runtime, debug);
        StringConversionShapes(runtime, debug);
    }
    DestroyRuntime(runtime);
    std::printf("NeoCore typed-op regression: %d/%d passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
