#pragma once

#include "NeoCoreTypes.h"

namespace NeoCore
{

typedef std::uint32_t HostBindingId;
static const HostBindingId InvalidHostBinding = 0;

enum class HostBindingKind : std::uint8_t
{
    Method,
    Getter,
    Setter
};

struct HostBinding
{
    HostBindingId id = InvalidHostBinding;
    TypeId ownerType = InvalidType;
    std::string name;
    HostBindingKind kind = HostBindingKind::Method;
    TypeId returnType = InvalidType;
    std::vector<TypeId> parameters;
};

struct HostProperty
{
    std::string name;
    TypeId type = InvalidType;
    HostBindingId getter = InvalidHostBinding;
    HostBindingId setter = InvalidHostBinding;
};

struct HostGlobal
{
    std::string name;
    TypeId type = InvalidType;
};

// 스키마에는 컴파일 계약만 저장한다. 실행 중 객체 주소와 콜백은 별도로 연결한다.
class HostSchema
{
public:
    TypeRegistry& Types() { return types_; }
    const TypeRegistry& Types() const { return types_; }

    TypeId AddObject(const std::string& name);
    HostBindingId AddMethod(TypeId owner, const std::string& name, TypeId result,
        const std::vector<TypeId>& parameters);
    bool AddProperty(TypeId owner, const std::string& name, TypeId type,
        bool readable = true, bool writable = true);
    bool AddGlobal(const std::string& name, TypeId nativeOrNullable);

    const HostBinding* FindMethod(TypeId owner, const std::string& name) const;
    const HostProperty* FindProperty(TypeId owner, const std::string& name) const;
    const HostGlobal* FindGlobal(const std::string& name) const;
    const HostBinding* FindBinding(HostBindingId id) const;

    // 바인딩 번호는 1부터 시작하며 Bindings()[id - 1]에 대응한다.
    const std::vector<HostBinding>& Bindings() const { return bindings_; }
    const std::vector<HostGlobal>& Globals() const { return globals_; }
    const std::string& LastError() const { return lastError_; }
    bool Validate(std::string& error) const;

private:
    struct Object
    {
        TypeId type = InvalidType;
        std::string name;
        std::vector<HostProperty> properties;
    };

    TypeRegistry types_;
    std::vector<Object> objects_;
    std::vector<HostBinding> bindings_;
    std::vector<HostGlobal> globals_;
    mutable std::string lastError_;

    const Object* FindObject(TypeId type) const;
    Object* FindObject(TypeId type);
    bool IsNameAllowed(const std::string& name) const;
    bool IsSupportedType(TypeId type, bool allowVoid) const;
    bool CheckMember(TypeId owner, const std::string& name);
    bool CheckBindingCapacity(std::size_t additional);
    bool Fail(const std::string& message) const;
};

}
