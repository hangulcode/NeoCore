#include "NeoCoreHostSchema.h"

#include <limits>
#include <set>

namespace NeoCore
{

bool HostSchema::Fail(const std::string& message) const
{
    lastError_ = message;
    return false;
}

const HostSchema::Object* HostSchema::FindObject(TypeId type) const
{
    for (const Object& object : objects_)
        if (object.type == type) return &object;
    return nullptr;
}

HostSchema::Object* HostSchema::FindObject(TypeId type)
{
    for (Object& object : objects_)
        if (object.type == type) return &object;
    return nullptr;
}

bool HostSchema::IsNameAllowed(const std::string& name) const
{
    if (name.empty()) return false;
    for (std::size_t i = 0; i < name.size(); ++i)
    {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        if (!letter && (i == 0 || c < '0' || c > '9')) return false;
    }
    // 프런트엔드의 식별자와 내장 이름을 가리지 않도록 등록 시점에 거부한다.
    static const char* const reserved[] = {
        "var", "fun", "int", "float", "bool", "string", "void", "int32", "float32", "any",
        "null", "true", "false", "if", "else", "while", "for", "foreach", "in", "return",
        "break", "continue", "import", "public", "export", "const", "struct", "class",
        "switch", "case", "default", "print", "tostring", "toint", "tofloat", "tosize",
        "type", "sleep", "yield", "__LINE__", "list", "map", "array", "array32", "set",
        "math", "system"
    };
    for (const char* value : reserved)
        if (name == value) return false;
    return types_.LookupBuiltin(name) == InvalidType;
}

bool HostSchema::IsSupportedType(TypeId type, bool allowVoid) const
{
    const TypeDesc& descriptor = types_.Get(type);
    switch (descriptor.kind)
    {
    case TypeKind::Void:
        return allowVoid;
    case TypeKind::Int:
    case TypeKind::Float:
    case TypeKind::Bool:
    case TypeKind::String:
    case TypeKind::Vector2:
    case TypeKind::Vector3:
    case TypeKind::Vector4:
    case TypeKind::Quaternion:
        return true;
    case TypeKind::Nullable:
        // 중첩 nullable과 참조 타입은 허용하지 않는다. nullable void도 반환 타입이 아니다.
        return types_.Get(descriptor.element).kind != TypeKind::Nullable &&
            IsSupportedType(descriptor.element, false);
    case TypeKind::Native:
    {
        const Object* object = FindObject(type);
        return object != nullptr && object->name == descriptor.name;
    }
    default:
        return false;
    }
}

TypeId HostSchema::AddObject(const std::string& name)
{
    lastError_.clear();
    if (!IsNameAllowed(name))
    {
        Fail("host object name must be a non-reserved identifier");
        return InvalidType;
    }
    if (types_.LookupNamed(name) != InvalidType)
    {
        Fail("host object type name is already declared");
        return InvalidType;
    }
    Object object;
    object.name = name;
    object.type = types_.DeclareNative(name);
    if (object.type == InvalidType)
    {
        Fail("host object type could not be declared");
        return InvalidType;
    }
    objects_.push_back(object);
    return object.type;
}

bool HostSchema::CheckMember(TypeId owner, const std::string& name)
{
    if (types_.Get(owner).kind != TypeKind::Native || !IsSupportedType(owner, false))
        return Fail("host member owner must be an object registered by AddObject");
    if (!IsNameAllowed(name))
        return Fail("host member name must be a non-reserved identifier");
    if (FindMethod(owner, name) != nullptr || FindProperty(owner, name) != nullptr)
        return Fail("host member name is already declared; overloads are not supported");
    return true;
}

bool HostSchema::CheckBindingCapacity(std::size_t additional)
{
    const std::size_t maximum = static_cast<std::size_t>(std::numeric_limits<HostBindingId>::max());
    if (additional > maximum || bindings_.size() > maximum - additional)
        return Fail("host binding id capacity exceeded");
    return true;
}

HostBindingId HostSchema::AddMethod(TypeId owner, const std::string& name, TypeId result,
    const std::vector<TypeId>& parameters)
{
    lastError_.clear();
    if (!CheckMember(owner, name)) return InvalidHostBinding;
    if (!IsSupportedType(result, true))
    {
        Fail("unsupported host method return type");
        return InvalidHostBinding;
    }
    for (TypeId parameter : parameters)
    {
        if (!IsSupportedType(parameter, false))
        {
            Fail("unsupported host method parameter type");
            return InvalidHostBinding;
        }
    }
    if (!CheckBindingCapacity(1)) return InvalidHostBinding;
    HostBinding binding;
    binding.id = static_cast<HostBindingId>(bindings_.size() + 1);
    binding.ownerType = owner;
    binding.name = name;
    binding.kind = HostBindingKind::Method;
    binding.returnType = result;
    binding.parameters = parameters;
    bindings_.push_back(binding);
    return binding.id;
}

bool HostSchema::AddProperty(TypeId owner, const std::string& name, TypeId type,
    bool readable, bool writable)
{
    lastError_.clear();
    if (!CheckMember(owner, name)) return false;
    if (!readable && !writable) return Fail("host property needs a getter or setter");
    if (!IsSupportedType(type, false)) return Fail("unsupported host property type");
    if (!CheckBindingCapacity((readable ? 1 : 0) + (writable ? 1 : 0))) return false;

    HostProperty property;
    property.name = name;
    property.type = type;
    HostBinding getter;
    getter.ownerType = owner;
    getter.name = name;
    getter.kind = HostBindingKind::Getter;
    getter.returnType = type;
    HostBinding setter;
    setter.ownerType = owner;
    setter.name = name;
    setter.kind = HostBindingKind::Setter;
    setter.returnType = types_.Void();
    setter.parameters.push_back(type);

    // 형식 검사는 삽입 전에 끝낸다. 읽기 전용과 쓰기 전용도 동일한 번호 공간을 쓴다.
    if (readable)
    {
        getter.id = static_cast<HostBindingId>(bindings_.size() + 1);
        property.getter = getter.id;
        bindings_.push_back(getter);
    }
    if (writable)
    {
        setter.id = static_cast<HostBindingId>(bindings_.size() + 1);
        property.setter = setter.id;
        bindings_.push_back(setter);
    }
    FindObject(owner)->properties.push_back(property);
    return true;
}

bool HostSchema::AddGlobal(const std::string& name, TypeId nativeOrNullable)
{
    lastError_.clear();
    if (!IsNameAllowed(name)) return Fail("host global name must be a non-reserved identifier");
    if (FindGlobal(name) != nullptr) return Fail("host global name is already declared");
    TypeId objectType = nativeOrNullable;
    if (types_.Get(objectType).kind == TypeKind::Nullable) objectType = types_.Get(objectType).element;
    if (types_.Get(objectType).kind != TypeKind::Native || !IsSupportedType(nativeOrNullable, false))
        return Fail("host global type must be a registered native object or its nullable type");
    HostGlobal global;
    global.name = name;
    global.type = nativeOrNullable;
    globals_.push_back(global);
    return true;
}

const HostBinding* HostSchema::FindMethod(TypeId owner, const std::string& name) const
{
    for (const HostBinding& binding : bindings_)
        if (binding.ownerType == owner && binding.kind == HostBindingKind::Method && binding.name == name)
            return &binding;
    return nullptr;
}

const HostProperty* HostSchema::FindProperty(TypeId owner, const std::string& name) const
{
    const Object* object = FindObject(owner);
    if (object != nullptr)
        for (const HostProperty& property : object->properties)
            if (property.name == name) return &property;
    return nullptr;
}

const HostGlobal* HostSchema::FindGlobal(const std::string& name) const
{
    for (const HostGlobal& global : globals_)
        if (global.name == name) return &global;
    return nullptr;
}

const HostBinding* HostSchema::FindBinding(HostBindingId id) const
{
    return id != InvalidHostBinding && static_cast<std::size_t>(id) <= bindings_.size() ?
        &bindings_[static_cast<std::size_t>(id) - 1] : nullptr;
}

bool HostSchema::Validate(std::string& error) const
{
    error.clear();
    lastError_.clear();
    std::set<TypeId> objectTypes;
    std::set<std::string> objectNames;
    for (const Object& object : objects_)
    {
        if (!IsNameAllowed(object.name) || types_.Get(object.type).kind != TypeKind::Native ||
            types_.Get(object.type).name != object.name || !objectTypes.insert(object.type).second ||
            !objectNames.insert(object.name).second)
        {
            error = "host object type registry no longer matches its schema";
            return Fail(error);
        }
        std::set<std::string> names;
        for (const HostBinding& binding : bindings_)
            if (binding.ownerType == object.type && binding.kind == HostBindingKind::Method)
                if (!names.insert(binding.name).second)
                {
                    error = "duplicate host method name";
                    return Fail(error);
                }
        for (const HostProperty& property : object.properties)
        {
            const HostBinding* getter = FindBinding(property.getter);
            const HostBinding* setter = FindBinding(property.setter);
            if (!IsNameAllowed(property.name) || !IsSupportedType(property.type, false) ||
                !names.insert(property.name).second || (getter == nullptr && setter == nullptr) ||
                (property.getter != InvalidHostBinding && (getter == nullptr ||
                    getter->ownerType != object.type || getter->name != property.name ||
                    getter->kind != HostBindingKind::Getter || getter->returnType != property.type ||
                    !getter->parameters.empty())) ||
                (property.setter != InvalidHostBinding && (setter == nullptr ||
                    setter->ownerType != object.type || setter->name != property.name ||
                    setter->kind != HostBindingKind::Setter || setter->returnType != types_.Void() ||
                    setter->parameters.size() != 1 || setter->parameters[0] != property.type)))
            {
                error = "invalid host property contract";
                return Fail(error);
            }
        }
    }
    for (std::size_t index = 0; index < bindings_.size(); ++index)
    {
        const HostBinding& binding = bindings_[index];
        if (binding.id != index + 1 || types_.Get(binding.ownerType).kind != TypeKind::Native ||
            !IsSupportedType(binding.ownerType, false) || !IsNameAllowed(binding.name) ||
            !IsSupportedType(binding.returnType, true))
        {
            error = "invalid host binding contract";
            return Fail(error);
        }
        for (TypeId parameter : binding.parameters)
            if (!IsSupportedType(parameter, false))
            {
                error = "unsupported host binding parameter type";
                return Fail(error);
            }
        if (binding.kind != HostBindingKind::Method)
        {
            const HostProperty* property = FindProperty(binding.ownerType, binding.name);
            if (property == nullptr || (binding.kind == HostBindingKind::Getter ?
                property->getter != binding.id : binding.kind != HostBindingKind::Setter ||
                property->setter != binding.id))
            {
                error = "host accessor is not owned by its property";
                return Fail(error);
            }
        }
    }
    std::set<std::string> globalNames;
    for (const HostGlobal& global : globals_)
    {
        TypeId objectType = global.type;
        if (types_.Get(objectType).kind == TypeKind::Nullable) objectType = types_.Get(objectType).element;
        if (!IsNameAllowed(global.name) || !globalNames.insert(global.name).second ||
            types_.Get(objectType).kind != TypeKind::Native || !IsSupportedType(global.type, false))
        {
            error = "invalid host global contract";
            return Fail(error);
        }
    }
    return true;
}

}
