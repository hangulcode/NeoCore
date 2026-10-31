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
    static bool Accepts(const TypeRegistry& types, TypeId type, const Scalar& value);
    static Scalar Wrap(NeoScript::IRuntime& runtime, const TypeRegistry& types, TypeId type,
        const std::shared_ptr<void>& lifetime, const std::shared_ptr<NeoCoreBackend::OwnedValue>& value);
    static std::shared_ptr<NeoCoreBackend::OwnedValue> Copy(NeoScript::IRuntime& runtime,
        const TypeRegistry& types, TypeId type, const std::shared_ptr<void>& lifetime, const Scalar& value);
};
}
