struct Scalar::BoundValue
{
    NeoScript::IRuntime* runtime = nullptr;
    const TypeRegistry* types = nullptr;
    TypeId type = InvalidType;
    std::shared_ptr<void> lifetime;
    NeoCoreBackend::ValuePtr value;
};

static TypeId Underlying(const TypeRegistry& types, TypeId type)
{ return types.IsNullable(type) ? types.Get(type).element : type; }

std::string Scalar::typeName() const
{
    if (bound_) return bound_->types->DisplayName(bound_->type);
    if (kind_ == Kind::Object) return objectType();
    TypeRegistry types;
    return types.DisplayName(ScalarType(types, kind_));
}

bool BoundaryAccess::Accepts(const TypeRegistry& types, TypeId type, const Scalar& value)
{
    if (value.kind() == Scalar::Kind::Void) return type == types.Void();
    if (value.kind() == Scalar::Kind::Null) return types.IsNullable(type);
    type = Underlying(types, type);
    if (value.bound_) return value.typeName() == types.DisplayName(type);
    if (value.kind() == Scalar::Kind::Object)
        return types.Get(type).kind == TypeKind::Native && value.objectType() == types.Get(type).name;
    return types.CanAssign(type, ScalarType(types, value.kind()));
}

static NeoCoreBackend::ArrayStorage StorageOf(const TypeRegistry& types, TypeId type)
{
    using NeoCoreBackend::ArrayStorage;
    const TypeDesc& desc = types.Get(type);
    if (desc.kind == TypeKind::Array32)
        return types.Get(desc.element).kind == TypeKind::Int ? ArrayStorage::Int32 : ArrayStorage::Float32;
    switch (types.Get(desc.element).kind)
    {
    case TypeKind::Int: return ArrayStorage::Int64;
    case TypeKind::Float: return ArrayStorage::Float64;
    case TypeKind::Bool: return ArrayStorage::Bool;
    case TypeKind::Vector2: return ArrayStorage::Vector2;
    case TypeKind::Vector3: return ArrayStorage::Vector3;
    case TypeKind::Vector4:
    case TypeKind::Quaternion: return ArrayStorage::Vector4;
    default: throw std::invalid_argument("unsupported typed array element");
    }
}

static NeoCoreBackend::ValuePtr NewCollection(NeoScript::IRuntime& runtime,
    const TypeRegistry& types, TypeId type, const std::shared_ptr<void>& lifetime)
{
    using NeoCoreBackend::ValueKind;
    using NeoCoreBackend::ArrayStorage;
    const TypeDesc& desc = types.Get(type);
    ValueKind kind;
    ArrayStorage storage = ArrayStorage::Int64;
    switch (desc.kind)
    {
    case TypeKind::Array:
    case TypeKind::Array32: kind = ValueKind::Array; storage = StorageOf(types,type); break;
    case TypeKind::List: kind = ValueKind::List; break;
    case TypeKind::Map: kind = ValueKind::Map; break;
    case TypeKind::Set: kind = ValueKind::Set; break;
    case TypeKind::Struct: kind = ValueKind::List; break;
    default: throw std::invalid_argument("type is not a collection or struct");
    }
    NeoCoreBackend::ValuePtr result = NeoCoreBackend::MakeCollection(runtime,kind,storage,lifetime);
    if (!result) throw std::runtime_error("could not allocate typed value");
    return result;
}

static Scalar DefaultBoundaryValue(NeoScript::IRuntime& runtime, const TypeRegistry& types,
    TypeId type, const std::shared_ptr<void>& lifetime)
{
    const TypeDesc& desc = types.Get(type);
    float vector[4] = {};
    switch (desc.kind)
    {
    case TypeKind::Int: return Scalar::Int(0);
    case TypeKind::Float: return Scalar::Float(0);
    case TypeKind::Bool: return Scalar::Bool(false);
    case TypeKind::Vector2: return Scalar::Vector(Scalar::Kind::Vector2,vector);
    case TypeKind::Vector3: return Scalar::Vector(Scalar::Kind::Vector3,vector);
    case TypeKind::Vector4: return Scalar::Vector(Scalar::Kind::Vector4,vector);
    case TypeKind::Quaternion: vector[0] = 1; return Scalar::Vector(Scalar::Kind::Quaternion,vector);
    case TypeKind::Nullable: return Scalar::Null();
    case TypeKind::Struct:
    {
        const auto result = NewCollection(runtime,types,type,lifetime);
        for (const StructField& field : desc.fields)
        {
            Scalar value = DefaultBoundaryValue(runtime,types,field.type,lifetime);
            if (!NeoCoreBackend::ValueAppend(result,BoundaryAccess::Copy(runtime,types,field.type,lifetime,value)))
                throw std::runtime_error("could not initialize struct field");
        }
        return BoundaryAccess::Wrap(runtime,types,type,lifetime,result);
    }
    default: throw std::invalid_argument("nonnullable reference requires an explicit initial value");
    }
}

