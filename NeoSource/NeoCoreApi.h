#pragma once

#include "NeoCoreTypes.h"

namespace NeoCore
{

// 내장 API 와 호스트 API 가 함께 쓰는 서명 모델이다. 컴파일러는 이 서명만 보고 인자 개수·타입·방향을 검사한다.

// Out 은 호출 뒤 인자 변수에 값을 되돌려 쓴다. 참조를 넘기지 않고 호출 창을 거쳐 복사한다.
enum class ApiParameterMode : std::uint8_t { In, Out };

// 검사가 끝난 호출 하나의 서명. 타입 변수는 수신자 타입으로 이미 채워져 있다.
struct ApiCallSignature
{
    TypeId result = InvalidType;
    std::vector<TypeId> parameters;
    std::vector<ApiParameterMode> modes;
    std::vector<std::string> names;
    // 뒤쪽 매개변수는 생략할 수 있다. minimum 은 반드시 넘겨야 하는 개수다.
    std::size_t minimum = 0;
    // 진단에 쓰는 이름(예: map.contains).
    std::string display;

    ApiParameterMode Mode(std::size_t index) const
    { return index < modes.size() ? modes[index] : ApiParameterMode::In; }
};

// 선언에 적는 타입 식의 종류. Self/Element/Key/Value 는 수신자 타입에서 채운다.
enum class ApiTypeKind : std::uint8_t
{
    Named,      // int, string, Vector3, TimeSpan 같은 이름 있는 타입
    Self,       // 수신자 타입
    Element,    // T: list/array/set 의 원소
    Key,        // K: map 의 키
    Value,      // V: map 의 값
    List, Array, Array32, Set,  // 인자 하나
    Map,                        // 인자 둘
    Function,                   // 첫 인자가 반환, 나머지가 매개변수
    // 한 타입으로 정해지지 않는 자리. 검사기가 선언의 Custom 규칙으로 따로 검사한다.
    Open
};

struct ApiTypeNode
{
    ApiTypeKind kind = ApiTypeKind::Open;
    std::string name;
    // 자식은 등록부의 노드 표에 연속으로 놓인다.
    std::uint32_t first = 0;
    std::uint32_t count = 0;
};

enum ApiFlags : std::uint32_t
{
    ApiNone = 0,
    // 서명만으로 표현되지 않는 규칙이 있다(가변 인자, 인자에서 추론하는 타입 등). 검사기가 전용 규칙을 쓴다.
    ApiCustom = 1u << 0,
    // 선언에 "..." 이 있다. 선언된 매개변수 뒤로 인자를 더 받는다.
    ApiVariadic = 1u << 1,
    // 원소가 구조체인 컬렉션에는 쓸 수 없다(동등 비교·네이티브 정렬).
    ApiNoStructElement = 1u << 2,
    // 본체가 내장 라이브러리 표에 없다. 런타임이 다른 경로로 제공한다(JSON 변환).
    ApiExternal = 1u << 3
};

struct ApiParameter
{
    std::uint32_t type = 0;     // 노드 표 색인
    std::string name;
    ApiParameterMode mode = ApiParameterMode::In;
    bool optional = false;
};

// 표의 한 줄. 색인이 곧 내장 본체의 번호이고, key 는 이미지에 남는 연결 이름이다.
struct ApiDeclaration
{
    std::uint32_t index = 0;
    std::string owner;          // "map", "list", "string", "math", ... 전역 함수는 빈 문자열
    std::string name;
    std::string key;            // owner.name (전역 함수는 name)
    std::string typeParameters; // 표시용 "<T>"
    std::uint32_t result = 0;   // 노드 표 색인
    std::vector<ApiParameter> parameters;
    std::size_t minimum = 0;
    std::uint32_t flags = ApiNone;
    // Out 매개변수의 비트(0번 비트가 첫 매개변수). 런타임이 창 슬롯을 놓지 않고 남길 자리다.
    std::uint32_t outMask = 0;

    bool Is(ApiFlags flag) const { return (flags & flag) != 0; }
};

// 등록부는 프로세스에 하나이고 선언 표(NeoCoreApiTable.inc)에서 한 번 만들어진다.
const std::vector<ApiDeclaration>& CoreApi();
const ApiDeclaration* FindCoreApi(const std::string& owner, const std::string& name);
// 이미지의 연결 이름으로 찾는다. 없으면 -1 이다.
int FindCoreApiIndex(const std::string& key);
// 소유자 이름이 내장 모듈(import 대상)인가.
bool IsCoreApiModule(const std::string& owner);
// 수신자 타입이 쓰는 소유자 이름. 내장 API 를 갖지 않는 타입이면 빈 문자열이다.
std::string CoreApiOwner(const TypeRegistry& types, TypeId receiver);

// 선언의 타입 식을 수신자 타입으로 채워 실제 타입으로 만든다. 정해지지 않는 자리는 InvalidType 이다.
TypeId ResolveApiType(TypeRegistry& types, std::uint32_t node, TypeId receiver);
// 선언 전체를 호출 서명으로 만든다. Custom 선언은 정해지는 자리만 채운다.
ApiCallSignature ResolveApiSignature(TypeRegistry& types, const ApiDeclaration& declaration, TypeId receiver);
// 표시용 문자열(예: "bool map.try_get(K key, out V value)").
std::string FormatApiDeclaration(const ApiDeclaration& declaration);
std::string FormatApiType(std::uint32_t node);
std::string FormatApiParameter(const ApiParameter& parameter);

}
