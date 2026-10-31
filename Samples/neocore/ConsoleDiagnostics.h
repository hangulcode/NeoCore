#pragma once

#include "NeoCore.h"
#include <chrono>
#include <cstdio>
#include <thread>

inline void PrintAllocationStats()
{
    const NeoCore::AllocationStats stats = NeoCore::GetAllocationStats();
    std::printf("[ALLOC] process-global str=%d map=%d list=%d array=%d buffer=%lld bytes "
        "set=%d cor=%d mod=%d async=%d closure=%d vec=%d pool=%lld bytes\n",
        stats.strings, stats.maps, stats.lists, stats.arrays, static_cast<long long>(stats.bufferBytes),
        stats.sets, stats.coroutines, stats.modules, stats.asyncs, stats.closures, stats.vectors,
        static_cast<long long>(stats.poolBytes));
    std::printf("[ALLOC] stringIdle=%lld bytes\n", static_cast<long long>(stats.stringIdleBytes));
}

namespace NeoCoreConsoleDiagnostics
{
inline void Error(const NeoCore::Error& error)
{
    std::fprintf(stderr, "[pooltrim] %s:%d:%d: %s\n", error.sourceName.c_str(),
        error.line, error.column, error.message.c_str());
}

inline std::int64_t PrintPool(const char* stage)
{
    const NeoCore::AllocationStats stats = NeoCore::GetAllocationStats();
    std::printf("[pooltrim] %-18s pool=%lld bytes stringIdle=%lld bytes (str=%d list=%d)\n",
        stage, static_cast<long long>(stats.poolBytes), static_cast<long long>(stats.stringIdleBytes),
        stats.strings, stats.lists);
    return stats.poolBytes;
}

inline bool Call(NeoCore::Program& program, const char* name, std::int64_t expected,
    const std::vector<NeoCore::Scalar>& arguments = {})
{
    const NeoCore::Result result = program.Call(name, arguments);
    if (!result.ok()) { Error(result.error); return false; }
    if (result.suspended() || result.value.kind() != NeoCore::Scalar::Kind::Int || result.value.asInt() != expected)
    {
        std::fprintf(stderr, "[pooltrim] unexpected result from %s\n", name);
        return false;
    }
    return true;
}

inline int RunPoolTrim(bool debug, bool measure)
{
    // 정적 구조체와 리스트로 값을 보유한 뒤 명시적으로 비워 풀 페이지 회수를 검사한다.
    const char* source =
        "struct Record { string path; int id; };\n"
        "list<Record> keep = [];\n"
        "export int Build(int count) {\n"
        "  for (int i in 0, count)\n"
        "    keep.append(Record(\"map/area/segment/\" .. tostring(i) .. \"/mesh.bin\", i));\n"
        "  return keep.len();\n"
        "}\n"
        "export int Drop() { keep = []; return keep.len(); }\n";
    const std::int64_t count = measure ? 30000 : 2048;
    const std::vector<NeoCore::Scalar> buildArguments{NeoCore::Scalar::Int(count)};
    NeoCore::Error error;
    std::unique_ptr<NeoCore::Program> program = NeoCore::Program::Compile(source, error, "pooltrim.nc", debug);
    if (!program) { Error(error); return 1; }
    PrintPool("start");
    if (!Call(*program, "Build", count, buildArguments)) return 1;
    const std::int64_t peak = PrintPool("after build");
    if (!Call(*program, "Drop", 0)) return 1;
    const std::int64_t dropped = PrintPool("after drop");

    program->SetEmptyPageHoldSeconds(5.0f);
    const std::int64_t freed = program->TrimMemory(true);
    const std::int64_t trimmed = PrintPool("after force trim");
    std::printf("[pooltrim] forced reclaim=%lld bytes\n", static_cast<long long>(freed));

    if (!Call(*program, "Build", count, buildArguments) || !Call(*program, "Drop", 0)) return 1;
    program->SetEmptyPageHoldSeconds(measure ? 1.0f : 0.01f);
    // 보유 시간은 첫 회수 요청이 빈 페이지를 발견한 시각부터 시작한다.
    const std::int64_t tooEarly = program->TrimMemory(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(measure ? 1100 : 20));
    const std::int64_t afterHold = program->TrimMemory(false);
    std::printf("[pooltrim] before hold=%lld bytes after hold=%lld bytes\n",
        static_cast<long long>(tooEarly), static_cast<long long>(afterHold));

    if (!Call(*program, "Build", count, buildArguments) || !Call(*program, "Drop", 0)) return 1;
    program->SetEmptyPageHoldSeconds(0.0f);
    program->SetTrimPagesPerCall(4);
    const std::int64_t incrementalStart = PrintPool("before incremental");
    std::int64_t worstBytes = 0;
    double totalUs = 0.0;
    double worstUs = 0.0;
    int calls = 0;
    for (; calls < 100000; )
    {
        std::chrono::steady_clock::time_point started;
        if (measure) started = std::chrono::steady_clock::now();
        const std::int64_t released = program->TrimMemory(false);
        ++calls;
        if (measure)
        {
            const double elapsed = std::chrono::duration<double, std::micro>(
                std::chrono::steady_clock::now() - started).count();
            totalUs += elapsed;
            if (elapsed > worstUs) worstUs = elapsed;
        }
        if (released > worstBytes) worstBytes = released;
        if (released == 0) break;
    }
    const std::int64_t incrementalEnd = PrintPool("after incremental");
    // 현재 풀의 최대 페이지 크기는 16 KiB 이내이며, 호출당 네 장으로 제한했다.
    const bool budgetKept = worstBytes <= 4 * 16 * 1024;
    std::printf("[pooltrim] incremental calls=%d maxReleased=%lld bytes budget=%s\n",
        calls, static_cast<long long>(worstBytes), budgetKept ? "OK" : "FAIL");

    // 자동 기능 검사는 실행 시간을 측정하거나 유휴 호출 벤치를 돌리지 않는다.
    if (measure)
    {
        std::printf("[pooltrim] incremental max=%.1f us avg=%.2f us\n", worstUs, totalUs / calls);
        double idleUs = 0.0;
        for (int i = 0; i < 1000; ++i)
        {
            const auto started = std::chrono::steady_clock::now();
            program->TrimMemory(false);
            idleUs += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count();
        }
        std::printf("[pooltrim] idle average=%.4f us\n", idleUs / 1000.0);
    }
    const bool passed = peak > 0 && freed > 0 && trimmed < dropped && tooEarly == 0 && afterHold > 0 &&
        budgetKept && incrementalEnd < incrementalStart && calls < 100000;
    std::printf("[%s] %s\n", measure ? "pooltrim" : "pooltrim-check", passed ? "passed" : "FAILED");
    return passed ? 0 : 1;
}
}

inline int RunPoolTrim(bool debug)
{
    return NeoCoreConsoleDiagnostics::RunPoolTrim(debug, true);
}

inline int RunPoolTrimChecks(bool debug)
{
    return NeoCoreConsoleDiagnostics::RunPoolTrim(debug, false);
}
