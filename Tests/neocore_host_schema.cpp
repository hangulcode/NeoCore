#include "NeoCoreHostSchema.h"

#include <cstdio>
#include <limits>
#include <string>
#include <vector>

using namespace NeoCore;

static int checks = 0;
static int failures = 0;

static void Check(bool value, const char* label)
{
    ++checks;
    if (!value)
    {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", label);
    }
}

static void CheckInvalidTypes()
{
    HostSchema schema;
    TypeRegistry& types = schema.Types();
    const TypeId object = schema.AddObject("Entity");
    const TypeId foreign = types.DeclareNative("Unregistered");
    const TypeId structure = types.DeclareStruct("Record");
    std::vector<TypeId> rejected;
    rejected.push_back(InvalidType);
    rejected.push_back(std::numeric_limits<TypeId>::max());
    rejected.push_back(types.Null());
    rejected.push_back(structure);
    rejected.push_back(foreign);
    rejected.push_back(types.ListOf(foreign));
    rejected.push_back(types.MapOf(types.String(), foreign));
    rejected.push_back(types.FunctionOf(foreign, std::vector<TypeId>()));
    rejected.push_back(types.FunctionOf(types.Void(), std::vector<TypeId>(1, foreign)));
    rejected.push_back(types.ListOf(structure));
    rejected.push_back(types.DeclareBuiltinObject("OpaqueHostObject"));
    for (TypeId type : rejected)
    {
        Check(schema.AddMethod(object, "Result", type, std::vector<TypeId>()) == InvalidHostBinding,
            "unsupported method result is rejected");
        Check(!schema.LastError().empty(), "rejected result explains failure");
        Check(schema.AddMethod(object, "Argument", types.Void(), std::vector<TypeId>(1, type)) == InvalidHostBinding,
            "unsupported method argument is rejected");
        Check(!schema.AddProperty(object, "Value", type), "unsupported property type is rejected");
        Check(!schema.AddGlobal("Global", type), "unsupported global type is rejected");
        Check(schema.Bindings().empty() && schema.Globals().empty() && schema.FindProperty(object, "Value") == nullptr,
            "failed type registration leaves no partial members or binding ids");
    }
    Check(schema.AddMethod(object, "VoidArgument", types.Int(), std::vector<TypeId>(1, types.Void())) == InvalidHostBinding,
        "void parameter is rejected");
    Check(!schema.AddProperty(object, "VoidValue", types.Void()), "void property is rejected");
    Check(schema.AddMethod(foreign, "Call", types.Void(), std::vector<TypeId>()) == InvalidHostBinding,
        "unregistered native cannot own a method");
    Check(!schema.AddProperty(types.Int(), "Value", types.Int()), "scalar cannot own a property");
    Check(types.NullableOf(object) == object && types.AllowsNull(object),
        "host objects hold null without a separate type");
    Check(schema.AddMethod(object, "Success", types.Void(), std::vector<TypeId>()) == 1,
        "failed additions do not consume binding ids");
    Check(schema.LastError().empty(), "successful addition clears the previous diagnostic");
    std::string error;
    Check(schema.Validate(error) && error.empty(), "registry descriptors unused by the schema are harmless");
}

