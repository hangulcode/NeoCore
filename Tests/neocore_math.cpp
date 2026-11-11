#include "NeoCore.h"
#include "NeoNumberFormat.h"

#include <cfenv>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace
{
int checks = 0;
int failures = 0;

void Check(bool condition, const std::string& label)
{
    ++checks;
    if (!condition)
    {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", label.c_str());
    }
}

struct FloatCase
{
    const char* expression;
    double expected;
    double tolerance;
};

const FloatCase floatCases[] = {
    {"math.abs(-2)", 2, 0},
    {"math.acos(0)", 1.5707963267948966, 1e-14},
    {"math.asin(1)", 1.5707963267948966, 1e-14},
    {"math.atan(1)", 0.7853981633974483, 1e-14},
    {"math.ceil(-1.2)", -1, 0},
    {"math.floor(-1.2)", -2, 0},
    {"math.round(-1.5)", -2, 0},
    {"math.sin(0)", 0, 0},
    {"math.cos(0)", 1, 0},
    {"math.tan(0)", 0, 0},
    {"math.log(1)", 0, 0},
    {"math.log10(100)", 2, 1e-14},
    {"math.exp(0)", 1, 0},
    {"math.pow(2, 5)", 32, 0},
    {"math.deg(math.rad(90))", 90, 1e-12},
    {"math.sqrt(81)", 9, 0},
    {"math.atan2(1, -1)", 2.356194490192345, 1e-14},
    {"math.hypot(3, 4)", 5, 0},
    {"math.fmod(-5, 2)", -1, 0},
    {"math.fmod(5, 0)", 0, 0},
    {"math.trunc(-1.8)", -1, 0},
    {"math.fract(-1.25)", 0.75, 0},
    {"math.sinh(0)", 0, 0},
    {"math.cosh(0)", 1, 0},
    {"math.tanh(0)", 0, 0},
    {"math.Sign(-2)", -1, 0},
    {"math.Min(4, 2)", 2, 0},
    {"math.Max(4, 2)", 4, 0},
    {"math.Clamp01(-3)", 0, 0},
    {"math.Clamp(4, 1, 3)", 3, 0},
    {"math.SmoothStep01(0.5)", 0.5, 0},
    {"math.Lerp(10, 20, 1.5)", 25, 0},
    {"math.Repeat(-5, 3)", 1, 0},
    {"math.PingPong(4, 3)", 2, 0},
    {"math.WrapAngle(7)", 0.7168146928204138, 1e-14},
    {"math.DeltaAngle(3, -3)", 0.2831853071795862, 1e-14},
    {"math.LerpAngle(3, -3, 0.5)", 3.141592653589793, 1e-14},
    {"math.MoveToward(1, 4, 2)", 3, 0},
    {"math.MoveTowardAngle(3, -3, 1)", -3, 1e-14},
    {"math.InverseLerp(10, 20, 25)", 1.5, 0},
    {"math.InverseLerp(2, 2, 9)", 0, 0},
    {"math.Remap(2, 0, 4, 10, 20)", 15, 0},
    {"math.SmoothStep(10, 20, 15)", 0.5, 0},
    {"math.Distance2(1, 2, 4, 6)", 5, 0},
    {"math.Dot3(Vector3(1, 2, 3), Vector3(4, 5, 6))", 32, 0},
    {"math.Length3(Vector3(0, 3, 4))", 5, 0},
    {"math.Distance3(Vector3(1, 2, 3), Vector3(1, 5, 7))", 5, 0},
    {"math.DistanceSquared3(Vector3(1, 2, 3), Vector3(1, 5, 7))", 25, 0}
};

const char* otherCases = R"nc(
export bool Vectors()
{
    Vector2 v2 = math.Vector2(1, 2);
    Vector3 a = math.Vector3(0, 0, 0);
    Vector3 b = Vector3(2, 4, 6);
    Vector4 v4 = math.Vector4(1, 2, 3, 4);
    Quaternion q = math.Quaternion(1, 0, 0, 0);
    Vector3 middle = math.Lerp3(a, b, 0.5);
    Vector3 unit = math.Normalize3(Vector3(0, 3, 4), Vector3(1, 0, 0));
    Vector3 fallback = math.Normalize3(a, Vector3(1, 0, 0));
    Vector3 cross = math.Cross3(Vector3(1, 0, 0), Vector3(0, 1, 0));
    Vector3 rotated = math.RotateVectorByQuat(q, b);
    Quaternion basis = math.quat_from_basis(Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1));
    Quaternion blended = math.quat_slerp(q, basis, 0.5);
    Vector3 after = math.RotateVectorByQuat(blended, b);
    return v2.y == 2 && v4.w == 4 && q.w == 1
        && middle.x == 1 && middle.y == 2 && middle.z == 3
        && math.abs(unit.y - 0.6) < 0.000001 && math.abs(unit.z - 0.8) < 0.000001
        && fallback.x == 1 && cross.z == 1
        && rotated.x == 2 && rotated.y == 4 && rotated.z == 6
        && after.x == 2 && after.y == 4 && after.z == 6
        && a.x == 0 && b.z == 6;
}
export bool Colors()
{
    int white = math.ColorRGB(1, 1, 1);
    int red = math.ColorRGB(1, 0, 0);
    int translucent = math.ColorARGB(0.5, 1, 0, 0);
    int clamped = math.ColorRGB(2, -1, 0);
    int invalid = math.ColorRGB(math.sqrt(-1), 0, 0);
    return white == -1 && red == -16776961 && translucent == 2130706687
        && clamped == red && invalid == -16777216;
}
export bool Random()
{
    math.srand(123);
    int first = math.rand();
    float second = math.Rand01();
    float third = math.RandRange(-4, 7);
    math.srand(123);
    if (first != math.rand() || second != math.Rand01() || third != math.RandRange(-4, 7)) return false;
    if (first < 0 || first > 32767 || second < 0 || second > 1 || third < -4 || third > 7) return false;
    return math.RandRange(3, 3) == 3 && math.Hash32(0) == 0 && math.Hash32(1) == 1753845952;
}
export bool Format()
{
    float value = 0.1 + 0.2;
    string text = math.tostr(value);
    return tofloat(text) == value && text != "0.3" && math.tostr(42) == "42";
}
const int FIXED_PLACES = 3;
export bool Fixed()
{
    return math.fixed(12.75, 1) == "12.8" && math.fixed(2.0, 1) == "2.0"
        && math.fixed(0.07, 1) == "0.1" && math.fixed(-0.05, 1) == "-0.1"
        && math.fixed(-0.04, 1) == "0.0" && math.fixed(-0.0, 2) == "0.00"
        && math.fixed(0.25, 1) == "0.2" && math.fixed(0.15, 1) == "0.1"
        && math.fixed(2.5, 0) == "2" && math.fixed(-2.5, 0) == "-2"
        && math.fixed(3.5, 0) == "4" && math.fixed(0.125, 2) == "0.12"
        && math.fixed(123456.789, 1) == "123456.8" && math.fixed(1.0/3.0, FIXED_PLACES) == "0.333"
        && math.fixed(0.0000001, 2) == "0.00" && math.fixed(1.0e20, 1) == "100000000000000000000.0"
        && math.fixed(42, 2) == "42.00" && math.fixed(math.Vector2(0.1, 0.0).x, 3) == "0.100"
        && math.fixed(0.0000000005, 9) == "0.000000001"
        && math.fixed(math.sqrt(-1), 2) == tostring(math.sqrt(-1))
        && math.fixed(math.pow(10, 400), 2) == tostring(math.pow(10, 400))
        && math.fixed(-math.pow(10, 400), 2) == tostring(-math.pow(10, 400));
}
export string FixedValue(float value, int decimals) { return math.fixed(value, decimals); }
export string FixedLiteralLow() { return math.fixed(1.0, -1); }
export string FixedLiteralHigh() { return math.fixed(1.0, 10); }
export string FixedLiteralWide() { return math.fixed(1.0, 4294967296); }
const int FIXED_INVALID_PLACES = 10;
export string FixedConstantHigh() { return math.fixed(1.0, FIXED_INVALID_PLACES); }
float cursor = 0;
float Next() { cursor += 1; return cursor; }
export bool EvaluationOrder()
{
    cursor = 0;
    Vector3 v = math.Vector3(Next(), Next(), Next());
    return v.x == 1 && v.y == 2 && v.z == 3 && cursor == 3;
}
)nc";

