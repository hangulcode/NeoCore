#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace NeoScript
{
class CallContext;
class IRuntime;
class Invocation;
struct ProgramHandle;
struct InstanceHandle;
struct Error;
}

// 정적 검사와 호스트 스키마 연결을 통과한 NeoCore 호출만 쓰는 내부 브리지다.
// 기존 공개 인자 접근자의 동적 변환은 수행하지 않으므로 호출자가 정확한 타입을 보장한다.
namespace NeoCoreBackend
{
// 호스트가 보관하는 값은 프로그램 수명을 붙들며 VM 참조를 독립적으로 소유한다.
// 공개 정적 서명 검사는 이 내부 브리지에 들어오기 전에 끝나야 한다.
struct OwnedValue;
struct ValueAccess;
typedef std::shared_ptr<OwnedValue> ValuePtr;
enum class ValueKind { Null, Int, Float, Bool, String, Vector2, Vector3, Vector4, List, Map, Set, Array, Function, Object };
enum class ArrayStorage { Bool, Int64, Float64, Vector2, Vector3, Vector4, Int32, Float32 };
struct ValueData
{
    ValueKind kind = ValueKind::Null;
    std::int64_t integer = 0;
    double number = 0;
    std::string text;
    float vector[4] = {};
};
bool AppendNull(NeoScript::Invocation& invocation);
bool AppendValue(NeoScript::Invocation& invocation, const ValuePtr& value);
bool CanPassValue(NeoScript::IRuntime& runtime, NeoScript::InstanceHandle instance, const ValuePtr& value);
ValuePtr CaptureArgument(const NeoScript::CallContext& context, std::size_t index, const std::shared_ptr<void>& lifetime);
ValuePtr CaptureResult(const NeoScript::Invocation& invocation, const std::shared_ptr<void>& lifetime);
bool ReturnValue(NeoScript::CallContext& context, const ValuePtr& value);
ValuePtr MakeValue(NeoScript::IRuntime& runtime, const ValueData& data, const std::shared_ptr<void>& lifetime);
ValuePtr MakeCollection(NeoScript::IRuntime& runtime, ValueKind kind, ArrayStorage storage, const std::shared_ptr<void>& lifetime);
bool ReadValue(const ValuePtr& value, ValueData& data);
std::size_t ValueSize(const ValuePtr& value);
ValuePtr ValueAt(const ValuePtr& value, std::size_t index);
ValuePtr ValueKeyAt(const ValuePtr& value, std::size_t index);
ValuePtr ValueFind(const ValuePtr& value, const ValuePtr& key);
bool SnapshotMap(const ValuePtr& value, std::vector<std::pair<ValuePtr, ValuePtr>>& entries);
bool ValueSet(const ValuePtr& value, std::size_t index, const ValuePtr& element);
bool ValuePut(const ValuePtr& value, const ValuePtr& key, const ValuePtr& element);
bool ValueAppend(const ValuePtr& value, const ValuePtr& element);
bool ValueResize(const ValuePtr& value, std::size_t count);
// 원소 저장 형식이 일치하는 숫자 배열만 범위를 검사한 뒤 한 번에 복사한다.
bool ReadArrayBuffer(const ValuePtr& value, ArrayStorage storage, std::size_t offset, void* target, std::size_t count);
bool WriteArrayBuffer(const ValuePtr& value, ArrayStorage storage, std::size_t offset, const void* source, std::size_t count);
bool ValueReserve(const ValuePtr& value, std::size_t capacity);
bool ValueRemoveAt(const ValuePtr& value, std::size_t index);
bool ValueRemove(const ValuePtr& value, const ValuePtr& key);
bool ValueContains(const ValuePtr& value, const ValuePtr& key);
bool ValueClear(const ValuePtr& value);
bool ValueSameStorage(const ValuePtr& left, const ValuePtr& right);
NeoScript::Invocation CallFunction(NeoScript::IRuntime& runtime, NeoScript::InstanceHandle instance, const ValuePtr& value);
ValuePtr FindFunction(NeoScript::IRuntime& runtime, NeoScript::ProgramHandle program,
    const std::string& name, const std::shared_ptr<void>& lifetime);
float NarrowFloat32(double value);
ValuePtr ReadGlobal(NeoScript::IRuntime& runtime, NeoScript::InstanceHandle instance, const std::string& name, const std::shared_ptr<void>& lifetime);
bool WriteGlobal(NeoScript::IRuntime& runtime, NeoScript::InstanceHandle instance, const std::string& name, const ValuePtr& value);
bool EncodeJson(const ValuePtr& value, bool sortKeys, std::string& text, std::string& error, std::vector<std::string>& warnings);
ValuePtr DecodeJson(NeoScript::IRuntime& runtime, NeoScript::InstanceHandle instance,
    const std::string& text, bool strictIntegers, bool requireMap, const std::shared_ptr<void>& lifetime,
    std::string& error, std::vector<std::string>& warnings);
std::int64_t ReadInt(const NeoScript::CallContext& context, std::size_t index);
double ReadFloat(const NeoScript::CallContext& context, std::size_t index);
bool ReadBool(const NeoScript::CallContext& context, std::size_t index);
// 재진입으로 원래 인자 슬롯이 달라져도 문자열이 살아 있도록 소유 복사본을 돌려준다.
std::string ReadString(const NeoScript::CallContext& context, std::size_t index);
// 벡터 인자는 재진입과 COW 수정에 영향받지 않는 성분 스냅샷으로 복사한다.
void ReadVector(const NeoScript::CallContext& context, std::size_t index, float out[4]);
bool IsNull(const NeoScript::CallContext& context, std::size_t index);
// 컴파일 직후 인스턴스 생성 전에만 호출한다. 루트 소스의 생성된 헬퍼 행을 원본 행으로 돌린다.
bool RemapSourceLines(NeoScript::IRuntime& runtime, NeoScript::ProgramHandle program,
    const std::vector<std::pair<int, int>>& mappings);
// 이미지의 공개 함수 이름과 인자 수가 실제 바이트코드의 함수 표와 같은지 검사한다.
bool ValidateImageFunctions(NeoScript::IRuntime& runtime, NeoScript::ProgramHandle program,
    const std::vector<std::pair<std::string, std::size_t>>& signatures, std::string& error);
// NeoCore 인스턴스의 sleep을 실행 제한과 무관하게 협력적 대기로 설정한다.
bool EnableCooperativeSleep(NeoScript::IRuntime& runtime, NeoScript::InstanceHandle instance);
// 전역 초기화처럼 Invocation이 없는 실행에서도 이미 저장된 오류 위치를 읽는다. 오류를 소비하지 않는다.
bool ReadLastCallError(NeoScript::IRuntime& runtime, NeoScript::InstanceHandle instance, NeoScript::Error& error);
}
