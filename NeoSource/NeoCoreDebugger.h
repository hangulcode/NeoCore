#pragma once

#include "NeoCore.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace NeoCore
{
class Runtime;

enum class DebugStopReason { Breakpoint, Step, Pause, Exception };
struct DebugLocation
{
    std::string sourceName;
    int line = 0;
};
struct DebugStackFrame
{
    int frameId = 0;
    std::string functionName;
    DebugLocation location;
};
struct DebugVariable
{
    std::string name;
    std::string type;
    std::string value;
    std::vector<DebugVariable> children;
};
struct IDebugListener
{
    virtual ~IDebugListener() = default;
    virtual void OnStopped(Instance& instance, const DebugLocation& location, DebugStopReason reason) = 0;
};

// 런타임이 소유한다. 리스너는 등록 기간 동안 살아 있어야 하며 같은 실행 스레드에서 호출된다.
// 중단 제어는 실행 모드만 바꾼다. 실제 진행은 Instance::Run/Call/Resume으로 수행한다.
class Debugger
{
public:
    ~Debugger();
    Debugger(const Debugger&) = delete;
    Debugger& operator=(const Debugger&) = delete;
    bool Attach(Instance& instance, Error& error);
    void Detach(Instance& instance);
    void SetListener(IDebugListener* listener);
    bool SetBreakpoints(Instance& instance, const std::vector<DebugLocation>& points, Error& error);
    bool Continue(Instance& instance);
    bool StepInto(Instance& instance);
    bool StepOver(Instance& instance);
    bool StepOut(Instance& instance);
    bool Pause(Instance& instance);
    bool IsPaused(Instance& instance) const;
    // Run과 명시 예산이 0인 Call/Resume에 적용한다. 양수 실행 예산은 이 기본값보다 우선한다.
    bool SetSliceBudget(Instance& instance, std::uint32_t budget);
    bool GetLocation(Instance& instance, DebugLocation& location) const;
    bool GetStackTrace(Instance& instance, std::vector<DebugStackFrame>& frames) const;
    bool GetFrameVariables(Instance& instance, int frameId, std::vector<DebugVariable>& variables) const;
    bool GetExecutableLocations(const Program& program, std::vector<DebugLocation>& locations) const;
    bool GetSourceFiles(const Program& program, std::vector<std::string>& files) const;
private:
    friend class Runtime;
    friend class Instance;
    void ClearStop(Instance& instance);
    explicit Debugger(Runtime& runtime);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