void Verify(NeoCore::Program& program, bool debug)
{
    for (std::size_t i = 0; i < sizeof(floatCases) / sizeof(floatCases[0]); ++i)
    {
        const FloatCase& test = floatCases[i];
        const NeoCore::Result result = program.Call("F" + std::to_string(i));
        const bool floating = result.ok() && !result.suspended() &&
            result.value.kind() == NeoCore::Scalar::Kind::Float;
        Check(floating, std::string(test.expression) + " returns float: " + result.error.message);
        if (floating)
            Check(std::fabs(result.value.asFloat() - test.expected) <= test.tolerance,
                std::string(test.expression) + " value");
    }
    const char* boolTests[] = {"Vectors", "Colors", "Random", "Format", "Fixed", "EvaluationOrder"};
    for (const char* name : boolTests)
    {
        const NeoCore::Result result = program.Call(name);
        Check(result.ok() && !result.suspended() && result.value.kind() == NeoCore::Scalar::Kind::Bool &&
            result.value.asBool(), std::string(name) + ": " + result.error.message);
    }
    const std::int64_t badDecimals[] = { -1, 10, 4294967296LL, (std::numeric_limits<std::int64_t>::max)() };
    for (std::int64_t decimals : badDecimals)
    {
        std::vector<NeoCore::Scalar> arguments;
        arguments.push_back(NeoCore::Scalar::Float(1.25));
        arguments.push_back(NeoCore::Scalar::Int(decimals));
        const NeoCore::Result result = program.Call("FixedValue", arguments);
        Check(!result.ok() && result.error.message.find("math.fixed decimals must be in [0, 9]") != std::string::npos,
            "fixed rejects runtime precision before narrowing");
        if (debug) Check(result.error.line > 0, "fixed runtime error has a source line");
    }
    const char* badConstants[] = { "FixedLiteralLow", "FixedLiteralHigh", "FixedLiteralWide", "FixedConstantHigh" };
    for (const char* name : badConstants)
    {
        const NeoCore::Result result = program.Call(name);
        Check(!result.ok() && result.error.message.find("math.fixed decimals must be in [0, 9]") != std::string::npos,
            "fixed checks constant and variable precisions at runtime");
        if (debug) Check(result.error.line > 0, "fixed constant runtime error has a source line");
    }
}