NeoCoreBackend::ValuePtr BoundaryAccess::Copy(NeoScript::IRuntime& runtime,
    const TypeRegistry& types, TypeId type, const std::shared_ptr<void>& lifetime, const Scalar& value)
{
    using NeoCoreBackend::ValueKind;
    if (!Accepts(types,type,value)) throw std::invalid_argument("value requires " + types.DisplayName(type));
    type = Underlying(types,type);
    if (value.bound_)
    {
        if (value.bound_->runtime != &runtime || value.bound_->lifetime.get() != lifetime.get())
            throw std::invalid_argument("cannot transfer a value between independent programs");
        if (types.Get(type).kind != TypeKind::Struct) return value.bound_->value;
        // 구조체는 필드를 복사하되 내부 컬렉션의 공유 정체성은 유지한다.
        const auto result = NewCollection(runtime,types,type,lifetime);
        const auto& fields = types.Get(type).fields;
        for (std::size_t i = 0; i < fields.size(); ++i)
            if (!NeoCoreBackend::ValueAppend(result,Copy(runtime,types,fields[i].type,lifetime,
                Wrap(runtime,types,fields[i].type,lifetime,NeoCoreBackend::ValueAt(value.bound_->value,i)))))
                throw std::runtime_error("could not copy struct field");
        return result;
    }
    NeoCoreBackend::ValueData data;
    switch (value.kind())
    {
    case Scalar::Kind::Void: break;
    case Scalar::Kind::Int:
        if (type == types.Float()) { data.kind = ValueKind::Float; data.number = static_cast<double>(value.asInt()); }
        else { data.kind = ValueKind::Int; data.integer = value.asInt(); }
        break;
    case Scalar::Kind::Float: data.kind = ValueKind::Float; data.number = value.asFloat(); break;
    case Scalar::Kind::Bool: data.kind = ValueKind::Bool; data.integer = value.asBool(); break;
    case Scalar::Kind::String: data.kind = ValueKind::String; data.text = value.asString(); break;
    case Scalar::Kind::Null: data.kind = ValueKind::Null; break;
    case Scalar::Kind::Vector2: data.kind = ValueKind::Vector2; std::copy(value.asVector(),value.asVector()+2,data.vector); break;
    case Scalar::Kind::Vector3: data.kind = ValueKind::Vector3; std::copy(value.asVector(),value.asVector()+3,data.vector); break;
    case Scalar::Kind::Vector4:
    case Scalar::Kind::Quaternion: data.kind = ValueKind::Vector4; std::copy(value.asVector(),value.asVector()+4,data.vector); break;
    case Scalar::Kind::Object:
    {
        data.kind = ValueKind::Int;
        const std::uint64_t bits = value.asObject();
        std::memcpy(&data.integer,&bits,sizeof(bits));
        break;
    }
    default: throw std::invalid_argument("unsupported boundary value");
    }
    const auto result = NeoCoreBackend::MakeValue(runtime,data,lifetime);
    if (!result) throw std::runtime_error("could not allocate boundary value");
    return result;
}

