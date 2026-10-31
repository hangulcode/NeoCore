#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace NeoCore
{

typedef std::uint32_t TypeId;
static const TypeId InvalidType = 0;
static const std::size_t InvalidFieldIndex = static_cast<std::size_t>(-1);

// 숫자 스칼라는 int64와 float64뿐이다. 32비트 저장은 Array32와 벡터의 속성이다.
enum class TypeKind : std::uint8_t
{
    Invalid,
    Void,
    Int,
    Float,
    Bool,
    String,
    Null,
    Vector2,
    Vector3,
    Vector4,
    Quaternion,
    Nullable,
    Array,
    Array32,
    List,
    Map,
    Set,
    Function,
    Struct,
    Native,
    Reference
};

struct StructField
{
    std::string name;
    TypeId type = InvalidType;
    StructField() = default;
    StructField(const std::string& fieldName, TypeId fieldType) : name(fieldName), type(fieldType) {}
};

struct TypeDesc
{
    TypeKind kind = TypeKind::Invalid;
    TypeId element = InvalidType;
    TypeId key = InvalidType;
    TypeId value = InvalidType;
    TypeId returnType = InvalidType;
    std::vector<TypeId> parameters;
    std::string name;
    std::vector<StructField> fields;
    bool fieldsDefined = false;
};

// 타입 번호는 이 레지스트리의 수명 동안 유지된다. 서로 다른 레지스트리나 파일 형식 사이의 ID는 아니다.
class TypeRegistry
{
public:
    TypeRegistry();

    TypeId Void() const { return static_cast<TypeId>(TypeKind::Void); }
    TypeId Int() const { return static_cast<TypeId>(TypeKind::Int); }
    TypeId Float() const { return static_cast<TypeId>(TypeKind::Float); }
    TypeId Bool() const { return static_cast<TypeId>(TypeKind::Bool); }
    TypeId String() const { return static_cast<TypeId>(TypeKind::String); }
    TypeId Null() const { return static_cast<TypeId>(TypeKind::Null); }
    TypeId Vector2() const { return static_cast<TypeId>(TypeKind::Vector2); }
    TypeId Vector3() const { return static_cast<TypeId>(TypeKind::Vector3); }
    TypeId Vector4() const { return static_cast<TypeId>(TypeKind::Vector4); }
    TypeId Quaternion() const { return static_cast<TypeId>(TypeKind::Quaternion); }

    TypeId LookupBuiltin(const std::string& name) const;
    TypeId LookupNamed(const std::string& qualifiedName) const;
    const TypeDesc& Get(TypeId id) const;
    std::string DisplayName(TypeId id) const;

    bool IsNumeric(TypeId id) const;
    bool IsNullable(TypeId id) const;
    bool IsReference(TypeId id) const;
    bool IsStorable(TypeId id) const;
    bool CanAssign(TypeId target, TypeId source) const;
    TypeId CommonNumericType(TypeId left, TypeId right) const;

    TypeId NullableOf(TypeId element);
    TypeId ReferenceOf(TypeId element);
    TypeId ArrayOf(TypeId element);
    TypeId Array32Of(TypeId element);
    TypeId ListOf(TypeId element);
    TypeId MapOf(TypeId key, TypeId value);
    TypeId SetOf(TypeId element);
    TypeId FunctionOf(TypeId returnType, const std::vector<TypeId>& parameters);

    // 구조체·호스트 객체의 이름을 먼저 등록한다. 구조체의 필드는 한 번만 확정한다.
    TypeId DeclareStruct(const std::string& qualifiedName);
    TypeId DeclareNative(const std::string& qualifiedName);
    bool DefineStructFields(TypeId structure, const std::vector<StructField>& fields, std::string* error = nullptr);
    // 성공한 정의의 필드 주소·순서는 레지스트리 수명 동안 유지된다. 실패한 조회는 인덱스도 무효화한다.
    const StructField* FindStructField(TypeId structure, const std::string& name, std::size_t* index = nullptr) const;

private:
    struct Key
    {
        TypeKind kind;
        std::vector<TypeId> arguments;
        std::string name;
        bool operator<(const Key& other) const;
    };

    std::deque<TypeDesc> types_;
    std::map<Key, TypeId> interned_;
    std::map<std::string, TypeId> names_;

    TypeId Intern(TypeKind kind, const std::vector<TypeId>& arguments, const std::string& name);
    TypeId Unary(TypeKind kind, TypeId element);
    TypeId DeclareNominal(TypeKind kind, const std::string& qualifiedName);
    bool ContainsStructValue(TypeId value, TypeId structure) const;
};

}