void Run(bool debug)
{
    std::string source = "import math;\n";
    for (std::size_t i = 0; i < sizeof(floatCases) / sizeof(floatCases[0]); ++i)
        source += "export float F" + std::to_string(i) + "() { return " + floatCases[i].expression + "; }\n";
    source += otherCases;
    NeoCore::Error error;
    std::unique_ptr<NeoCore::Program> program = NeoCore::Program::Compile(source, error, "math.nc", debug);
    Check(bool(program), "math compile: " + error.message);
    if (!program) return;
    Verify(*program, debug);
    std::vector<std::uint8_t> image;
    Check(program->SaveImage(image, error), "save math image: " + error.message);
    std::unique_ptr<NeoCore::Program> restored = NeoCore::Program::LoadImage(image, error);
    Check(bool(restored), "load math image: " + error.message);
    if (restored) Verify(*restored, debug);
}

void Rejected()
{
    const char* invalid[] = {
        "float x = math.Clamp(1, 0, 2);",
        "import math; float x = math.Clamp(1, 0);",
        "import math; float x = math.atan2(true, 1);",
        "import math; float x = math.RandRange(\"0\", 1);",
        "import math; int x = math.Clamp(1, 0, 2);",
        "import math; int x = math.Min(1, 2);",
        "import math; math.srand(1.5);",
        "import math; int x = math.Hash32(1.5);",
        "import math; int x = math.ColorRGB(1, 0);",
        "import math; int x = math.ColorARGB(true, 1, 0, 0);",
        "import math; float x = math.Dot3(Vector2(1, 2), Vector3());",
        "import math; Vector3 x = math.Normalize3(Vector3());",
        "import math; Quaternion x = math.quat_slerp(Vector4(), Quaternion(), 0.5);",
        "import math; Vector3? value = null; float x = math.Length3(value);",
        "import math; Vector3 x = math.Vector3(1, 2);",
        "Vector3 x = math.Vector3(1, 2, 3);",
        "import math;string s=math.fixed(1.0,2.0);",
        "import math;string s=math.fixed(\"1\",2);",
        "import math;int n=math.fixed(1.0,2);",
        "string s=math.fixed(1.0,2);"
    };
    for (const char* source : invalid)
    {
        NeoCore::Error error;
        Check(!NeoCore::Program::Compile(source, error, "invalid_math.nc") && !error.ok(), source);
    }
}