Scalar BoundaryAccess::Wrap(NeoScript::IRuntime& runtime, const TypeRegistry& types, TypeId type,
    const std::shared_ptr<void>& lifetime, const NeoCoreBackend::ValuePtr& value)
{
    using NeoCoreBackend::ValueKind;
    NeoCoreBackend::ValueData data;
    if (!value || !NeoCoreBackend::ReadValue(value,data)) throw std::runtime_error("could not read boundary value");
    if (type == types.Void()) return Scalar();
    if (data.kind == ValueKind::Null)
    {
        if (!types.IsNullable(type)) throw std::runtime_error("nonnullable script result is null");
        return Scalar::Null();
    }
    type = Underlying(types,type);
    const TypeDesc& desc = types.Get(type);
    Scalar result;
    switch (desc.kind)
    {
    case TypeKind::Int: if(data.kind==ValueKind::Int) return Scalar::Int(data.integer); break;
    case TypeKind::Float: if(data.kind==ValueKind::Float) return Scalar::Float(data.number); break;
    case TypeKind::Bool: if(data.kind==ValueKind::Bool) return Scalar::Bool(data.integer!=0); break;
    case TypeKind::String: if(data.kind==ValueKind::String) return Scalar::String(data.text); break;
    case TypeKind::Vector2: if(data.kind==ValueKind::Vector2) return Scalar::Vector(Scalar::Kind::Vector2,data.vector); break;
    case TypeKind::Vector3: if(data.kind==ValueKind::Vector3) return Scalar::Vector(Scalar::Kind::Vector3,data.vector); break;
    case TypeKind::Vector4: if(data.kind==ValueKind::Vector4) return Scalar::Vector(Scalar::Kind::Vector4,data.vector); break;
    case TypeKind::Quaternion: if(data.kind==ValueKind::Vector4) return Scalar::Vector(Scalar::Kind::Quaternion,data.vector); break;
    case TypeKind::Array:
    case TypeKind::Array32: if(data.kind==ValueKind::Array) result.kind_=Scalar::Kind::Array; break;
    case TypeKind::List: if(data.kind==ValueKind::List) result.kind_=Scalar::Kind::List; break;
    case TypeKind::Map: if(data.kind==ValueKind::Map) result.kind_=Scalar::Kind::Map; break;
    case TypeKind::Set: if(data.kind==ValueKind::Set) result.kind_=Scalar::Kind::Set; break;
    case TypeKind::Function: if(data.kind==ValueKind::Function) result.kind_=Scalar::Kind::Function; break;
    case TypeKind::Struct:
        if(data.kind==ValueKind::List && NeoCoreBackend::ValueSize(value)==desc.fields.size()) result.kind_=Scalar::Kind::Struct;
        break;
    case TypeKind::Native:
        if(data.kind==ValueKind::Int)
        {
            std::uint64_t id;
            std::memcpy(&id,&data.integer,sizeof(id));
            return Scalar::Object(desc.name,id);
        }
        break;
    default: break;
    }
    if (result.kind_==Scalar::Kind::Void) throw std::runtime_error("backend value disagrees with " + types.DisplayName(type));
    result.bound_.reset(new Scalar::BoundValue());
    result.bound_->runtime=&runtime;
    result.bound_->types=&types;
    result.bound_->type=type;
    result.bound_->lifetime=lifetime;
    result.bound_->value=value;
    return result;
}

