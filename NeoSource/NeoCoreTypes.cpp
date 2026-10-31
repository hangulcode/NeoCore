#include "NeoCoreTypes.h"

#include <limits>
#include <set>

namespace NeoCore
{

bool TypeRegistry::Key::operator<(const Key& other) const
{
    if (kind != other.kind) return kind < other.kind;
    if (arguments != other.arguments) return arguments < other.arguments;
    return name < other.name;
}

TypeRegistry::TypeRegistry()
{
    static const char* const names[] = {
        "<invalid>", "void", "int", "float", "bool", "string", "null",
        "Vector2", "Vector3", "Vector4", "Quaternion"
    };
    for (TypeId i = 0; i <= static_cast<TypeId>(TypeKind::Quaternion); ++i)
    {
        TypeDesc type;
        type.kind = static_cast<TypeKind>(i);
        type.name = names[i];
        types_.push_back(type);
        if (type.kind != TypeKind::Invalid && type.kind != TypeKind::Null)
            names_.insert(std::make_pair(type.name, i));
    }
    DeclareBuiltinObject("TimeSpan");
    DeclareBuiltinObject("Stopwatch");
    DeclareBuiltinObject("AsyncRequest");
}

TypeId TypeRegistry::LookupBuiltin(const std::string& name) const
{
    const TypeId id = LookupNamed(name);
    return id <= static_cast<TypeId>(TypeKind::Quaternion) ? id : InvalidType;
}

TypeId TypeRegistry::LookupNamed(const std::string& qualifiedName) const
{
    const auto found = names_.find(qualifiedName);
    return found == names_.end() ? InvalidType : found->second;
}

bool TypeRegistry::IsNumeric(TypeId id) const
{
    return id == Int() || id == Float();
}

bool TypeRegistry::CanAssign(TypeId target, TypeId source) const
{
    if (!IsStorable(target) || Get(source).kind == TypeKind::Invalid || source == Void())
        return false;
    if (target == source)
        return true;
    if (target == Float() && source == Int())
        return true;

    const TypeDesc& destination = Get(target);
    if (destination.kind == TypeKind::Nullable)
    {
        if (source == Null())
            return true;
        // null을 포함하는 숫자의 승격에는 조건부 변환이 필요하므로 정확히 같은 nullable만 허용한다.
        if (IsNullable(source))
            return false;
        return CanAssign(destination.element, source);
    }
    // 가변 컬렉션의 원소 승격과 함수 시그니처의 변형은 허용하지 않는다.
    return false;
}

TypeId TypeRegistry::CommonNumericType(TypeId left, TypeId right) const
{
    if (!IsNumeric(left) || !IsNumeric(right))
        return InvalidType;
    return left == Float() || right == Float() ? Float() : Int();
}

TypeId TypeRegistry::Intern(TypeKind kind, const std::vector<TypeId>& arguments, const std::string& name)
{
    Key key;
    key.kind = kind;
    key.arguments = arguments;
    key.name = name;
    const auto found = interned_.find(key);
    if (found != interned_.end())
        return found->second;
    if (types_.size() >= std::numeric_limits<TypeId>::max())
        return InvalidType;

    TypeDesc desc;
    desc.kind = kind;
    desc.name = name;
    switch (kind)
    {
    case TypeKind::Nullable:
    case TypeKind::Array:
    case TypeKind::Array32:
    case TypeKind::List:
        desc.element = arguments[0];
        break;
    case TypeKind::Map:
        desc.key = arguments[0];
        desc.value = arguments[1];
        break;
    case TypeKind::Set:
        desc.element = arguments[0];
        break;
    case TypeKind::Function:
        desc.returnType = arguments[0];
        desc.parameters.assign(arguments.begin() + 1, arguments.end());
        break;
    case TypeKind::Struct:
    case TypeKind::Native:
    case TypeKind::BuiltinObject:
        break;
    case TypeKind::Coroutine:
        desc.element = arguments[0];
        break;
    default:
        return InvalidType;
    }

    const TypeId id = static_cast<TypeId>(types_.size());
    // deque가 재할당되어도 기존 타입 기술자를 참조하는 AST는 유효하다.
    types_.push_back(desc);
    interned_.insert(std::make_pair(key, id));
    return id;
}

TypeId TypeRegistry::Unary(TypeKind kind, TypeId element)
{
    if (!IsStorable(element))
        return InvalidType;
    return Intern(kind, std::vector<TypeId>(1, element), std::string());
}

TypeId TypeRegistry::NullableOf(TypeId element)
{
    if (IsNullable(element))
        return InvalidType;
    return Unary(TypeKind::Nullable, element);
}

TypeId TypeRegistry::ArrayOf(TypeId element)
{
    return Unary(TypeKind::Array, element);
}

TypeId TypeRegistry::Array32Of(TypeId element)
{
    if (!IsNumeric(element))
        return InvalidType;
    return Unary(TypeKind::Array32, element);
}

TypeId TypeRegistry::ListOf(TypeId element)
{
    return Unary(TypeKind::List, element);
}

TypeId TypeRegistry::MapOf(TypeId key, TypeId value)
{
    if (!IsStorable(key) || !IsStorable(value))
        return InvalidType;
    std::vector<TypeId> arguments;
    arguments.push_back(key);
    arguments.push_back(value);
    return Intern(TypeKind::Map, arguments, std::string());
}

TypeId TypeRegistry::SetOf(TypeId element)
{
    return Unary(TypeKind::Set, element);
}

TypeId TypeRegistry::FunctionOf(TypeId returnType, const std::vector<TypeId>& parameters)
{
    if (returnType != Void() && !IsStorable(returnType))
        return InvalidType;
    std::vector<TypeId> arguments;
    arguments.reserve(parameters.size() + 1);
    arguments.push_back(returnType);
    for (TypeId parameter : parameters)
    {
        if (!IsStorable(parameter))
            return InvalidType;
        arguments.push_back(parameter);
    }
    return Intern(TypeKind::Function, arguments, std::string());
}

TypeId TypeRegistry::DeclareNominal(TypeKind kind, const std::string& qualifiedName)
{
    if (qualifiedName.empty() || qualifiedName == "int32" || qualifiedName == "float32" ||
        qualifiedName == "any" || qualifiedName == "null" || qualifiedName == "var" || qualifiedName == "fun")
        return InvalidType;
    const TypeId previous = LookupNamed(qualifiedName);
    if (previous != InvalidType)
        return Get(previous).kind == kind ? previous : InvalidType;
    const TypeId id = Intern(kind, std::vector<TypeId>(), qualifiedName);
    if (id != InvalidType)
        names_.insert(std::make_pair(qualifiedName, id));
    return id;
}

TypeId TypeRegistry::CoroutineOf(TypeId function)
{
    if (Get(function).kind != TypeKind::Function || Get(function).returnType != Void()) return InvalidType;
    return Unary(TypeKind::Coroutine, function);
}

TypeId TypeRegistry::DeclareStruct(const std::string& qualifiedName)
{
    return DeclareNominal(TypeKind::Struct, qualifiedName);
}

TypeId TypeRegistry::DeclareNative(const std::string& qualifiedName)
{
    return DeclareNominal(TypeKind::Native, qualifiedName);
}

TypeId TypeRegistry::DeclareBuiltinObject(const std::string& qualifiedName)
{
    return DeclareNominal(TypeKind::BuiltinObject, qualifiedName);
}

bool TypeRegistry::ContainsStructValue(TypeId value, TypeId structure) const
{
    // 깊은 값 타입 연결도 C++ 호출 스택을 사용하지 않고 검사한다.
    std::vector<TypeId> pending(1, value);
    std::set<TypeId> visited;
    while (!pending.empty())
    {
        const TypeId current = pending.back();
        pending.pop_back();
        if (current == structure) return true;
        if (!visited.insert(current).second) continue;
        const TypeDesc& type = Get(current);
        if (type.kind == TypeKind::Nullable)
            pending.push_back(type.element);
        else if (type.kind == TypeKind::Struct)
        {
            for (const StructField& field : type.fields)
                pending.push_back(field.type);
        }
        // 컬렉션과 함수·호스트 객체는 별도 저장소를 가리키므로 인라인 값 순환의 연결이 아니다.
    }
    return false;
}

bool TypeRegistry::DefineStructFields(TypeId structure, const std::vector<StructField>& fields, std::string* error)
{
    if (error) error->clear();
    if (Get(structure).kind != TypeKind::Struct)
    {
        if (error) *error = "field definitions require a nominal struct type";
        return false;
    }
    if (Get(structure).fieldsDefined)
    {
        if (error) *error = "struct fields are already defined";
        return false;
    }
    std::set<std::string> names;
    for (const StructField& field : fields)
    {
        if (field.name.empty())
        {
            if (error) *error = "a struct field requires a name";
            return false;
        }
        if (!names.insert(field.name).second)
        {
            if (error) *error = "duplicate struct field '" + field.name + "'";
            return false;
        }
        if (!IsStorable(field.type))
        {
            if (error) *error = "struct field '" + field.name + "' requires a storable value type";
            return false;
        }
        if (ContainsStructValue(field.type, structure))
        {
            if (error) *error = "struct field '" + field.name + "' creates a recursive value type";
            return false;
        }
    }
    // 검증·복사가 모두 끝난 후 교체하여 실패한 정의가 부분적인 필드를 남기지 않게 한다.
    std::vector<StructField> complete(fields);
    TypeDesc& type = types_[structure];
    type.fields.swap(complete);
    type.fieldsDefined = true;
    return true;
}

const StructField* TypeRegistry::FindStructField(TypeId structure, const std::string& name, std::size_t* index) const
{
    if (index) *index = InvalidFieldIndex;
    const TypeDesc& type = Get(structure);
    if (type.kind != TypeKind::Struct || !type.fieldsDefined) return nullptr;
    for (std::size_t i = 0; i < type.fields.size(); ++i)
    {
        if (type.fields[i].name != name) continue;
        if (index) *index = i;
        return &type.fields[i];
    }
    return nullptr;
}

std::string TypeRegistry::DisplayName(TypeId id) const
{
    const TypeDesc& type = Get(id);
    switch (type.kind)
    {
    case TypeKind::Invalid:
    case TypeKind::Void:
    case TypeKind::Int:
    case TypeKind::Float:
    case TypeKind::Bool:
    case TypeKind::String:
    case TypeKind::Null:
    case TypeKind::Vector2:
    case TypeKind::Vector3:
    case TypeKind::Vector4:
    case TypeKind::Quaternion:
        return type.name;
    case TypeKind::Nullable:
        return DisplayName(type.element) + "?";
    case TypeKind::Array:
        return "array<" + DisplayName(type.element) + ">";
    case TypeKind::Array32:
        return "array32<" + DisplayName(type.element) + ">";
    case TypeKind::List:
        return "list<" + DisplayName(type.element) + ">";
    case TypeKind::Map:
        return "map<" + DisplayName(type.key) + ", " + DisplayName(type.value) + ">";
    case TypeKind::Set:
        return "set<" + DisplayName(type.element) + ">";
    case TypeKind::Function:
    {
        std::string result = DisplayName(type.returnType) + " function(";
        for (std::size_t i = 0; i < type.parameters.size(); ++i)
        {
            if (i != 0) result += ", ";
            result += DisplayName(type.parameters[i]);
        }
        return result + ")";
    }
    case TypeKind::Struct:
    case TypeKind::Native:
    case TypeKind::BuiltinObject:
        return type.name;
    case TypeKind::Coroutine:
        return "coroutine<" + DisplayName(type.element) + ">";
    default:
        return "<invalid>";
    }
}

}