void FixedFormatting()
{
    struct FixedCase { double value; unsigned decimals; const char* expected; };
    const FixedCase cases[] = {
        { 0.25, 1, "0.2" }, { 0.15, 1, "0.1" }, { -0.05, 1, "-0.1" },
        { -0.04, 1, "0.0" }, { -0.0, 9, "0.000000000" }, { 3.5, 0, "4" },
        { -2.5, 0, "-2" }, { 0.125, 2, "0.12" }, { 1.0e20, 1, "100000000000000000000.0" },
        { (std::numeric_limits<double>::denorm_min)(), 9, "0.000000000" },
        { 9.999, 2, "10.00" }, { -9.999, 2, "-10.00" }
    };
    const int savedRounding = std::fegetround();
    const char* previousLocale = std::setlocale(LC_NUMERIC, nullptr);
    const std::string savedLocale = previousLocale ? previousLocale : "C";
    NeoCore::Error error;
    std::unique_ptr<NeoCore::Program> program = NeoCore::Program::Compile(
        "import math;export string F(float value,int decimals){return math.fixed(value,decimals);}", error);
    Check(bool(program), "fixed environment test compiles: " + error.message);
    // 각 환경을 바꾼 뒤에도 같은 결과를 내며 호출한 쪽의 설정을 건드리지 않아야 한다.
    const char* locales[] = { "C", "de-DE", "de_DE.UTF-8", "German_Germany.1252" };
    const int rounding[] = { FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO };
    for (const char* locale : locales)
    {
        if (!std::setlocale(LC_NUMERIC, locale)) continue;
        const std::string activeLocale = std::setlocale(LC_NUMERIC, nullptr);
        for (int mode : rounding)
        {
            if (std::fesetround(mode) != 0) continue;
            for (const FixedCase& sample : cases)
            {
                char text[NeoScript::FixedNumberBufferSize];
                Check(NeoScript::FormatFixedNumber(sample.value, sample.decimals, text) && text == std::string(sample.expected),
                    "fixed ignores locale and rounding mode");
                if (program)
                {
                    std::vector<NeoCore::Scalar> arguments;
                    arguments.push_back(NeoCore::Scalar::Float(sample.value));
                    arguments.push_back(NeoCore::Scalar::Int(sample.decimals));
                    const NeoCore::Result result = program->Call("F", arguments);
                    Check(result.ok() && result.value.kind() == NeoCore::Scalar::Kind::String &&
                        result.value.asString() == sample.expected, "fixed runtime ignores locale and rounding mode");
                }
            }
            Check(std::fegetround() == mode && std::string(std::setlocale(LC_NUMERIC, nullptr)) == activeLocale,
                "fixed leaves caller rounding and locale untouched");
        }
    }
    std::setlocale(LC_NUMERIC, "C");
    std::fesetround(FE_TONEAREST);
    // 독립적인 CRT 변환과 정상·비정규·최댓값을 포함한 임의 binary64 비트 패턴을 대조한다.
    std::uint64_t state = 0x7f4a7c159e3779b9ULL;
    for (unsigned i = 0; i < 10000; ++i)
    {
        state ^= state << 13; state ^= state >> 7; state ^= state << 17;
        if (((state >> 52) & 0x7ffu) == 0x7ffu) continue;
        double value;
        std::memcpy(&value, &state, sizeof(value));
        char actual[NeoScript::FixedNumberBufferSize], reference[NeoScript::FixedNumberBufferSize];
        const unsigned decimals = i % 10;
        std::snprintf(reference, sizeof(reference), "%.*f", static_cast<int>(decimals), value);
        std::string expected(reference);
        if (expected[0] == '-' && expected.find_first_of("123456789") == std::string::npos) expected.erase(0, 1);
        Check(NeoScript::FormatFixedNumber(value, decimals, actual) && actual == expected, "fixed agrees with nearest CRT formatting");
    }
    char text[NeoScript::FixedNumberBufferSize];
    const double maximum = (std::numeric_limits<double>::max)();
    Check(NeoScript::FormatFixedNumber(-maximum, 9, text) && std::strlen(text) == 320 &&
        std::string(text).substr(310) == ".000000000", "fixed holds the full negative binary64 maximum");
    Check(!NeoScript::FormatFixedNumber(std::numeric_limits<double>::infinity(), 0, text) &&
        !NeoScript::FormatFixedNumber(std::numeric_limits<double>::quiet_NaN(), 0, text), "fixed delegates non-finite spelling");
    std::setlocale(LC_NUMERIC, savedLocale.c_str());
    if (savedRounding != -1) std::fesetround(savedRounding);
}
}

int main()
{
    Run(false);
    Run(true);
    Rejected();
    FixedFormatting();
    std::printf("NeoCore math: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