std::size_t Scalar::Count() const
{
    if (!bound_ || kind_ == Kind::Function) throw std::logic_error("value is not a collection or struct");
    return NeoCoreBackend::ValueSize(bound_->value);
}
Scalar Scalar::At(std::size_t index) const
{
    if (!bound_ || index>=Count()) throw std::out_of_range("collection index is out of range");
    const TypeDesc& desc = bound_->types->Get(bound_->type);
    TypeId type = desc.kind==TypeKind::Struct ? desc.fields[index].type : desc.kind==TypeKind::Map ? desc.value : desc.element;
    Scalar result=BoundaryAccess::Wrap(*bound_->runtime,*bound_->types,type,bound_->lifetime,NeoCoreBackend::ValueAt(bound_->value,index));
    if (result.kind_==Kind::Struct)
        return BoundaryAccess::Wrap(*bound_->runtime,*bound_->types,type,bound_->lifetime,BoundaryAccess::Copy(*bound_->runtime,*bound_->types,type,bound_->lifetime,result));
    return result;
}
Scalar Scalar::KeyAt(std::size_t index) const
{
    if (kind_!=Kind::Map || index>=Count()) throw std::out_of_range("map key index is out of range");
    const TypeId type=bound_->types->Get(bound_->type).key;
    return BoundaryAccess::Wrap(*bound_->runtime,*bound_->types,type,bound_->lifetime,NeoCoreBackend::ValueKeyAt(bound_->value,index));
}
Scalar Scalar::Get(const Scalar& key) const
{
    if(kind_!=Kind::Map) throw std::logic_error("value is not a map");
    const TypeDesc& desc=bound_->types->Get(bound_->type);
    const auto storedKey=BoundaryAccess::Copy(*bound_->runtime,*bound_->types,desc.key,bound_->lifetime,key);
    const auto result=NeoCoreBackend::ValueFind(bound_->value,storedKey);
    if(!result) throw std::out_of_range("map key is not present");
    return BoundaryAccess::Wrap(*bound_->runtime,*bound_->types,desc.value,bound_->lifetime,result).Clone();
}
bool Scalar::Contains(const Scalar& key) const
{
    if(kind_!=Kind::Map && kind_!=Kind::Set) throw std::logic_error("value is not a map or set");
    const TypeDesc& desc=bound_->types->Get(bound_->type);
    return NeoCoreBackend::ValueContains(bound_->value,BoundaryAccess::Copy(*bound_->runtime,*bound_->types,
        kind_==Kind::Map?desc.key:desc.element,bound_->lifetime,key));
}
void Scalar::Set(std::size_t index, const Scalar& value) const
{
    if(!bound_ || (kind_!=Kind::List && kind_!=Kind::Array && kind_!=Kind::Struct) || index>=Count())
        throw std::out_of_range("indexed assignment is out of range");
    const TypeDesc& desc=bound_->types->Get(bound_->type);
    TypeId type=kind_==Kind::Struct?desc.fields[index].type:desc.element;
    if(!NeoCoreBackend::ValueSet(bound_->value,index,BoundaryAccess::Copy(*bound_->runtime,*bound_->types,type,bound_->lifetime,value)))
        throw std::runtime_error("could not store collection element");
}
void Scalar::Put(const Scalar& key, const Scalar& value) const
{
    if(kind_!=Kind::Map) throw std::logic_error("value is not a map");
    const TypeDesc& desc=bound_->types->Get(bound_->type);
    const auto storedKey=BoundaryAccess::Copy(*bound_->runtime,*bound_->types,desc.key,bound_->lifetime,key);
    const auto storedValue=BoundaryAccess::Copy(*bound_->runtime,*bound_->types,desc.value,bound_->lifetime,value);
    if(!NeoCoreBackend::ValuePut(bound_->value,storedKey,storedValue)) throw std::runtime_error("could not store map entry");
}
void Scalar::Append(const Scalar& value) const
{
    if(kind_!=Kind::List && kind_!=Kind::Array && kind_!=Kind::Set) throw std::logic_error("value is not appendable");
    const TypeId element=bound_->types->Get(bound_->type).element;
    if(!NeoCoreBackend::ValueAppend(bound_->value,BoundaryAccess::Copy(*bound_->runtime,*bound_->types,element,bound_->lifetime,value)))
        throw std::runtime_error("could not append collection element");
}
bool Scalar::Remove(const Scalar& key) const
{
    if(kind_!=Kind::Map && kind_!=Kind::Set) throw std::logic_error("value is not a map or set");
    const TypeDesc& desc=bound_->types->Get(bound_->type);
    return NeoCoreBackend::ValueRemove(bound_->value,BoundaryAccess::Copy(*bound_->runtime,*bound_->types,
        kind_==Kind::Map?desc.key:desc.element,bound_->lifetime,key));
}
void Scalar::Resize(std::size_t count) const
{
    if(kind_!=Kind::List && kind_!=Kind::Array) throw std::logic_error("value is not resizable");
    if(count>static_cast<std::size_t>(std::numeric_limits<int>::max())) throw std::out_of_range("collection size exceeds representable limit");
    const std::size_t previous=Count();
    if(count<=previous) { if(!NeoCoreBackend::ValueResize(bound_->value,count)) throw std::runtime_error("could not resize collection"); return; }
    const TypeId element=bound_->types->Get(bound_->type).element;
    const Scalar initial=DefaultBoundaryValue(*bound_->runtime,*bound_->types,element,bound_->lifetime);
    if(!NeoCoreBackend::ValueReserve(bound_->value,count)) throw std::runtime_error("could not reserve collection storage");
    for(std::size_t i=previous;i<count;++i) Append(initial);
}
void Scalar::Reserve(std::size_t count) const
{
    if(!bound_ || kind_==Kind::Struct || kind_==Kind::Function) throw std::logic_error("value is not a collection");
    if(count>static_cast<std::size_t>(std::numeric_limits<int>::max())) throw std::out_of_range("collection capacity exceeds representable limit");
    if(!NeoCoreBackend::ValueReserve(bound_->value,count)) throw std::runtime_error("could not reserve collection storage");
}
void Scalar::RemoveAt(std::size_t index) const
{
    if(kind_!=Kind::List || index>=Count()) throw std::out_of_range("list index is out of range");
    if(!NeoCoreBackend::ValueRemoveAt(bound_->value,index)) throw std::runtime_error("could not remove collection element");
}
void Scalar::Clear() const
{
    if(!bound_ || kind_==Kind::Struct || kind_==Kind::Function || !NeoCoreBackend::ValueClear(bound_->value))
        throw std::logic_error("value cannot be cleared");
}

