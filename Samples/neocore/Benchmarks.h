#pragma once

#include "NeoCore.h"
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace NeoCoreBenchmarkDetail
{
enum Checksum
{
    VectorSum,
    IntegerSum,
    SquareRootSum,
    IncrementSum,
    FourAdds,
    LastRead
};

struct Case
{
    const char* name;
    const char* source;
    const char* function;
    int argument;
    int iterations;
    Checksum checksum;
};

inline const Case* Cases(std::size_t& count)
{
    // 기존 콘솔의 아홉 작업과 호출 횟수를 유지한다. 축소 검증도 같은 소스를 쓴다.
    static const Case cases[] = {
        {"vec_churn", R"nc(
list<float> V3(float x, float y, float z) { return [x, y, z]; }
list<float> AddV3(list<float> a, list<float> b)
{
    return [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
}
export float VecChurn(int n)
{
    float sum = 0.0;
    for (int i in 0, n, 1)
    {
        list<float> a = V3(i, i + 1.0, i + 2.0);
        list<float> b = V3(i * 0.5, i * 0.25, i * 0.125);
        list<float> c = AddV3(a, b);
        sum = sum + c[0] + c[1] + c[2];
    }
    return sum;
}
)nc", "VecChurn", 200000, 5, VectorSum},
        {"vec_churn_vt", R"nc(
Vector3 V3(float x, float y, float z) { return Vector3(x, y, z); }
Vector3 AddV3(Vector3 a, Vector3 b) { return a + b; }
export float VecChurnVT(int n)
{
    float sum = 0.0;
    for (int i in 0, n, 1)
    {
        Vector3 a = V3(i, i + 1.0, i + 2.0);
        Vector3 b = V3(i * 0.5, i * 0.25, i * 0.125);
        Vector3 c = AddV3(a, b);
        sum = sum + c.x + c.y + c.z;
    }
    return sum;
}
)nc", "VecChurnVT", 200000, 5, VectorSum},
        {"loop_sum", R"nc(
export float LoopSum(int n)
{
    float sum = 0.0;
    for (int i in 0, n, 1) sum += i;
    return sum;
}
)nc", "LoopSum", 200000, 5, IntegerSum},
        {"math_sqrt", R"nc(
import math;
export float MathSqrt(int n)
{
    float sum = 0.0;
    for (int i in 0, n, 1) sum += math.sqrt(i);
    return sum;
}
)nc", "MathSqrt", 100000, 3, SquareRootSum},
        {"call_loop", R"nc(
int inc(int x) { return x + 1; }
export float CallLoop(int n)
{
    float sum = 0.0;
    for (int i in 0, n, 1) sum += inc(i);
    return sum;
}
)nc", "CallLoop", 100000, 5, IncrementSum},
        {"map_loop", R"nc(
export float MapLoop(int n)
{
    map<int, int> m = {};
    for (int i in 0, n, 1) m[i] = i;
    float sum = 0.0;
    foreach (int k, int v in m) sum += v;
    return sum;
}
)nc", "MapLoop", 30000, 3, IntegerSum},
        {"map_str_read", R"nc(
export float MapStringRead(int n)
{
    map<string, int> m = {};
    m["alpha"] = 1;
    m["bravo"] = 2;
    m["charlie"] = 3;
    m["delta"] = 4;
    m["echo"] = 5;
    m["foxtrot"] = 6;
    m["golf"] = 7;
    m["hotel"] = 8;
    float sum = 0.0;
    for (int i in 0, n, 1)
    {
        sum = sum + m.get("alpha", 0);
        sum = sum + m.get("hotel", 0);
        sum = sum + m.get("charlie", 0);
        sum = sum + m.get("foxtrot", 0);
    }
    return sum;
}
)nc", "MapStringRead", 50000, 5, FourAdds},
        {"add4_only", R"nc(
export float Add4Only(int n)
{
    float sum = 0.0;
    for (int i in 0, n, 1)
    {
        sum = sum + 1;
        sum = sum + 8;
        sum = sum + 3;
        sum = sum + 6;
    }
    return sum;
}
)nc", "Add4Only", 50000, 5, FourAdds},
        {"read4_only", R"nc(
export int Read4Only(int n)
{
    map<string, int> m = {};
    m["alpha"] = 1;
    m["bravo"] = 2;
    m["charlie"] = 3;
    m["delta"] = 4;
    m["echo"] = 5;
    m["foxtrot"] = 6;
    m["golf"] = 7;
    m["hotel"] = 8;
    int t = 0;
    for (int i in 0, n, 1)
    {
        t = m.get("alpha", 0);
        t = m.get("hotel", 0);
        t = m.get("charlie", 0);
        t = m.get("foxtrot", 0);
    }
    return t;
}
)nc", "Read4Only", 50000, 5, LastRead}
    };
    count = sizeof(cases) / sizeof(cases[0]);
    return cases;
}

inline double Expected(const Case& test, int argument)
{
    const double n = static_cast<double>(argument);
    switch (test.checksum)
    {
    case VectorSum: return 3.875 * n * (n - 1.0) / 2.0 + 3.0 * n;
    case IntegerSum: return n * (n - 1.0) / 2.0;
    case SquareRootSum:
    {
        double sum = 0.0;
        for (int i = 0; i < argument; ++i) sum += std::sqrt(static_cast<double>(i));
        return sum;
    }
    case IncrementSum: return n * (n + 1.0) / 2.0;
    case FourAdds: return 18.0 * n;
    case LastRead: return argument == 0 ? 0.0 : 6.0;
    default: return 0.0;
    }
}

inline bool Completed(const Case& test, const NeoCore::Result& result, const char* phase)
{
    if (result.ok() && !result.suspended()) return true;
    std::fprintf(stderr, "[bench] %s %s failed: %s:%d:%d: %s\n", test.name, phase,
        result.error.sourceName.c_str(), result.error.line, result.error.column,
        result.suspended() ? "unexpected suspension" : result.error.message.c_str());
    return false;
}

inline bool ReadResult(const Case& test, const NeoCore::Result& result, double& value)
{
    if (!Completed(test, result, "call")) return false;
    const NeoCore::Scalar::Kind kind = test.checksum == LastRead
        ? NeoCore::Scalar::Kind::Int : NeoCore::Scalar::Kind::Float;
    if (result.value.kind() != kind)
    {
        std::fprintf(stderr, "[bench] %s returned the wrong scalar type\n", test.name);
        return false;
    }
    value = kind == NeoCore::Scalar::Kind::Int
        ? static_cast<double>(result.value.asInt()) : result.value.asFloat();
    return true;
}

inline bool CheckResult(const Case& test, int argument, double actual, double expected)
{
    const double tolerance = test.checksum == SquareRootSum
        ? std::fabs(expected) * 1.0e-12 + 1.0e-9 : 0.0;
    if (std::isfinite(actual) && std::fabs(actual - expected) <= tolerance) return true;
    std::fprintf(stderr, "[bench] %s checksum failed: arg=%d expected=%.17g actual=%.17g\n",
        test.name, argument, expected, actual);
    return false;
}

inline std::unique_ptr<NeoCore::Program> Compile(const Case& test, bool debug)
{
    NeoCore::Error error;
    std::unique_ptr<NeoCore::Program> program = NeoCore::Program::Compile(test.source, error,
        std::string("bench/") + test.name + ".nc", debug);
    if (!program)
        std::fprintf(stderr, "[bench] %s compile failed: %s:%d:%d: %s\n", test.name,
            error.sourceName.c_str(), error.line, error.column, error.message.c_str());
    return program;
}

inline int RunTimed(const Case& test, bool debug)
{
    typedef std::chrono::steady_clock Clock;
    // 기준값 계산과 검증은 측정 구간 밖에서 처리한다.
    const double expected = Expected(test, test.argument);
    std::vector<double> values(static_cast<std::size_t>(test.iterations));
    const Clock::time_point compileStart = Clock::now();
    std::unique_ptr<NeoCore::Program> program = Compile(test, debug);
    if (!program || !Completed(test, program->Run(), "initialization")) return 1;
    const Clock::time_point compileEnd = Clock::now();

    const Clock::time_point runStart = Clock::now();
    for (int i = 0; i < test.iterations; ++i)
    {
        const NeoCore::Result result = program->Call(test.function, {NeoCore::Scalar::Int(test.argument)});
        if (!ReadResult(test, result, values[static_cast<std::size_t>(i)])) return 1;
    }
    const Clock::time_point runEnd = Clock::now();
    for (double value : values)
        if (!CheckResult(test, test.argument, value, expected)) return 1;

    const double compileMs = std::chrono::duration<double, std::milli>(compileEnd - compileStart).count();
    const double runMs = std::chrono::duration<double, std::milli>(runEnd - runStart).count();
    std::printf("[bench] %-14s compile=%8.3f ms run=%8.3f ms iter=%d arg=%d result=%g\n",
        test.name, compileMs, runMs, test.iterations, test.argument, values.back());
    return 0;
}
}

inline int RunBenchmarks(bool debug)
{
    std::size_t count = 0;
    const NeoCoreBenchmarkDetail::Case* cases = NeoCoreBenchmarkDetail::Cases(count);
    std::printf("NeoCore console benchmarks (%s).\n", debug ? "debug" : "non-debug");
    for (std::size_t i = 0; i < count; ++i)
        if (NeoCoreBenchmarkDetail::RunTimed(cases[i], debug) != 0) return 1;
    return 0;
}

inline int RunBenchmarkChecks(bool debug)
{
    std::size_t count = 0;
    const NeoCoreBenchmarkDetail::Case* cases = NeoCoreBenchmarkDetail::Cases(count);
    const int arguments[] = {0, 1, 37};
    // 타이머 없이 빈 입력, 한 원소, 작은 반복과 재호출을 모두 확인한다.
    for (std::size_t i = 0; i < count; ++i)
    {
        const NeoCoreBenchmarkDetail::Case& test = cases[i];
        std::unique_ptr<NeoCore::Program> program = NeoCoreBenchmarkDetail::Compile(test, debug);
        if (!program || !NeoCoreBenchmarkDetail::Completed(test, program->Run(), "initialization")) return 1;
        for (int argument : arguments)
        {
            const double expected = NeoCoreBenchmarkDetail::Expected(test, argument);
            for (int repeat = 0; repeat < 2; ++repeat)
            {
                const NeoCore::Result result = program->Call(test.function, {NeoCore::Scalar::Int(argument)});
                double actual = 0.0;
                if (!NeoCoreBenchmarkDetail::ReadResult(test, result, actual) ||
                    !NeoCoreBenchmarkDetail::CheckResult(test, argument, actual, expected)) return 1;
            }
        }
        std::printf("[bench-check] %-14s passed (n=0,1,37; two calls each)\n", test.name);
    }
    std::printf("Benchmark checks passed: %u cases, %u calls; no timing.\n",
        static_cast<unsigned>(count), static_cast<unsigned>(count * 6));
    return 0;
}