static void CheckNames()
{
    const char* const invalidNames[] = {
        "", "two words", "Game.Object", "0object", "bad-name", "var", "fun", "int", "float",
        "bool", "string", "void", "int32", "float32", "any", "null", "true", "false", "return",
        "import", "public", "export", "const", "struct", "if", "while", "print", "tostring",
        "toint", "tofloat", "list", "map", "array", "array32", "set", "math", "system",
        "Vector2", "Vector3", "Vector4", "Quaternion"
    };
    HostSchema schema;
    const TypeId owner = schema.AddObject("Object1");
    for (const char* name : invalidNames)
    {
        Check(schema.AddObject(name) == InvalidType, "invalid or reserved object identifier is rejected");
        Check(schema.AddMethod(owner, name, schema.Types().Void(), std::vector<TypeId>()) == InvalidHostBinding,
            "invalid or reserved method identifier is rejected");
        Check(!schema.AddProperty(owner, name, schema.Types().Int()),
            "invalid or reserved property identifier is rejected");
        Check(!schema.AddGlobal(name, owner), "invalid or reserved global identifier is rejected");
    }
    Check(schema.AddObject("Object1") == InvalidType, "duplicate object type is rejected");
    Check(schema.AddObject("object1") != InvalidType, "object names are case-sensitive");
    Check(schema.AddObject("_Object2") != InvalidType, "identifier underscore is allowed");
    Check(schema.AddGlobal("Object1", owner), "a type and global may have the same name");
    Check(!schema.AddGlobal("Object1", owner), "duplicate global is rejected");
    Check(schema.AddGlobal("FutureType", owner), "global may precede a same-named type");
    Check(schema.AddObject("FutureType") != InvalidType, "type and global namespaces are independent in either order");
    const TypeId second = schema.AddObject("Second");
    Check(schema.AddMethod(owner, "Value", schema.Types().Int(), std::vector<TypeId>()) != InvalidHostBinding,
        "method declaration succeeds");
    Check(schema.AddMethod(owner, "Value", schema.Types().Float(), std::vector<TypeId>(1, schema.Types().Int())) == InvalidHostBinding,
        "same-name overload is explicitly rejected");
    Check(!schema.AddProperty(owner, "Value", schema.Types().Int()), "property cannot hide a method");
    Check(schema.AddProperty(second, "Value", schema.Types().Int()), "members belong to distinct object namespaces");
    Check(schema.AddMethod(second, "Value", schema.Types().Int(), std::vector<TypeId>()) == InvalidHostBinding,
        "method cannot hide a property");
    Check(!schema.AddProperty(second, "Value", schema.Types().Float()), "duplicate property is rejected");
    Check(schema.FindMethod(second, "Value") == nullptr && schema.FindProperty(owner, "Value") == nullptr,
        "lookup does not cross member kinds or owners");
    Check(schema.FindMethod(owner, "value") == nullptr, "member lookup is case-sensitive");
}