#define NEOCORE_ARRAY_COPY(cppType, storageKind) \
void Scalar::ReadArray(cppType* output, std::size_t count, std::size_t offset) const \
{ \
    if(kind_!=Kind::Array || !NeoCoreBackend::ReadArrayBuffer(bound_->value,NeoCoreBackend::ArrayStorage::storageKind,offset,output,count)) \
        throw std::invalid_argument("array copy requires matching storage, a valid buffer and an in-range slice"); \
} \
void Scalar::WriteArray(const cppType* input, std::size_t count, std::size_t offset) const \
{ \
    if(kind_!=Kind::Array || !NeoCoreBackend::WriteArrayBuffer(bound_->value,NeoCoreBackend::ArrayStorage::storageKind,offset,input,count)) \
        throw std::invalid_argument("array copy requires matching storage, a valid buffer and an in-range slice"); \
}
NEOCORE_ARRAY_COPY(std::int64_t,Int64)
NEOCORE_ARRAY_COPY(double,Float64)
NEOCORE_ARRAY_COPY(std::int32_t,Int32)
NEOCORE_ARRAY_COPY(float,Float32)
#undef NEOCORE_ARRAY_COPY
bool Scalar::SameStorage(const Scalar& other) const
{
    return bound_ && other.bound_ && NeoCoreBackend::ValueSameStorage(bound_->value,other.bound_->value);
}
Scalar Scalar::Clone() const
{
    if(kind_!=Kind::Struct) return *this;
    return BoundaryAccess::Wrap(*bound_->runtime,*bound_->types,bound_->type,bound_->lifetime,
        BoundaryAccess::Copy(*bound_->runtime,*bound_->types,bound_->type,bound_->lifetime,*this));
}

Scalar BoundaryAccess::Capture(Instance& owner, const NeoScript::CallContext& context, std::size_t index, TypeId type)
{
    return Wrap(*owner.impl_->runtime,owner.impl_->checked.types,type,owner.impl_->module,
        NeoCoreBackend::CaptureArgument(context,index,owner.impl_->module)).Clone();
}
bool BoundaryAccess::Return(Instance& owner, NeoScript::CallContext& context, TypeId type,
    const Scalar& value, std::string& error)
{
    try
    {
        if(NeoCoreBackend::ReturnValue(context,Copy(*owner.impl_->runtime,owner.impl_->checked.types,type,owner.impl_->module,value))) return true;
        error="could not return a value to the active script instance";
        return false;
    }
    catch(const std::exception& failure) { error=failure.what(); return false; }
}

