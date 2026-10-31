#include "NeoCoreHostSchema.h"

#include <limits>
#include <map>
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
        "sleep", "yield", "list", "map", "array", "array32", "set",
        "math", "system"
    };
    for (const char* value : reserved)
        if (name == value) return false;
    return types_.LookupBuiltin(name) == InvalidType;
}

bool HostSchema::IsSupportedType(TypeId type, bool allowVoid) const
{
    // list<Node> 같은 간접 순환은 허용하되 타입마다 한 번만 검사한다.
    // 반복 탐색으로 깊은 호스트 타입 그래프도 C++ 호출 스택을 소모하지 않는다.
    std::vector<std::pair<TypeId, bool>> pending(1, std::make_pair(type, allowVoid));
    std::set<std::pair<TypeId, bool>> visited;
    while (!pending.empty())
    {
        const std::pair<TypeId, bool> current = pending.back();
        pending.pop_back();
        if (!visited.insert(current).second) continue;
        const TypeDesc& descriptor = types_.Get(current.first);
        switch (descriptor.kind)
        {
        case TypeKind::Void:
            if (!current.second) return false;
            break;
        case TypeKind::Int:
        case TypeKind::Float:
        case TypeKind::Bool:
        case TypeKind::String:
        case TypeKind::Vector2:
        case TypeKind::Vector3:
        case TypeKind::Vector4:
        case TypeKind::Quaternion:
            break;
        case TypeKind::Nullable:
            if (types_.Get(descriptor.element).kind == TypeKind::Nullable) return false;
            pending.push_back(std::make_pair(descriptor.element, false));
            break;
        case TypeKind::Array:
        case TypeKind::Array32:
        case TypeKind::List:
            pending.push_back(std::make_pair(descriptor.element, false));
            break;
        case TypeKind::Map:
            pending.push_back(std::make_pair(descriptor.key, false));
            pending.push_back(std::make_pair(descriptor.value, false));
            break;
        case TypeKind::Set:
            pending.push_back(std::make_pair(descriptor.element, false));
            break;
        case TypeKind::Function:
            pending.push_back(std::make_pair(descriptor.returnType, true));
            for (TypeId parameter : descriptor.parameters) pending.push_back(std::make_pair(parameter, false));
            break;
        case TypeKind::Struct:
            if (!descriptor.fieldsDefined) return false;
            for (const StructField& field : descriptor.fields) pending.push_back(std::make_pair(field.type, false));
            break;
        case TypeKind::Native:
        {
            const Object* object = FindObject(current.first);
            if (object == nullptr || object->name != descriptor.name) return false;
            break;
        }
        default:
            return false;
        }
    }
    return true;
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

TypeId HostSchema::AddStruct(const std::string& name)
{
    lastError_.clear();
    if(!IsNameAllowed(name) || types_.LookupNamed(name)!=InvalidType)
    { Fail("host struct name must be an unused non-reserved identifier"); return InvalidType; }
    return types_.DeclareStruct(name);
}
bool HostSchema::DefineStruct(TypeId type, const std::vector<StructField>& fields)
{
    lastError_.clear();
    for(const auto& field:fields)
        if(!IsNameAllowed(field.name)) return Fail("host struct field must be a non-reserved identifier");
    return types_.DefineStructFields(type,fields,&lastError_);
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

HostBindingId HostSchema::AddEvent(TypeId owner, const std::string& eventName, TypeId callbackType,
    const std::string& registrationMethod)
{
    lastError_.clear();
    if (types_.Get(owner).kind != TypeKind::Native || !IsSupportedType(owner, false))
    {
        Fail("host event owner must be an object registered by AddObject");
        return InvalidHostBinding;
    }
    if (!IsNameAllowed(registrationMethod) || eventName.empty() || eventName.find('\0') != std::string::npos)
    {
        Fail("host event needs a registration method identifier and a nonempty name without NUL");
        return InvalidHostBinding;
    }
    const HostBinding* member = FindMethod(owner, registrationMethod);
    if (FindProperty(owner, registrationMethod) || (member && member->eventName.empty()))
    {
        Fail("host event registration method conflicts with an existing member");
        return InvalidHostBinding;
    }
    if (FindEvent(owner, registrationMethod, eventName))
    {
        Fail("host event name is already declared for this registration method");
        return InvalidHostBinding;
    }
    if (types_.Get(callbackType).kind != TypeKind::Function || !IsSupportedType(callbackType, false))
    {
        Fail("host event callback must be a nonnullable function type");
        return InvalidHostBinding;
    }
    if (!CheckBindingCapacity(1)) return InvalidHostBinding;
    HostBinding binding;
    binding.id = static_cast<HostBindingId>(bindings_.size() + 1);
    binding.ownerType = owner;
    binding.name = registrationMethod;
    binding.returnType = types_.Void();
    binding.parameters.push_back(types_.String());
    binding.parameters.push_back(callbackType);
    binding.eventName = eventName;
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

const HostBinding* HostSchema::FindEvent(TypeId owner, const std::string& registrationMethod,
    const std::string& eventName) const
{
    if (eventName.empty()) return nullptr;
    for (const HostBinding& binding : bindings_)
        if (binding.ownerType == owner && binding.kind == HostBindingKind::Method &&
            binding.name == registrationMethod && binding.eventName == eventName)
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
        std::map<std::string, std::set<std::string>> eventNames;
        for (const HostBinding& binding : bindings_)
            if (binding.ownerType == object.type && binding.kind == HostBindingKind::Method)
            {
                // 같은 등록 메서드의 이벤트만 이름별 서명을 갖는다. 일반 오버로드는 허용하지 않는다.
                const bool event = !binding.eventName.empty();
                if ((!event || eventNames.count(binding.name) == 0) && !names.insert(binding.name).second)
                {
                    error = "duplicate host method name";
                    return Fail(error);
                }
                if (event && !eventNames[binding.name].insert(binding.eventName).second)
                {
                    error = "duplicate host event name";
                    return Fail(error);
                }
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
        if (!binding.eventName.empty() && (binding.kind != HostBindingKind::Method ||
            binding.eventName.find('\0') != std::string::npos || binding.returnType != types_.Void() ||
            binding.parameters.size() != 2 || binding.parameters[0] != types_.String() ||
            types_.Get(binding.parameters[1]).kind != TypeKind::Function))
        {
            error = "invalid host event registration contract";
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
