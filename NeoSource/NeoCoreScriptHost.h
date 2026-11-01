#pragma once

#include "NeoCoreHostSchema.h"
#include <functional>
#include <map>

namespace NeoCore
{
// 콘솔의 구현 모듈만 접근하는 등록 API와 타입별 ID 검사 바인딩이다. 일반 엔진 호스트에는 적용하지 않는다.
struct ScriptHostCompileConfig
{
    std::string apiGlobal;
    std::map<std::string, HostBindingId> members;
    std::map<TypeId, HostBindingId> validators;
    std::function<bool(const std::string&)> allowsSource;
};

std::string HostMemberPath(const HostSchema& schema, const HostBinding& binding);
}
