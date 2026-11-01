#include "NeoCoreImage.h"
#include "NeoCoreHostSchema.h"

#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>

namespace NeoCore
{
namespace Image
{
namespace
{
// 2: nullable 타입 종류를 제거했다. null 가능성은 타입 종류가 정한다.
// 3: 구조체 필드의 선언 기본값을 타입 표에 담는다.
// 4: 생성 코드의 행 매핑에서 0은 사용자 호출 위치를 따라가는 도우미를 뜻한다.
const std::uint32_t Version = 4;
const char Magic[] = {'N', 'E', 'O', 'C', 'O', 'R', 'E', '\0'};

struct Writer
{
    std::vector<std::uint8_t> bytes;
    void U32(std::uint32_t value)
    {
        for (unsigned i = 0; i < 4; ++i) bytes.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
    }
    void U64(std::uint64_t value)
    {
        for (unsigned i = 0; i < 8; ++i) bytes.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
    }
    void Count(std::size_t value)
    {
        if (value > (std::numeric_limits<std::uint32_t>::max)()) throw std::runtime_error("NeoCore image count exceeds format limit");
        U32(static_cast<std::uint32_t>(value));
    }
    void Text(const std::string& value)
    {
        Count(value.size());
        bytes.insert(bytes.end(), value.begin(), value.end());
    }
    void Blob(const std::vector<std::uint8_t>& value)
    {
        Count(value.size());
        bytes.insert(bytes.end(), value.begin(), value.end());
    }
    void Ids(const std::vector<TypeId>& values)
    {
        Count(values.size());
        for (TypeId id : values) U32(id);
    }
};

struct Reader
{
    const std::vector<std::uint8_t>& bytes;
    std::size_t position = 0;
    explicit Reader(const std::vector<std::uint8_t>& input) : bytes(input) {}
    std::size_t Remaining() const { return bytes.size() - position; }
    void Need(std::size_t size) const
    {
        if (size > Remaining()) throw std::runtime_error("truncated NeoCore image");
    }
    std::uint32_t U32()
    {
        Need(4);
        std::uint32_t result = 0;
        for (unsigned i = 0; i < 4; ++i) result |= static_cast<std::uint32_t>(bytes[position++]) << (i * 8);
        return result;
    }
    std::uint64_t U64()
    {
        Need(8);
        std::uint64_t result = 0;
        for (unsigned i = 0; i < 8; ++i) result |= static_cast<std::uint64_t>(bytes[position++]) << (i * 8);
        return result;
    }
    std::uint32_t Count(std::size_t minimumBytes = 1)
    {
        const std::uint32_t count = U32();
        if (minimumBytes && count > Remaining() / minimumBytes) throw std::runtime_error("invalid NeoCore image count");
        return count;
    }
    bool Flag()
    {
        const std::uint32_t value = U32();
        if (value > 1) throw std::runtime_error("invalid NeoCore image flag");
        return value != 0;
    }
    int Positive()
    {
        const std::uint32_t value = U32();
        if (!value || value > static_cast<std::uint32_t>((std::numeric_limits<int>::max)()))
            throw std::runtime_error("invalid NeoCore source position");
        return static_cast<int>(value);
    }
    std::string Text()
    {
        const std::uint32_t size = Count();
        const std::string value(bytes.begin() + position, bytes.begin() + position + size);
        position += size;
        return value;
    }
    std::vector<std::uint8_t> Blob()
    {
        const std::uint32_t size = Count();
        std::vector<std::uint8_t> value(bytes.begin() + position, bytes.begin() + position + size);
        position += size;
        return value;
    }
    std::vector<TypeId> Ids()
    {
        const std::uint32_t size = Count(4);
        std::vector<TypeId> values;
        values.reserve(size);
        for (std::uint32_t i = 0; i < size; ++i) values.push_back(U32());
        return values;
    }
};

std::uint64_t Fingerprint(const std::vector<std::uint8_t>& bytes, std::size_t first = 0)
{
    std::uint64_t value = UINT64_C(14695981039346656037);
    for (std::size_t i = first; i < bytes.size(); ++i)
    {
        value ^= bytes[i];
        value *= UINT64_C(1099511628211);
    }
    return value;
}

std::size_t TypeCount(const TypeRegistry& types)
{
    TypeId id = 1;
    while (types.Get(id).kind != TypeKind::Invalid) ++id;
    return id;
}

void WriteTypes(Writer& writer, const TypeRegistry& types)
{
    const std::size_t count = TypeCount(types);
    writer.Count(count);
    for (TypeId id = 0; id < count; ++id)
    {
        const TypeDesc& type = types.Get(id);
        writer.U32(static_cast<std::uint32_t>(type.kind));
        writer.U32(type.element);
        writer.U32(type.key);
        writer.U32(type.value);
        writer.U32(type.returnType);
        writer.Ids(type.parameters);
        writer.Text(type.name);
        writer.U32(type.fieldsDefined ? 1 : 0);
        writer.Count(type.fields.size());
        for (const StructField& field : type.fields)
        {
            writer.Text(field.name); writer.U32(field.type);
            writer.U32(field.hasDefault ? 1 : 0);
            if (!field.hasDefault) continue;
            writer.U64(static_cast<std::uint64_t>(field.defaultInteger));
            std::uint64_t bits = 0;
            std::memcpy(&bits, &field.defaultNumber, sizeof(bits));
            writer.U64(bits);
            writer.Text(field.defaultText);
            for (int i = 0; i < 4; ++i)
            {
                std::uint32_t component = 0;
                std::memcpy(&component, &field.defaultVector[i], sizeof(component));
                writer.U32(component);
            }
        }
    }
}

TypeId ReadTypeId(Reader& reader, const TypeRegistry& types, bool allowVoid)
{
    const TypeId id = reader.U32();
    if (!types.IsStorable(id) && !(allowVoid && id == types.Void()))
        throw std::runtime_error("invalid NeoCore image type reference");
    return id;
}

void ReadTypes(Reader& reader, TypeRegistry& types)
{
    const std::uint32_t count = reader.Count(36);
    const std::size_t builtins = TypeCount(types);
    if (count < builtins) throw std::runtime_error("missing NeoCore image builtin types");
    std::vector<TypeDesc> descriptors;
    descriptors.reserve(count);
    std::vector<unsigned> depths;
    depths.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        TypeDesc type;
        const std::uint32_t kind = reader.U32();
        if (kind > static_cast<std::uint32_t>(TypeKind::Coroutine)) throw std::runtime_error("invalid NeoCore image type kind");
        type.kind = static_cast<TypeKind>(kind);
        type.element = reader.U32();
        type.key = reader.U32();
        type.value = reader.U32();
        type.returnType = reader.U32();
        type.parameters = reader.Ids();
        type.name = reader.Text();
        type.fieldsDefined = reader.Flag();
        const std::uint32_t fields = reader.Count(8);
        for (std::uint32_t field = 0; field < fields; ++field)
        {
            const std::string name = reader.Text();
            StructField entry(name, reader.U32());
            entry.hasDefault = reader.Flag();
            if (entry.hasDefault)
            {
                entry.defaultInteger = static_cast<std::int64_t>(reader.U64());
                const std::uint64_t bits = reader.U64();
                std::memcpy(&entry.defaultNumber, &bits, sizeof(bits));
                entry.defaultText = reader.Text();
                for (int i = 0; i < 4; ++i)
                {
                    const std::uint32_t component = reader.U32();
                    std::memcpy(&entry.defaultVector[i], &component, sizeof(component));
                }
            }
            type.fields.push_back(entry);
        }
        TypeId created = InvalidType;
        if (index < builtins) created = index;
        else
        {
            switch (type.kind)
            {
            case TypeKind::Array: created = types.ArrayOf(type.element); break;
            case TypeKind::Array32: created = types.Array32Of(type.element); break;
            case TypeKind::List: created = types.ListOf(type.element); break;
            case TypeKind::Map: created = types.MapOf(type.key, type.value); break;
            case TypeKind::Set: created = types.SetOf(type.element); break;
            case TypeKind::Function: created = types.FunctionOf(type.returnType, type.parameters); break;
            case TypeKind::Struct: created = types.DeclareStruct(type.name); break;
            case TypeKind::Native: created = types.DeclareNative(type.name); break;
            case TypeKind::BuiltinObject: created = types.DeclareBuiltinObject(type.name); break;
            case TypeKind::Coroutine: created = types.CoroutineOf(type.element); break;
            default: break;
            }
        }
        if (created != index || (index != 0 && created == InvalidType))
            throw std::runtime_error("noncanonical NeoCore image type table");
        const TypeDesc& actual = types.Get(created);
        if (actual.kind != type.kind || actual.element != type.element || actual.key != type.key ||
            actual.value != type.value || actual.returnType != type.returnType || actual.parameters != type.parameters || actual.name != type.name ||
            (type.kind != TypeKind::Struct && (type.fieldsDefined != actual.fieldsDefined || !type.fields.empty())))
            throw std::runtime_error("inconsistent NeoCore image type descriptor");
        unsigned depth = 1;
        std::vector<TypeId> dependencies;
        switch (type.kind)
        {
        case TypeKind::Array:
        case TypeKind::Array32:
        case TypeKind::List: dependencies.push_back(type.element); break;
        case TypeKind::Map: dependencies.push_back(type.key); dependencies.push_back(type.value); break;
        case TypeKind::Set: dependencies.push_back(type.element); break;
        case TypeKind::Function:
            dependencies = type.parameters;
            dependencies.push_back(type.returnType);
            break;
        case TypeKind::Coroutine: dependencies.push_back(type.element); break;
        default: break;
        }
        for (TypeId dependency : dependencies)
        {
            if (dependency >= depths.size()) throw std::runtime_error("cyclic NeoCore image type graph");
            if (depth <= depths[dependency]) depth = depths[dependency] + 1;
        }
        // 소스 파서와 같은 합성 타입 깊이 한도를 적용해 진단 중 재귀 폭주를 막는다.
        if (depth > 256) throw std::runtime_error("NeoCore image type nesting exceeds compiler limit");
        depths.push_back(depth);
        descriptors.push_back(type);
    }
    // 구조체는 이름을 모두 복구한 뒤 필드를 확정하여 전방 참조와 값 순환 검사를 보존한다.
    for (TypeId id = 0; id < count; ++id)
    {
        const TypeDesc& type = descriptors[id];
        if (type.kind != TypeKind::Struct) continue;
        if (!type.fieldsDefined) throw std::runtime_error("incomplete NeoCore image struct");
        std::string error;
        if (!types.DefineStructFields(id, type.fields, &error)) throw std::runtime_error("invalid NeoCore image struct: " + error);
    }
}

std::vector<std::uint8_t> SchemaDescription(const HostSchema* schema)
{
    Writer writer;
    writer.U32(schema ? 1 : 0);
    if (!schema) return writer.bytes;
    WriteTypes(writer, schema->Types());
    writer.Count(schema->Bindings().size());
    for (const HostBinding& binding : schema->Bindings())
    {
        writer.U32(binding.id);
        writer.U32(binding.ownerType);
        writer.Text(binding.name);
        writer.U32(static_cast<std::uint32_t>(binding.kind));
        writer.U32(binding.returnType);
        writer.Ids(binding.parameters);
    }
    writer.Count(schema->Globals().size());
    for (const HostGlobal& global : schema->Globals()) { writer.Text(global.name); writer.U32(global.type); }
    // 기존 스키마의 이미지 계약은 유지하고 이벤트가 있을 때만 이름별 연결 정보를 덧붙인다.
    std::size_t events = 0;
    for (const HostBinding& binding : schema->Bindings())
        if (!binding.eventName.empty()) ++events;
    if (events != 0)
    {
        writer.Text("host-events");
        writer.Count(events);
        for (const HostBinding& binding : schema->Bindings())
            if (!binding.eventName.empty()) { writer.U32(binding.id); writer.Text(binding.eventName); }
    }
    return writer.bytes;
}

void WriteMetadata(Writer& writer, const Data& data)
{
    writer.Text(data.sourceName);
    writer.U32(data.debug ? 1 : 0);
    writer.Text(data.checked.hostPrefix);
    WriteTypes(writer, data.checked.types);
    writer.Count(data.checked.functions.size());
    for (const StaticFunctionSignature& function : data.checked.functions)
    {
        writer.Text(function.name);
        writer.U32(function.returnType);
        writer.Ids(function.parameterTypes);
        writer.U32(function.exported ? 1 : 0);
    }
    writer.Count(data.checked.globals.size());
    for (const StaticGlobalSignature& global : data.checked.globals)
    {
        writer.Text(global.name);
        writer.U32(global.type);
        writer.U32(global.exported ? 1 : 0);
    }
    writer.Count(data.checked.debugSymbols.size());
    for (const StaticDebugSymbol& symbol : data.checked.debugSymbols)
    {
        writer.Text(symbol.backendName);
        writer.Text(symbol.name);
        writer.U32(symbol.type);
        writer.U32(symbol.boxed ? 1 : 0);
    }
    writer.Count(data.checked.hostWriteSourceLines.size());
    for (const auto& line : data.checked.hostWriteSourceLines)
    {
        writer.U32(static_cast<std::uint32_t>(line.first));
        writer.U32(static_cast<std::uint32_t>(line.second));
    }
    writer.Count(data.bundle.lines.size());
    for (const ModuleSourceLine& line : data.bundle.lines)
    {
        writer.Text(line.sourceName);
        writer.U32(static_cast<std::uint32_t>(line.line));
        writer.Count(line.columns.size());
        for (const ModuleSourceColumn& column : line.columns)
        {
            writer.U32(static_cast<std::uint32_t>(column.generatedColumn));
            writer.U32(static_cast<std::uint32_t>(column.sourceColumn));
            writer.U32(column.fixed ? 1 : 0);
        }
    }
    writer.Count(data.bundle.symbolNames.size());
    for (const auto& name : data.bundle.symbolNames) { writer.Text(name.first); writer.Text(name.second); }
}

void ReadMetadata(Reader& reader, Data& data)
{
    data.sourceName = reader.Text();
    data.debug = reader.Flag();
    data.checked.hostPrefix = reader.Text();
    if (!data.checked.hostPrefix.empty() &&
        data.checked.hostPrefix.compare(0, std::strlen(InternalSymbolPrefix()), InternalSymbolPrefix()) != 0)
        throw std::runtime_error("invalid NeoCore image host prefix");
    ReadTypes(reader, data.checked.types);
    const std::uint32_t functions = reader.Count(16);
    std::set<std::string> names;
    for (std::uint32_t i = 0; i < functions; ++i)
    {
        StaticFunctionSignature function;
        function.name = reader.Text();
        if (function.name.empty() || !names.insert(function.name).second) throw std::runtime_error("duplicate NeoCore image function");
        function.returnType = ReadTypeId(reader, data.checked.types, true);
        function.parameterTypes = reader.Ids();
        for (TypeId parameter : function.parameterTypes)
            if (!data.checked.types.IsStorable(parameter)) throw std::runtime_error("invalid NeoCore image parameter type");
        function.exported = reader.Flag();
        data.checked.functions.push_back(function);
    }
    const std::uint32_t globals = reader.Count(12);
    names.clear();
    for (std::uint32_t i = 0; i < globals; ++i)
    {
        StaticGlobalSignature global;
        global.name = reader.Text();
        if (global.name.empty() || !names.insert(global.name).second) throw std::runtime_error("duplicate NeoCore image global");
        global.type = ReadTypeId(reader, data.checked.types, false);
        global.exported = reader.Flag();
        data.checked.globals.push_back(global);
    }
    const std::uint32_t symbols = reader.Count(16);
    for (std::uint32_t i = 0; i < symbols; ++i)
    {
        StaticDebugSymbol symbol;
        symbol.backendName = reader.Text();
        symbol.name = reader.Text();
        symbol.type = ReadTypeId(reader, data.checked.types, false);
        symbol.boxed = reader.Flag();
        if (symbol.name.empty() || symbol.backendName.empty()) throw std::runtime_error("invalid NeoCore image debug symbol");
        data.checked.debugSymbols.push_back(symbol);
    }
    const std::uint32_t remaps = reader.Count(8);
    for (std::uint32_t i = 0; i < remaps; ++i)
    {
        const int generated = reader.Positive();
        const std::uint32_t original = reader.U32();
        if (original > (std::numeric_limits<std::uint16_t>::max)())
            throw std::runtime_error("invalid NeoCore source position");
        data.checked.hostWriteSourceLines.push_back(std::make_pair(generated, static_cast<int>(original)));
    }
    const std::uint32_t lines = reader.Count(12);
    for (std::uint32_t i = 0; i < lines; ++i)
    {
        ModuleSourceLine line;
        line.sourceName = reader.Text();
        line.line = reader.Positive();
        const std::uint32_t columns = reader.Count(12);
        for (std::uint32_t j = 0; j < columns; ++j)
        {
            ModuleSourceColumn column;
            column.generatedColumn = reader.Positive();
            column.sourceColumn = reader.Positive();
            column.fixed = reader.Flag();
            line.columns.push_back(column);
        }
        data.bundle.lines.push_back(line);
    }
    const std::uint32_t renamed = reader.Count(8);
    for (std::uint32_t i = 0; i < renamed; ++i)
    {
        const std::string backend = reader.Text();
        const std::string original = reader.Text();
        if (!data.bundle.symbolNames.insert(std::make_pair(backend, original)).second)
            throw std::runtime_error("duplicate NeoCore image symbol mapping");
    }
    data.checked.types.SetDiagnosticNames(data.bundle.symbolNames);
    data.checked.success = true;
}
}

bool Write(const Data& data, const HostSchema* schema, std::vector<std::uint8_t>& image, Error& error)
{
    image.clear();
    error = Error();
    error.sourceName = data.sourceName;
    try
    {
        Writer payload;
        const std::vector<std::uint8_t> description = SchemaDescription(schema);
        payload.U64(Fingerprint(description));
        payload.Blob(description);
        WriteMetadata(payload, data);
        payload.Blob(data.bytecode);
        Writer result;
        result.bytes.insert(result.bytes.end(), Magic, Magic + sizeof(Magic));
        result.U32(Version);
        result.Count(payload.bytes.size());
        result.U64(Fingerprint(payload.bytes));
        result.bytes.insert(result.bytes.end(), payload.bytes.begin(), payload.bytes.end());
        image.swap(result.bytes);
        return true;
    }
    catch (const std::exception& failure) { error.message = failure.what(); return false; }
}

bool Read(const std::vector<std::uint8_t>& image, const HostSchema* schema, Data& data, Error& error)
{
    error = Error();
    try
    {
        Reader reader(image);
        reader.Need(sizeof(Magic));
        if (std::memcmp(image.data(), Magic, sizeof(Magic)) != 0) throw std::runtime_error("not a NeoCore program image");
        reader.position = sizeof(Magic);
        if (reader.U32() != Version) throw std::runtime_error("NeoCore image version mismatch");
        const std::uint32_t payloadSize = reader.U32();
        const std::uint64_t digest = reader.U64();
        if (payloadSize != reader.Remaining()) throw std::runtime_error("invalid NeoCore image payload length");
        if (digest != Fingerprint(image, reader.position)) throw std::runtime_error("NeoCore image checksum mismatch");
        const std::uint64_t schemaDigest = reader.U64();
        const std::vector<std::uint8_t> storedSchema = reader.Blob();
        const std::vector<std::uint8_t> expectedSchema = SchemaDescription(schema);
        if (schemaDigest != Fingerprint(storedSchema) || storedSchema != expectedSchema)
            throw std::runtime_error("NeoCore image host schema mismatch");
        Data restored;
        ReadMetadata(reader, restored);
        restored.bytecode = reader.Blob();
        if (reader.Remaining() || restored.bytecode.empty() ||
            restored.bytecode.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
            throw std::runtime_error("invalid NeoCore bytecode payload");
        data = std::move(restored);
        return true;
    }
    catch (const std::exception& failure) { error.message = failure.what(); return false; }
}
}
}
