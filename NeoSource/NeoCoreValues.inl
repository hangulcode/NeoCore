// NeoScriptShim.cpp의 내부 구현 타입이 완성된 뒤 포함한다.
#include "NeoJson.h"

namespace NeoCoreBackend
{
struct ValueAccess
{
    static NeoScript::CNeoVM* VM(NeoScript::IRuntime& runtime)
    { return static_cast<NeoScript::RuntimeImpl&>(runtime).m_vm; }
    static void SetVector(NeoScript::BoundaryValue& value, int count, const float* components)
    {
        if (count == 2) value.SetInlineVec(components[0], components[1]);
        else value.SetVecType(count);
    }
    static bool WorkerAlive(NeoScript::RuntimeImpl& runtime, std::uint32_t id)
    {
        for (const auto& worker : runtime.m_workerHandle)
            if (worker.first->GetWorkerID() == id) return true;
        return false;
    }
};

struct OwnedValue
{
    std::shared_ptr<void> lifetime;
    NeoScript::RuntimeImpl* runtime;
    NeoScript::CNeoVM* vm;
    NeoScript::BoundaryValue value;
    OwnedValue(NeoScript::IRuntime& owner, const std::shared_ptr<void>& keep)
        : lifetime(keep), runtime(static_cast<NeoScript::RuntimeImpl*>(&owner)), vm(ValueAccess::VM(owner)) {}
    ~OwnedValue() { vm->Var_Release(&value); }
};

namespace
{
ValuePtr CopyOwned(const ValuePtr& owner, NeoScript::ValueRef value)
{
    if (!owner || !value) return ValuePtr();
    ValuePtr copy(new OwnedValue(*owner->runtime, owner->lifetime));
    copy->vm->Move(&copy->value, value);
    return copy;
}

ValuePtr CaptureOwned(NeoScript::InstanceRec* instance, NeoScript::ValueRef value,
    const std::shared_ptr<void>& lifetime)
{
    if (!instance || !value || !lifetime) return ValuePtr();
    ValuePtr result(new OwnedValue(*instance->runtime, lifetime));
    result->vm->Move(&result->value, value);
    return result;
}

bool SameDomain(const ValuePtr& left, const ValuePtr& right)
{ return left && right && left->runtime == right->runtime; }

bool UsableFunction(const ValuePtr& value, NeoScript::INeoVMWorker* target = nullptr)
{
    using namespace NeoScript;
    if (!value) return false;
    if (value->value.GetType() < VAR_MAP) return true;
    // 반복 탐색으로 깊은 컨테이너와 순환 그래프에서도 C++ 스택을 늘리지 않는다.
    std::vector<ValueRef> pending(1, &value->value);
    std::unordered_set<const void*> visited;
    while (!pending.empty())
    {
        ValueRef current = pending.back(); pending.pop_back();
        switch (current.GetType())
        {
        case VAR_MAP:
            if (current->_tbl->_cycleState._mayContainContainerChild && visited.insert(current->_tbl).second)
                for (int i = 0; i < current->_tbl->_BucketCapa; ++i)
                    if (IsMapNodeUsed(current->_tbl->_Bucket[i]))
                    {
                        pending.push_back(&current->_tbl->_Bucket[i].data->key);
                        pending.push_back(&current->_tbl->_Bucket[i].data->value);
                    }
            break;
        case VAR_LIST:
            if (current->_lst->_cycleState._mayContainContainerChild && visited.insert(current->_lst).second)
                for (int i = 0; i < current->_lst->GetCount(); ++i) pending.push_back(current->_lst->GetValue(i));
            break;
        case VAR_SET:
            if (current->_set->_cycleState._mayContainContainerChild && visited.insert(current->_set).second)
                for (int i = 0; i < current->_set->_BucketCapa; ++i)
                    if (IsSetNodeUsed(current->_set->_Bucket[i])) pending.push_back(&current->_set->_Bucket[i].data->key);
            break;
        case VAR_CLOSURE:
            if ((target && target->GetWorkerID() != current->_closure->_ownerWorkerId) ||
                !ValueAccess::WorkerAlive(*value->runtime, current->_closure->_ownerWorkerId)) return false;
            if (visited.insert(current->_closure).second)
                for (size_t i = 0; i < current->_closure->_captures.size(); ++i) pending.push_back(current->_closure->Capture(i));
            break;
        default: break;
        }
    }
    return true;
}
}

bool AppendNull(NeoScript::Invocation& invocation)
{
    if (!invocation) return false;
    NeoScript::InstanceRec* instance = static_cast<NeoScript::InstanceRec*>(NeoScript::NeoScriptInternal::invImpl(invocation));
    instance->callArgs.emplace_back();
    return true;
}

bool AppendValue(NeoScript::Invocation& invocation, const ValuePtr& value)
{
    if (!invocation || !value) return false;
    NeoScript::InstanceRec* instance = static_cast<NeoScript::InstanceRec*>(NeoScript::NeoScriptInternal::invImpl(invocation));
    if (instance->runtime != value->runtime || !UsableFunction(value, instance->worker)) return false;
    instance->callArgs.emplace_back();
    value->vm->Move(&instance->callArgs.back(), &value->value);
    return true;
}

bool CanPassValue(NeoScript::IRuntime& runtime, NeoScript::InstanceHandle instance, const ValuePtr& value)
{
    if (!value || value->runtime != &runtime) return false;
    NeoScript::InstanceRec* record = value->runtime->resolveInstance(instance);
    return record && record->worker && UsableFunction(value, record->worker);
}

ValuePtr CaptureArgument(const NeoScript::CallContext& context, std::size_t index, const std::shared_ptr<void>& lifetime)
{
    NeoScript::CallContextImpl* impl = NeoScript::NeoScriptInternal::ctxImpl(context);
    if (!impl || !impl->runtime) return ValuePtr();
    return CaptureOwned(static_cast<NeoScript::RuntimeImpl*>(impl->runtime)->instanceForWorker(impl->worker),
        NeoScript::CtxArgVar(impl, index), lifetime);
}

ValuePtr CaptureResult(const NeoScript::Invocation& invocation, const std::shared_ptr<void>& lifetime)
{
    void* raw = NeoScript::NeoScriptInternal::invImpl(invocation);
    return CaptureOwned(static_cast<NeoScript::InstanceRec*>(raw),
        NeoScript::InvReturn(raw, NeoScript::NeoScriptInternal::invSequence(invocation)), lifetime);
}

bool ReturnValue(NeoScript::CallContext& context, const ValuePtr& value)
{
    NeoScript::CallContextImpl* impl = NeoScript::NeoScriptInternal::ctxImpl(context);
    if (!impl || !value || impl->runtime != value->runtime || !UsableFunction(value, impl->worker)) return false;
    NeoScript::ValueRef destination = NeoScript::CtxRetVar(impl);
    if (!destination) return false;
    value->vm->Move(destination, &value->value);
    return true;
}

ValuePtr MakeValue(NeoScript::IRuntime& runtime, const ValueData& data, const std::shared_ptr<void>& lifetime)
{
    using namespace NeoScript;
    if (!lifetime) return ValuePtr();
    ValuePtr result(new OwnedValue(runtime, lifetime));
    BoundaryValue& value = result->value;
    switch (data.kind)
    {
    case ValueKind::Null: break;
    case ValueKind::Int: value = BoundaryValue(VAR_INT); value._int = data.integer; break;
    case ValueKind::Float: value = BoundaryValue(VAR_FLOAT); value._float = data.number; break;
    case ValueKind::Bool: value = BoundaryValue(VAR_BOOL); value._bl = data.integer != 0; break;
    case ValueKind::String: result->vm->Var_SetStringA(&value, data.text); break;
    case ValueKind::Vector2: ValueAccess::SetVector(value, 2, data.vector); break;
    case ValueKind::Vector3:
    case ValueKind::Vector4:
        value._vec = result->vm->VecAlloc();
        ValueAccess::SetVector(value, data.kind == ValueKind::Vector3 ? 3 : 4, data.vector);
        value._vec->_refCount = 1;
        std::copy(data.vector, data.vector + 4, value._vec->v);
        break;
    default: return ValuePtr();
    }
    return result;
}

ValuePtr MakeCollection(NeoScript::IRuntime& runtime, ValueKind kind, ArrayStorage storage,
    const std::shared_ptr<void>& lifetime)
{
    using namespace NeoScript;
    if (!lifetime) return ValuePtr();
    ValuePtr result(new OwnedValue(runtime, lifetime));
    BoundaryValue& value = result->value;
    switch (kind)
    {
    case ValueKind::List:
        { ListInfo* list = result->vm->ListAlloc(); value = BoundaryValue(VAR_LIST); value._lst = list; ++list->_refCount; break; }
    case ValueKind::Map:
        { MapInfo* map = result->vm->TableAlloc(); value = BoundaryValue(VAR_MAP); value._tbl = map; ++map->_refCount; break; }
    case ValueKind::Set:
        { SetInfo* set = result->vm->SetAlloc(); value = BoundaryValue(VAR_SET); value._set = set; ++set->_refCount; break; }
    case ValueKind::Array:
    {
        ValueData initial;
        NeoArrayElementType element;
        switch (storage)
        {
        case ArrayStorage::Bool: element = NeoArrayElementType::Bool; initial.kind = ValueKind::Bool; break;
        case ArrayStorage::Int64: element = NeoArrayElementType::Int; initial.kind = ValueKind::Int; break;
        case ArrayStorage::Float64: element = NeoArrayElementType::Float; initial.kind = ValueKind::Float; break;
        case ArrayStorage::Vector2: element = NeoArrayElementType::Vector2; initial.kind = ValueKind::Vector2; break;
        case ArrayStorage::Vector3: element = NeoArrayElementType::Vector3; initial.kind = ValueKind::Vector3; break;
        case ArrayStorage::Vector4: element = NeoArrayElementType::Vector4; initial.kind = ValueKind::Vector4; break;
        case ArrayStorage::Int32: element = NeoArrayElementType::Int32; initial.kind = ValueKind::Int; break;
        case ArrayStorage::Float32: element = NeoArrayElementType::Float32; initial.kind = ValueKind::Float; break;
        default: return ValuePtr();
        }
        ValuePtr seed = MakeValue(runtime, initial, lifetime);
        ArrayInfo* array = result->vm->ArrayAlloc(element, 0, seed->value);
        if (!array) return ValuePtr();
        value = BoundaryValue(VAR_ARRAY); value._array = array; ++array->_refCount;
        break;
    }
    default: return ValuePtr();
    }
    return result;
}

bool ReadValue(const ValuePtr& owner, ValueData& data)
{
    using namespace NeoScript;
    data = ValueData();
    if (!owner) return false;
    const BoundaryValue& value = owner->value;
    switch (value.GetType())
    {
    case VAR_INT: data.kind = ValueKind::Int; data.integer = value._int; break;
    case VAR_FLOAT: data.kind = ValueKind::Float; data.number = value._float; break;
    case VAR_BOOL: data.kind = ValueKind::Bool; data.integer = value._bl ? 1 : 0; break;
    case VAR_NONE: break;
    case VAR_FUN: data.kind = ValueKind::Function; break;
    case VAR_VEC:
        data.kind = ValueKind::Vector2;
        value.CopyVectorComponents(data.vector); break;
    case VAR_STRING: data.kind = ValueKind::String; data.text = value._str->_str; break;
    case VAR_VEC3:
    case VAR_VEC4:
        data.kind = value.VectorComponentCount() == 3 ? ValueKind::Vector3 : ValueKind::Vector4;
        value.CopyVectorComponents(data.vector); break;
    case VAR_FP_NATIVE: data.kind = ValueKind::Object; break;
    case VAR_ARRAY: data.kind = ValueKind::Array; break;
    case VAR_MAP: data.kind = ValueKind::Map; break;
    case VAR_LIST: data.kind = ValueKind::List; break;
    case VAR_SET: data.kind = ValueKind::Set; break;
    case VAR_CLOSURE: data.kind = ValueKind::Function; break;
    default: return false;
    }
    return true;
}

std::size_t ValueSize(const ValuePtr& owner)
{
    using namespace NeoScript;
    if (!owner) return 0;
    const BoundaryValue& value = owner->value;
    switch (value.GetType())
    {
    case VAR_ARRAY: return value._array->_count;
    case VAR_MAP: return value._tbl->GetCount();
    case VAR_LIST: return value._lst->GetCount();
    case VAR_SET: return value._set->GetCount();
    default: return 0;
    }
}

ValuePtr ValueAt(const ValuePtr& owner, std::size_t index)
{
    using namespace NeoScript;
    if (!owner || index >= ValueSize(owner)) return ValuePtr();
    if (owner->value.GetType() == VAR_LIST) return CopyOwned(owner, owner->value._lst->GetValue(static_cast<int>(index)));
    if (owner->value.GetType() == VAR_MAP)
    {
        MapInfo* map = owner->value._tbl;
        for (int slot = 0; slot < map->_BucketCapa; ++slot)
            if (IsMapNodeUsed(map->_Bucket[slot]) && index-- == 0) return CopyOwned(owner, &map->_Bucket[slot].data->value);
        return ValuePtr();
    }
    if (owner->value.GetType() == VAR_SET) return ValueKeyAt(owner, index);
    if (owner->value.GetType() != VAR_ARRAY) return ValuePtr();
    ArrayInfo* array = owner->value._array;
    ValueData data;
    switch (array->_elementType)
    {
    case NeoArrayElementType::Bool: data.kind = ValueKind::Bool; data.integer = array->GetBool(static_cast<int>(index)); break;
    case NeoArrayElementType::Int: data.kind = ValueKind::Int; data.integer = array->IntData()[index]; break;
    case NeoArrayElementType::Float: data.kind = ValueKind::Float; data.number = array->FloatData()[index]; break;
    case NeoArrayElementType::Vector2:
    case NeoArrayElementType::Vector3:
    case NeoArrayElementType::Vector4: return CopyOwned(owner, array->VectorAt(static_cast<int>(index)));
    case NeoArrayElementType::Int32: data.kind = ValueKind::Int; data.integer = array->Int32Data()[index]; break;
    case NeoArrayElementType::Float32: data.kind = ValueKind::Float; data.number = array->Float32Data()[index]; break;
    default: return ValuePtr();
    }
    return MakeValue(*owner->runtime, data, owner->lifetime);
}

ValuePtr ValueKeyAt(const ValuePtr& owner, std::size_t index)
{
    using namespace NeoScript;
    if (!owner || index >= ValueSize(owner)) return ValuePtr();
    // 키를 복사해서 돌려주므로 다음 호출에서 컨테이너가 바뀌어도 노드 포인터를 보관하지 않는다.
    if (owner->value.GetType() == VAR_MAP)
    {
        MapInfo* map = owner->value._tbl;
        for (int slot = 0; slot < map->_BucketCapa; ++slot)
            if (IsMapNodeUsed(map->_Bucket[slot]) && index-- == 0) return CopyOwned(owner, &map->_Bucket[slot].data->key);
    }
    if (owner->value.GetType() == VAR_SET)
    {
        SetInfo* set = owner->value._set;
        for (int slot = 0; slot < set->_BucketCapa; ++slot)
            if (IsSetNodeUsed(set->_Bucket[slot]) && index-- == 0) return CopyOwned(owner, &set->_Bucket[slot].data->key);
    }
    return ValuePtr();
}

ValuePtr ValueFind(const ValuePtr& owner, const ValuePtr& key)
{
    if (!SameDomain(owner, key) || owner->value.GetType() != NeoScript::VAR_MAP) return ValuePtr();
    return CopyOwned(owner, owner->value._tbl->Find(&key->value));
}

bool ValueSet(const ValuePtr& owner, std::size_t index, const ValuePtr& element)
{
    if (!SameDomain(owner, element) || !UsableFunction(element) || index >= ValueSize(owner)) return false;
    if (owner->value.GetType() == NeoScript::VAR_LIST)
        return owner->value._lst->SetValue(static_cast<int>(index), &element->value);
    if (owner->value.GetType() == NeoScript::VAR_ARRAY)
        return owner->value._array->AssignValue(static_cast<int>(index), &element->value);
    return false;
}

bool SnapshotMap(const ValuePtr& owner, std::vector<std::pair<ValuePtr, ValuePtr>>& entries)
{
    using namespace NeoScript;
    entries.clear();
    if (!owner || owner->value.GetType() != VAR_MAP) return false;
    const MapInfo* map = owner->value._tbl;
    entries.reserve(ValueSize(owner));
    // 순번마다 버킷 처음부터 다시 찾지 않고 전체 맵을 한 번만 순회한다.
    for (int i = 0; i < map->_BucketCapa; ++i)
        if (IsMapNodeUsed(map->_Bucket[i]))
            entries.push_back(std::make_pair(CopyOwned(owner, &map->_Bucket[i].data->key),
                CopyOwned(owner, &map->_Bucket[i].data->value)));
    return true;
}

bool ValuePut(const ValuePtr& owner, const ValuePtr& key, const ValuePtr& element)
{
    if (!SameDomain(owner, key) || !SameDomain(owner, element) || !UsableFunction(element) ||
        owner->value.GetType() != NeoScript::VAR_MAP || key->value.GetType() == NeoScript::VAR_NONE) return false;
    owner->value._tbl->Insert(&key->value, &element->value);
    return true;
}

bool ValueAppend(const ValuePtr& owner, const ValuePtr& element)
{
    if (!SameDomain(owner, element) || !UsableFunction(element)) return false;
    if (owner->value.GetType() == NeoScript::VAR_LIST)
        return owner->value._lst->InsertLast(&element->value);
    if (owner->value.GetType() == NeoScript::VAR_ARRAY)
    {
        NeoScript::ArrayInfo* array = owner->value._array;
        const int previous = array->_count;
        if (previous == (std::numeric_limits<int>::max)() || !owner->vm->ArrayResize(array, previous + 1)) return false;
        if (array->AssignValue(previous, &element->value)) return true;
        owner->vm->ArrayResize(array, previous);
        return false;
    }
    if (owner->value.GetType() == NeoScript::VAR_SET)
    {
        return owner->value._set->Insert(&element->value);
    }
    return false;
}

bool ValueResize(const ValuePtr& owner, std::size_t count)
{
    if (!owner || count > static_cast<std::size_t>((std::numeric_limits<int>::max)())) return false;
    if (owner->value.GetType() == NeoScript::VAR_LIST)
    { owner->value._lst->Resize(static_cast<int>(count)); return true; }
    if (owner->value.GetType() == NeoScript::VAR_ARRAY)
        return owner->vm->ArrayResize(owner->value._array, static_cast<int>(count));
    return false;
}

bool ValueContains(const ValuePtr& owner, const ValuePtr& key)
{
    if (!SameDomain(owner, key)) return false;
    if (owner->value.GetType() == NeoScript::VAR_MAP) return owner->value._tbl->Find(&key->value) != nullptr;
    if (owner->value.GetType() == NeoScript::VAR_SET) return owner->value._set->Find(&key->value);
    return false;
}

namespace
{
bool ArrayBufferRange(const ValuePtr& value, ArrayStorage storage, std::size_t offset,
    std::size_t count, unsigned char*& buffer, std::size_t& bytes)
{
    using namespace NeoScript;
    if (!value || value->value.GetType() != VAR_ARRAY) return false;
    ArrayInfo* array = value->value._array;
    NeoArrayElementType element;
    std::size_t width;
    switch (storage)
    {
    case ArrayStorage::Int64: element = NeoArrayElementType::Int; width = sizeof(NS_INT); break;
    case ArrayStorage::Float64: element = NeoArrayElementType::Float; width = sizeof(NS_FLOAT); break;
    case ArrayStorage::Int32: element = NeoArrayElementType::Int32; width = sizeof(std::int32_t); break;
    case ArrayStorage::Float32: element = NeoArrayElementType::Float32; width = sizeof(float); break;
    default: return false;
    }
    const std::size_t size = static_cast<std::size_t>(array->_count);
    if (element != array->_elementType || offset > size || count > size - offset ||
        size > (std::numeric_limits<std::size_t>::max)() / width) return false;
    bytes = count * width;
    // 빈 배열은 데이터 포인터가 없으므로 0을 더하는 연산도 하지 않는다.
    buffer = bytes ? static_cast<unsigned char*>(array->_data) + offset * width : nullptr;
    return true;
}
}

bool ReadArrayBuffer(const ValuePtr& value, ArrayStorage storage, std::size_t offset, void* target, std::size_t count)
{
    if (!target && count) return false;
    unsigned char* buffer = nullptr;
    std::size_t bytes = 0;
    if (!ArrayBufferRange(value, storage, offset, count, buffer, bytes)) return false;
    if (bytes) std::memmove(target, buffer, bytes);
    return true;
}

bool WriteArrayBuffer(const ValuePtr& value, ArrayStorage storage, std::size_t offset, const void* source, std::size_t count)
{
    if (!source && count) return false;
    unsigned char* buffer = nullptr;
    std::size_t bytes = 0;
    if (!ArrayBufferRange(value, storage, offset, count, buffer, bytes)) return false;
    if (bytes) std::memmove(buffer, source, bytes);
    return true;
}

bool ValueReserve(const ValuePtr& owner, std::size_t capacity)
{
    using namespace NeoScript;
    if (!owner || capacity > static_cast<std::size_t>((std::numeric_limits<int>::max)())) return false;
    const int count = static_cast<int>(capacity);
    switch (owner->value.GetType())
    {
    case VAR_ARRAY: owner->vm->ArrayReserve(owner->value._array, count); return true;
    case VAR_MAP: owner->value._tbl->Reserve(count); return true;
    case VAR_LIST: owner->value._lst->Reserve(count); return true;
    case VAR_SET: owner->value._set->Reserve(count); return true;
    default: return false;
    }
}

bool ValueRemoveAt(const ValuePtr& owner, std::size_t index)
{
    return owner && owner->value.GetType() == NeoScript::VAR_LIST && index < ValueSize(owner) &&
        owner->value._lst->Remove(static_cast<int>(index));
}

bool ValueRemove(const ValuePtr& owner, const ValuePtr& key)
{
    if (!ValueContains(owner, key)) return false;
    if (owner->value.GetType() == NeoScript::VAR_MAP) owner->value._tbl->Remove(&key->value);
    else owner->value._set->Remove(&key->value);
    return true;
}

bool ValueClear(const ValuePtr& owner)
{
    if (!owner) return false;
    if (owner->value.GetType() == NeoScript::VAR_MAP) { owner->value._tbl->Clear(); return true; }
    if (owner->value.GetType() == NeoScript::VAR_SET) { owner->value._set->Clear(); return true; }
    return ValueResize(owner, 0);
}

bool ValueSameStorage(const ValuePtr& left, const ValuePtr& right)
{
    using namespace NeoScript;
    if (!SameDomain(left, right) || left->value.GetType() != right->value.GetType()) return false;
    switch (left->value.GetType())
    {
    case VAR_ARRAY: return left->value._array == right->value._array;
    case VAR_MAP: return left->value._tbl == right->value._tbl;
    case VAR_LIST: return left->value._lst == right->value._lst;
    case VAR_SET: return left->value._set == right->value._set;
    default: return false;
    }
}

NeoScript::Invocation CallFunction(NeoScript::IRuntime& runtime, NeoScript::InstanceHandle instance, const ValuePtr& value)
{
    using namespace NeoScript;
    if (!value || value->runtime != &runtime) return Invocation();
    InstanceRec* record = value->runtime->resolveInstance(instance);
    if (!record || !UsableFunction(value, record->worker) ||
        (value->value.GetType() != VAR_FUN && value->value.GetType() != VAR_CLOSURE)) return Invocation();
    const int index = value->value.GetType() == VAR_FUN ? value->value._fun_index : value->value._closure->_funIndex;
    Invocation result = runtime.Call(instance, NeoScriptInternal::fun(static_cast<std::uint32_t>(index),
        record->program.id, record->program.generation, value->runtime->identity()));
    if (result && value->value.GetType() == VAR_CLOSURE) value->vm->Move(&record->callClosure, &value->value);
    return result;
}

ValuePtr FindFunction(NeoScript::IRuntime& runtime, NeoScript::ProgramHandle program,
    const std::string& name, const std::shared_ptr<void>& lifetime)
{
    if (!lifetime) return ValuePtr();
    const NeoScript::FunctionHandle function = runtime.FindFunction(program, name);
    if (!function) return ValuePtr();
    ValuePtr result(new OwnedValue(runtime, lifetime));
    result->value = NeoScript::BoundaryValue(NeoScript::VAR_FUN);
    result->value._fun_index = static_cast<int>(NeoScript::NeoScriptInternal::funIndex(function));
    return result;
}

float NarrowFloat32(double value) { return NeoScript::NeoNarrowFloat32(value); }

ValuePtr ReadGlobal(NeoScript::IRuntime& runtime, NeoScript::InstanceHandle instance,
    const std::string& name, const std::shared_ptr<void>& lifetime)
{
    NeoScript::InstanceRec* record = static_cast<NeoScript::RuntimeImpl&>(runtime).resolveInstance(instance);
    if (!record || !record->worker) return ValuePtr();
    return CaptureOwned(record, record->worker->GetVar(name), lifetime);
}

bool WriteGlobal(NeoScript::IRuntime& runtime, NeoScript::InstanceHandle instance,
    const std::string& name, const ValuePtr& value)
{
    if (!value || value->runtime != &runtime) return false;
    NeoScript::InstanceRec* record = static_cast<NeoScript::RuntimeImpl&>(runtime).resolveInstance(instance);
    if (!record || !record->worker || !UsableFunction(value, record->worker)) return false;
    NeoScript::ValueRef destination = record->worker->GetVar(name);
    if (!destination) return false;
    value->vm->Move(destination, &value->value);
    return true;
}

bool EncodeJson(const ValuePtr& value, bool sortKeys, std::string& text,
    std::string& error, std::vector<std::string>& warnings)
{
    text.clear(); error.clear(); warnings.clear();
    if (!value) { error = "JSON value is unavailable"; return false; }
    NeoScript::JsonResult result;
    // 정적 구조체의 nullable 필드는 누락된 필드와 구분되므로 JSON null을 보존한다.
    const bool success = NeoScript::JsonInternal::Encode(value->value, text, result, sortKeys, true);
    error = result.error;
    warnings = std::move(result.warnings);
    return success;
}

ValuePtr DecodeJson(NeoScript::IRuntime& runtime, NeoScript::InstanceHandle instance,
    const std::string& text, bool strictIntegers, bool requireMap, const std::shared_ptr<void>& lifetime,
    std::string& error, std::vector<std::string>& warnings)
{
    error.clear(); warnings.clear();
    NeoScript::InstanceRec* record = static_cast<NeoScript::RuntimeImpl&>(runtime).resolveInstance(instance);
    if (!record || !record->worker || !lifetime) { error = "JSON instance is unavailable"; return ValuePtr(); }
    ValuePtr value(new OwnedValue(runtime, lifetime));
    NeoScript::JsonResult result;
    const bool success = NeoScript::JsonInternal::Decode(*static_cast<NeoScript::CNeoVMWorker*>(record->worker),
        text.data(), text.size(), &value->value, result, strictIntegers, requireMap, true);
    error = result.error;
    warnings = std::move(result.warnings);
    return success ? value : ValuePtr();
}
}
