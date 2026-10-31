#pragma once

#include "NeoCoreHost.h"

namespace NeoScript { class IRuntime; }

namespace NeoCore
{

// 기존 VM의 호출 프레임을 쓰는 전환용 연결이다. 공개 호스트 API에는 백엔드 타입을 내보내지 않는다.
class HostRuntime
{
public:
    explicit HostRuntime(const HostBindings& bindings);
    ~HostRuntime();
    const HostSchema& Schema() const;
    bool Link(NeoScript::IRuntime& runtime, const std::string& prefix, std::string& error);
    void SetProgram(Program* program);
    bool SetGlobal(const std::string& name, ObjectHandle object, std::string& error);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