int main()
{
    HostSchema schema;
    std::string error = "old error";
    Check(schema.Validate(error) && error.empty(), "empty host schema is valid");
    Check(schema.FindBinding(InvalidHostBinding) == nullptr && schema.FindBinding(1) == nullptr,
        "binding zero and out-of-range ids are not addressable");
    Check(schema.FindGlobal("missing") == nullptr && schema.FindProperty(InvalidType, "missing") == nullptr &&
        schema.FindMethod(InvalidType, "missing") == nullptr, "missing lookups return null");
    const TypeId entity = schema.AddObject("Entity");
    const TypeId service = schema.AddObject("Service");
    TypeRegistry& types = schema.Types();
    Check(entity != InvalidType && service != InvalidType && entity != service,
        "host objects are distinct nominal types");
    const TypeId nullableEntity = types.NullableOf(entity);
    std::vector<TypeId> supported;
    supported.push_back(types.Int());
    supported.push_back(types.Float());
    supported.push_back(types.Bool());
    supported.push_back(types.String());
    supported.push_back(types.Vector2());
    supported.push_back(types.Vector3());
    supported.push_back(types.Vector4());
    supported.push_back(types.Quaternion());
    supported.push_back(entity);
    Check(nullableEntity == entity && types.NullableOf(types.Int()) == InvalidType && types.NullableOf(types.Vector3()) == InvalidType,
        "the compatibility nullable constructor keeps reference types and rejects value types");
    supported.push_back(types.ArrayOf(types.Int()));
    supported.push_back(types.Array32Of(types.Float()));
    supported.push_back(types.ListOf(types.String()));
    supported.push_back(types.MapOf(types.String(), types.Int()));
    supported.push_back(types.SetOf(types.Int()));
    supported.push_back(types.FunctionOf(types.Void(), std::vector<TypeId>()));
    const TypeId record = types.DeclareStruct("Record");
    std::vector<StructField> fields;
    fields.push_back(StructField("number", types.Int()));
    fields.push_back(StructField("owner", nullableEntity));
    Check(types.DefineStructFields(record, fields, &error), "complete host struct fields");
    supported.push_back(record);
    Check(types.NullableOf(record) == InvalidType, "host structs never hold null");
    supported.push_back(types.ListOf(record));
    supported.push_back(types.FunctionOf(record, std::vector<TypeId>(1, types.ListOf(record))));
    const HostBindingId first = schema.AddMethod(service, "Invoke", types.Void(), supported);
    Check(first == 1, "first method receives binding one");
    const HostBinding* method = schema.FindMethod(service, "Invoke");
    Check(method != nullptr && method->id == first && method->ownerType == service &&
        method->name == "Invoke" && method->kind == HostBindingKind::Method &&
        method->returnType == types.Void() && method->parameters == supported,
        "method contract preserves exact owner, argument order and result");
    Check(schema.AddProperty(entity, "Count", types.Int()), "read-write property succeeds");
    const HostProperty* property = schema.FindProperty(entity, "Count");
    Check(property != nullptr && property->getter == 2 && property->setter == 3,
        "property accessor ids share the method id space");
    const HostBinding* getter = schema.FindBinding(2);
    const HostBinding* setter = schema.FindBinding(3);
    Check(getter != nullptr && getter->kind == HostBindingKind::Getter && getter->parameters.empty() &&
        getter->returnType == types.Int() && getter->ownerType == entity && getter->name == "Count",
        "getter contract has no arguments and returns its property type");
    Check(setter != nullptr && setter->kind == HostBindingKind::Setter && setter->parameters.size() == 1 &&
        setter->parameters[0] == types.Int() && setter->returnType == types.Void() && setter->ownerType == entity,
        "setter contract takes its exact property type and returns void");
    Check(schema.AddProperty(entity, "Parent", nullableEntity, true, false), "read-only object property succeeds");
    Check(schema.AddProperty(entity, "Label", types.String(), false, true), "write-only string property succeeds");
    property = schema.FindProperty(entity, "Parent");
    Check(property != nullptr && property->getter == 4 && property->setter == InvalidHostBinding,
        "read-only property has no setter id");
    property = schema.FindProperty(entity, "Label");
    Check(property != nullptr && property->getter == InvalidHostBinding && property->setter == 5,
        "write-only property has no getter id");
    Check(!schema.AddProperty(entity, "Unavailable", types.Int(), false, false) &&
        schema.Bindings().size() == 5 && schema.FindProperty(entity, "Unavailable") == nullptr,
        "accessorless property fails atomically");
    for (std::size_t i = 0; i < supported.size(); ++i)
    {
        const std::string name = "Result" + std::to_string(i);
        Check(schema.AddMethod(service, name, supported[i], std::vector<TypeId>()) != InvalidHostBinding,
            "all supported nonvoid result types are accepted");
    }
    Check(schema.AddGlobal("Current", entity) && schema.AddGlobal("Optional", nullableEntity),
        "native globals are accepted");
    Check(schema.FindGlobal("Current")->type == entity && schema.FindGlobal("Optional")->type == nullableEntity,
        "global descriptors preserve the object type");
    Check(!schema.AddGlobal("Scalar", types.Int()) && !schema.AddGlobal("NullableScalar", types.NullableOf(types.Int())),
        "host globals in this slice must be native objects");
    for (std::size_t i = 0; i < schema.Bindings().size(); ++i)
        Check(schema.Bindings()[i].id == i + 1 && schema.FindBinding(static_cast<HostBindingId>(i + 1)) == &schema.Bindings()[i],
            "binding id maps directly to the descriptor vector");
    Check(schema.Validate(error) && error.empty() && schema.LastError().empty(), "complete schema validates");

    HostSchema snapshot = schema;
    const std::size_t snapshotBindings = snapshot.Bindings().size();
    const TypeId later = schema.AddObject("Later");
    schema.AddProperty(entity, "LaterValue", types.Float());
    schema.AddGlobal("LaterGlobal", later);
    Check(snapshot.Bindings().size() == snapshotBindings && snapshot.FindProperty(entity, "LaterValue") == nullptr &&
        snapshot.FindGlobal("LaterGlobal") == nullptr && snapshot.Types().LookupNamed("Later") == InvalidType,
        "schema copy is an independent immutable-program snapshot");
    Check(snapshot.Validate(error), "copied schema preserves all internal ids");
    snapshot.Types() = TypeRegistry();
    Check(!snapshot.Validate(error) && !error.empty() && snapshot.LastError() == error,
        "replacing the mutable registry cannot silently invalidate a compiled contract");
    Check(schema.Validate(error), "mutating a schema copy cannot corrupt the original");

    CheckInvalidTypes();
    CheckNames();
    std::printf("NeoCore host schema: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