static NeoCoreBackend::ValuePtr ConvertJsonValue(NeoScript::IRuntime& runtime, const TypeRegistry& types,
    TypeId type, const std::shared_ptr<void>& lifetime, const NeoCoreBackend::ValuePtr& value, bool decoding, unsigned depth)
{
    using namespace NeoCoreBackend;
    if(depth>128) throw std::invalid_argument("JSON value nesting exceeds 128 levels or contains a cycle");
    ValueData data;
    if(!ReadValue(value,data)) throw std::invalid_argument("invalid JSON boundary value");
    if(data.kind==ValueKind::Null)
    {
        if(!types.IsNullable(type) && types.Get(type).kind != TypeKind::Null)
            throw std::invalid_argument("null requires a nullable JSON target");
        return value;
    }
    type=Underlying(types,type);
    const TypeDesc& desc=types.Get(type);
    switch(desc.kind)
    {
    case TypeKind::Int: if(data.kind==ValueKind::Int) return value; break;
    case TypeKind::Float:
        if(data.kind==ValueKind::Int) { data.kind=ValueKind::Float; data.number=static_cast<double>(data.integer); }
        if(data.kind==ValueKind::Float && std::isfinite(data.number)) return MakeValue(runtime,data,lifetime);
        break;
    case TypeKind::Bool: if(data.kind==ValueKind::Bool) return value; break;
    case TypeKind::String: if(data.kind==ValueKind::String) return value; break;
    case TypeKind::Vector2:
    case TypeKind::Vector3:
    case TypeKind::Vector4:
    case TypeKind::Quaternion:
    {
        const std::size_t count=desc.kind==TypeKind::Vector2?2:desc.kind==TypeKind::Vector3?3:4;
        if(decoding)
        {
            if(data.kind!=ValueKind::List || ValueSize(value)!=count) break;
            ValueData vector;
            vector.kind=count==2?ValueKind::Vector2:count==3?ValueKind::Vector3:ValueKind::Vector4;
            for(std::size_t i=0;i<count;++i)
            {
                ValueData component;
                if(!ReadValue(ValueAt(value,i),component) || (component.kind!=ValueKind::Int && component.kind!=ValueKind::Float))
                    throw std::invalid_argument("JSON vector components must be numbers");
                const double number=component.kind==ValueKind::Int?static_cast<double>(component.integer):component.number;
                vector.vector[i]=NeoCoreBackend::NarrowFloat32(number);
                if(!std::isfinite(vector.vector[i])) throw std::invalid_argument("JSON vector component exceeds finite float32 range");
            }
            return MakeValue(runtime,vector,lifetime);
        }
        const auto result=MakeCollection(runtime,ValueKind::List,ArrayStorage::Int64,lifetime);
        for(std::size_t i=0;i<count;++i)
        {
            if(!std::isfinite(data.vector[i])) throw std::invalid_argument("JSON cannot encode nonfinite vector components");
            ValueData component; component.kind=ValueKind::Float; component.number=data.vector[i];
            if(!ValueAppend(result,MakeValue(runtime,component,lifetime))) throw std::runtime_error("could not encode JSON vector");
        }
        return result;
    }
    case TypeKind::Array:
    case TypeKind::Array32:
    case TypeKind::List:
    case TypeKind::Set:
    {
        if(decoding && data.kind!=ValueKind::List) break;
        const auto result=decoding?NewCollection(runtime,types,type,lifetime):MakeCollection(runtime,ValueKind::List,ArrayStorage::Int64,lifetime);
        const std::size_t count=ValueSize(value);
        if(!ValueReserve(result,count)) throw std::runtime_error("could not reserve JSON collection");
        for(std::size_t i=0;i<count;++i)
        {
            const auto element=ConvertJsonValue(runtime,types,desc.element,lifetime,ValueAt(value,i),decoding,depth+1);
            if(decoding && desc.kind==TypeKind::Array32 && desc.element==types.Float())
            {
                ValueData number;
                if(!ReadValue(element,number) || !std::isfinite(NeoCoreBackend::NarrowFloat32(number.number)))
                    throw std::invalid_argument("JSON compact array element exceeds finite float32 range");
            }
            if(!ValueAppend(result,element))
                throw std::runtime_error("could not store JSON collection element");
        }
        return result;
    }
    case TypeKind::Map:
    {
        if(desc.key!=types.String()) throw std::invalid_argument("JSON objects require map<string, T>");
        if(data.kind!=ValueKind::Map) break;
        const auto result=MakeCollection(runtime,ValueKind::Map,ArrayStorage::Int64,lifetime);
        std::vector<std::pair<ValuePtr,ValuePtr>> entries;
        if(!SnapshotMap(value,entries)) throw std::runtime_error("could not enumerate JSON object");
        for(const auto& entry:entries)
            if(!ValuePut(result,entry.first,ConvertJsonValue(runtime,types,desc.value,lifetime,entry.second,decoding,depth+1)))
                throw std::runtime_error("could not store JSON object field");
        return result;
    }
    case TypeKind::Struct:
    {
        if(decoding && (data.kind!=ValueKind::Map || ValueSize(value)!=desc.fields.size()))
            throw std::invalid_argument("JSON struct fields must exactly match its declaration");
        const auto result=decoding?NewCollection(runtime,types,type,lifetime):MakeCollection(runtime,ValueKind::Map,ArrayStorage::Int64,lifetime);
        for(std::size_t i=0;i<desc.fields.size();++i)
        {
            ValueData name; name.kind=ValueKind::String; name.text=desc.fields[i].name;
            const auto key=MakeValue(runtime,name,lifetime);
            const auto field=decoding?ValueFind(value,key):ValueAt(value,i);
            if(!field) throw std::invalid_argument("JSON struct field is missing: "+name.text);
            const auto converted=ConvertJsonValue(runtime,types,desc.fields[i].type,lifetime,field,decoding,depth+1);
            if(!(decoding?ValueAppend(result,converted):ValuePut(result,key,converted))) throw std::runtime_error("could not store JSON struct field");
        }
        return result;
    }
    default: throw std::invalid_argument("JSON does not support this declared type: "+types.DisplayName(type));
    }
    throw std::invalid_argument("JSON value requires "+types.DisplayName(type));
}

