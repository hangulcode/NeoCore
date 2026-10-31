#include "NeoCore.h"

#include <cmath>
#include <cstdio>
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
float cursor = 0;
float Next() { cursor += 1; return cursor; }
export bool EvaluationOrder()
{
    cursor = 0;
    Vector3 v = math.Vector3(Next(), Next(), Next());
    return v.x == 1 && v.y == 2 && v.z == 3 && cursor == 3;
}
)nc";

void Verify(NeoCore::Program& program)
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
    const char* boolTests[] = {"Vectors", "Colors", "Random", "Format", "EvaluationOrder"};
    for (const char* name : boolTests)
    {
        const NeoCore::Result result = program.Call(name);
        Check(result.ok() && !result.suspended() && result.value.kind() == NeoCore::Scalar::Kind::Bool &&
            result.value.asBool(), std::string(name) + ": " + result.error.message);
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
    Verify(*program);
    std::vector<std::uint8_t> image;
    Check(program->SaveImage(image, error), "save math image: " + error.message);
    std::unique_ptr<NeoCore::Program> restored = NeoCore::Program::LoadImage(image, error);
    Check(bool(restored), "load math image: " + error.message);
    if (restored) Verify(*restored);
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
        "Vector3 x = math.Vector3(1, 2, 3);"
    };
    for (const char* source : invalid)
    {
        NeoCore::Error error;
        Check(!NeoCore::Program::Compile(source, error, "invalid_math.nc") && !error.ok(), source);
    }
}
}

int main()
{
    Run(false);
    Run(true);
    Rejected();
    std::printf("NeoCore math: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
