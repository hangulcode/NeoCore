#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace NeoScript
{
class CallContext;
class IRuntime;
struct ProgramHandle;
struct InstanceHandle;
struct Error;
}

// 정적 검사와 호스트 스키마 연결을 통과한 NeoCore 호출만 쓰는 내부 브리지다.
// 기존 공개 인자 접근자의 동적 변환은 수행하지 않으므로 호출자가 정확한 타입을 보장한다.
namespace NeoCoreBackend
{
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
// 전역 초기화처럼 Invocation이 없는 실행에서도 이미 저장된 오류 위치를 읽는다. 오류를 소비하지 않는다.
bool ReadLastCallError(NeoScript::IRuntime& runtime, NeoScript::InstanceHandle instance, NeoScript::Error& error);
}