// JSON은 호출자의 타입 표와 할당 영역에서 변환한다. 기본 인스턴스로 우회하지 않는다.
static NeoCoreBackend::ValuePtr DecodeTypedJson(NeoScript::IRuntime& runtime, NeoScript::InstanceHandle instance,
    const TypeRegistry& types, TypeId type, const std::shared_ptr<void>& lifetime,
    const std::string& text, bool strictIntegers, std::string& error)
{
    if (!types.IsJsonType(type))
        throw std::invalid_argument("JSON does not support this declared type: " + types.DisplayName(type));
    std::vector<std::string> warnings;
    const auto raw = NeoCoreBackend::DecodeJson(runtime, instance, text, strictIntegers, false, lifetime, error, warnings);
    if (!raw) return NeoCoreBackend::ValuePtr();
    return ConvertJsonValue(runtime, types, type, lifetime, raw, true, 0);
}

Scalar BoundaryAccess::ParseJson(Instance& owner, TypeId type, const std::string& text,
    bool strictIntegers, std::string& error)
{
    error.clear();
    try
    {
        const auto value = DecodeTypedJson(*owner.impl_->runtime, owner.impl_->instance,
            owner.impl_->checked.types, type, owner.impl_->module, text, strictIntegers, error);
        if (!value) return Scalar();
        return Wrap(*owner.impl_->runtime, owner.impl_->checked.types, type, owner.impl_->module, value);
    }
    catch (const std::exception& failure) { error = failure.what(); }
    return Scalar();
}

bool BoundaryAccess::EncodeJson(Instance& owner, const NeoScript::CallContext& context, std::size_t index,
    TypeId type, bool sortKeys, std::string& text, std::string& error)
{
    text.clear(); error.clear();
    try
    {
        const auto& types = owner.impl_->checked.types;
        if (!types.IsJsonType(type))
            throw std::invalid_argument("JSON does not support this declared type: " + types.DisplayName(type));
        const auto raw = NeoCoreBackend::CaptureArgument(context, index, owner.impl_->module);
        const auto normalized = ConvertJsonValue(*owner.impl_->runtime, types, type, owner.impl_->module, raw, false, 0);
        std::vector<std::string> warnings;
        return NeoCoreBackend::EncodeJson(normalized, sortKeys, text, error, warnings);
    }
    catch (const std::exception& failure) { error = failure.what(); return false; }
}

bool BoundaryAccess::DecodeJson(Instance& owner, NeoScript::CallContext& context, TypeId type,
    const std::string& text, std::string& error)
{
    error.clear();
    try
    {
        const auto normalized = DecodeTypedJson(*owner.impl_->runtime, owner.impl_->instance,
            owner.impl_->checked.types, type, owner.impl_->module, text, true, error);
        if (!normalized) return false;
        if (NeoCoreBackend::ReturnValue(context, normalized)) return true;
        error = "could not return JSON value to the active script instance";
    }
    catch (const std::exception& failure) { error = failure.what(); }
    return false;
}
