#pragma once

#include "NeoCore.h"
#include "NeoCoreTypes.h"
#include "NeoCoreHostBridge.h"

namespace NeoScript { class CallContext; }
namespace NeoCore
{
struct BoundaryAccess
{
    static Scalar Capture(Instance& owner, const NeoScript::CallContext& context, std::size_t index, TypeId type);
    static bool Return(Instance& owner, NeoScript::CallContext& context, TypeId type, const Scalar& value, std::string& error);
    static Scalar ParseJson(Instance& owner, TypeId type, const std::string& text, bool strictIntegers, std::string& error);
    static bool EncodeJson(Instance& owner, const NeoScript::CallContext& context, std::size_t index,
        TypeId type, bool sortKeys, std::string& text, std::string& error);
    // lenient: 빠진 구조체 필드는 기본값, 남는 필드는 무시. fallbackIndex >= 0 이면 실패 시 그 인자를 결과로 돌려주고
    // 이유를 인스턴스의 JsonError 에 남긴다.
    static bool DecodeJson(Instance& owner, NeoScript::CallContext& context, TypeId type,
        const std::string& text, bool lenient, int fallbackIndex, std::string& error);
    static const std::string& JsonError(Instance& owner);
    static bool Accepts(const TypeRegistry& types, TypeId type, const Scalar& value);
    static Scalar Wrap(NeoScript::IRuntime& runtime, const TypeRegistry& types, TypeId type,
        const std::shared_ptr<void>& lifetime, const std::shared_ptr<NeoCoreBackend::OwnedValue>& value);
    static std::shared_ptr<NeoCoreBackend::OwnedValue> Copy(NeoScript::IRuntime& runtime,
        const TypeRegistry& types, TypeId type, const std::shared_ptr<void>& lifetime, const Scalar& value);
};
}
