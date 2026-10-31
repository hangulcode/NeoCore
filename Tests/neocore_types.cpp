#include "NeoCoreTypes.h"

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

int main()
{
    TypeRegistry types;
    Check(types.LookupBuiltin("int") == types.Int(), "int is the exposed integer type");
    Check(types.LookupBuiltin("float") == types.Float(), "float is the exposed floating type");
    Check(types.LookupBuiltin("int32") == InvalidType, "int32 is not declarable");
    Check(types.LookupBuiltin("float32") == InvalidType, "float32 is not declarable");
    Check(types.LookupBuiltin("any") == InvalidType, "no implicit dynamic escape");
    Check(types.LookupBuiltin("null") == InvalidType, "null is a literal type only");
    Check(types.LookupBuiltin("Int") == InvalidType, "builtin names are case-sensitive");
    Check(types.Get(std::numeric_limits<TypeId>::max()).kind == TypeKind::Invalid,
        "invalid type ids are not numeric or storable");
    Check(!types.CanAssign(types.Int(), InvalidType) && !types.CanAssign(InvalidType, types.Int()),
        "unknown types cannot pass an assignment check");
    Check(!types.CanAssign(types.Void(), types.Void()), "void is not a value");
    Check(!types.CanAssign(types.Int(), types.Void()), "void expression cannot initialize a value");
    Check(types.CanAssign(types.Float(), types.Int()), "integer to float conversion is permitted");
    Check(!types.CanAssign(types.Int(), types.Float()), "float to integer conversion is explicit");
    Check(!types.CanAssign(types.Bool(), types.Int()), "truthiness is not a numeric conversion");
    Check(!types.CanAssign(types.String(), types.Int()), "string conversion is explicit");
    Check(types.CommonNumericType(types.Int(), types.Int()) == types.Int(), "integer arithmetic keeps int");
    Check(types.CommonNumericType(types.Int(), types.Float()) == types.Float(), "mixed arithmetic promotes int");
    Check(types.CommonNumericType(types.Float(), types.Int()) == types.Float(), "promotion is independent of operand order");
    Check(types.CommonNumericType(types.Bool(), types.Int()) == InvalidType, "bool is not arithmetic");

    const TypeId optionalInt = types.NullableOf(types.Int());
    const TypeId optionalFloat = types.NullableOf(types.Float());
    const TypeId optionalString = types.NullableOf(types.String());
    Check(optionalInt == types.NullableOf(types.Int()), "nullable type is interned");
    Check(types.Get(optionalInt).element == types.Int(), "nullable retains the underlying type");
    Check(types.NullableOf(types.Void()) == InvalidType, "void cannot be nullable");
    Check(types.NullableOf(types.Null()) == InvalidType, "bare null cannot be nullable");
    Check(types.NullableOf(optionalInt) == InvalidType, "nested nullable declaration is rejected");
    Check(types.CanAssign(optionalInt, types.Null()), "null may initialize a nullable value");
    Check(types.CanAssign(optionalString, types.String()), "nonnull reference may initialize nullable reference");
    Check(types.CanAssign(optionalFloat, types.Int()), "integer promotes before nullable wrapping");
    Check(!types.CanAssign(types.String(), types.Null()), "nonnull reference rejects null");
    Check(!types.CanAssign(types.Int(), optionalInt), "nullable value cannot implicitly unwrap");
    Check(!types.CanAssign(optionalFloat, optionalInt), "nullable numeric conversion requires explicit handling");
    Check(types.CommonNumericType(optionalInt, types.Int()) == InvalidType, "nullable arithmetic requires unwrapping");

    const TypeId arrayInt = types.ArrayOf(types.Int());
    const TypeId arrayFloat = types.ArrayOf(types.Float());
    const TypeId packedInt = types.Array32Of(types.Int());
    const TypeId packedFloat = types.Array32Of(types.Float());
    Check(arrayInt != packedInt && arrayFloat != packedFloat, "storage widths are distinct array types");
    Check(types.Array32Of(types.Bool()) == InvalidType, "array32 does not support bool");
    Check(types.Array32Of(types.Vector3()) == InvalidType, "array32 does not support vectors");
    Check(types.Array32Of(optionalInt) == InvalidType, "array32 does not support nullable elements");
    Check(types.ArrayOf(types.Void()) == InvalidType, "void cannot be stored in an array");
    Check(!types.CanAssign(arrayFloat, arrayInt), "mutable arrays are invariant");
    Check(!types.CanAssign(arrayInt, packedInt) && !types.CanAssign(packedInt, arrayInt),
        "array width conversion is not implicit");
    Check(types.Get(packedInt).element == types.Int() && types.Get(packedFloat).element == types.Float(),
        "array32 elements are read as the standard scalar types");
    Check(!types.CanAssign(types.ListOf(types.Float()), types.ListOf(types.Int())), "mutable lists are invariant");
    Check(!types.CanAssign(types.SetOf(types.Float()), types.SetOf(types.Int())), "sets are invariant");
    const TypeId mapInt = types.MapOf(types.String(), types.Int());
    Check(mapInt == types.MapOf(types.String(), types.Int()), "map types are interned");
    Check(mapInt != types.MapOf(types.Int(), types.String()), "map key and value order matters");
    Check(!types.CanAssign(types.MapOf(types.String(), types.Float()), mapInt), "map values are invariant");
    Check(types.MapOf(types.Void(), types.Int()) == InvalidType, "map keys must be storable");
    Check(types.MapOf(types.Int(), types.Void()) == InvalidType, "map values must be storable");
    Check(types.Get(types.NullableOf(arrayInt)).element == arrayInt, "nullable array differs from nullable elements");
    Check(types.NullableOf(arrayInt) != types.ArrayOf(optionalInt), "nullable array and nullable element identities differ");

    Check(!types.CanAssign(types.Vector3(), types.Vector2()), "vector dimensions are distinct");
    Check(!types.CanAssign(types.Vector4(), types.Quaternion()), "quaternion is not a Vector4 alias");
    Check(!types.CanAssign(types.Quaternion(), types.Vector4()), "Vector4 is not a quaternion alias");
    Check(!types.IsNumeric(types.Vector2()), "vectors are not scalar numbers");

    const TypeId unaryInt = types.FunctionOf(types.Int(), std::vector<TypeId>(1, types.Int()));
    const TypeId unaryFloat = types.FunctionOf(types.Float(), std::vector<TypeId>(1, types.Int()));
    Check(unaryInt == types.FunctionOf(types.Int(), std::vector<TypeId>(1, types.Int())), "function signatures are interned");
    Check(!types.CanAssign(unaryFloat, unaryInt), "function return conversion does not change function identity");
    Check(types.FunctionOf(types.Int(), std::vector<TypeId>(1, types.Void())) == InvalidType, "void parameter is rejected");
    Check(types.FunctionOf(types.Null(), std::vector<TypeId>()) == InvalidType, "bare null is not a return declaration");
    Check(types.FunctionOf(types.Void(), std::vector<TypeId>()) != InvalidType, "void-returning function is a valid function type");

    const TypeId position = types.DeclareStruct("geometry.Position");
    const TypeId velocity = types.DeclareStruct("geometry.Velocity");
    const TypeId object = types.DeclareNative("host.GameObject");
    Check(position == types.DeclareStruct("geometry.Position"), "nominal declarations keep stable identity");
    Check(!types.CanAssign(position, velocity), "different named structures are not interchangeable");
    Check(!types.CanAssign(position, object), "native and structure types are distinct");
    Check(types.DeclareNative("geometry.Position") == InvalidType, "conflicting nominal declaration is rejected");
    Check(types.DeclareStruct("int") == InvalidType, "nominal type cannot replace a builtin");
    Check(types.DeclareStruct("int32") == InvalidType && types.DeclareNative("float32") == InvalidType,
        "nominal type cannot introduce a forbidden scalar name");
    Check(types.DeclareStruct("any") == InvalidType, "nominal type cannot disguise a dynamic escape");
    Check(types.LookupNamed("geometry.Position") == position && types.LookupBuiltin("geometry.Position") == InvalidType,
        "nominal and builtin lookup remain distinct");
    Check(types.DisplayName(packedFloat) == "array32<float>", "array32 diagnostics use the exposed scalar name");
    Check(types.DisplayName(types.NullableOf(object)) == "host.GameObject?", "nullable nominal diagnostics");
    Check(types.DisplayName(unaryInt) == "int function(int)", "function signature diagnostics");

    Check(types.LookupNamed("int&") == InvalidType && types.LookupNamed("geometry.Position&") == InvalidType,
        "reference spellings are not registered types");
    const TypeId valueParameter = types.FunctionOf(types.Void(), std::vector<TypeId>(1, position));
    Check(valueParameter != InvalidType && types.Get(valueParameter).parameters[0] == position,
        "function parameters retain nominal value types");

    std::vector<StructField> coordinates;
    coordinates.push_back(StructField("x", types.Float()));
    coordinates.push_back(StructField("y", types.Float()));
    Check(!types.Get(position).fieldsDefined && types.Get(position).fields.empty(),
        "a nominal declaration starts with unresolved fields");
    std::size_t fieldIndex = 12;
    Check(types.FindStructField(position, "x", &fieldIndex) == nullptr && fieldIndex == InvalidFieldIndex,
        "an incomplete struct has no exposed field metadata");
    std::string fieldError = "previous error";
    Check(types.DefineStructFields(position, coordinates, &fieldError) && fieldError.empty(),
        "field definition succeeds and clears stale diagnostics");
    Check(types.Get(position).fieldsDefined && types.Get(position).fields.size() == 2 &&
        types.Get(position).fields[0].name == "x" && types.Get(position).fields[1].name == "y",
        "field declaration order defines stable indices");
    const StructField* savedField = types.FindStructField(position, "y", &fieldIndex);
    Check(savedField && savedField->type == types.Float() && fieldIndex == 1,
        "field lookup returns its exact declared type and index");
    Check(!types.DefineStructFields(position, coordinates, &fieldError) && !fieldError.empty() &&
        savedField == types.FindStructField(position, "y"), "successful definitions cannot be replaced");
    Check(types.DefineStructFields(velocity, coordinates) && !types.CanAssign(position, velocity),
        "identical field layouts remain nominally distinct");
    Check(types.FindStructField(position, "missing", &fieldIndex) == nullptr && fieldIndex == InvalidFieldIndex,
        "missing field lookup invalidates an old index");
    Check(types.FindStructField(types.NullableOf(position), "x") == nullptr,
        "nullable receivers must be resolved explicitly by the checker");
    Check(!types.DefineStructFields(types.Int(), coordinates) && !types.DefineStructFields(object, coordinates) &&
        !types.DefineStructFields(InvalidType, coordinates), "only nominal structs accept field definitions");

    const TypeId invalidFields = types.DeclareStruct("InvalidFields");
    std::vector<StructField> repeated = coordinates;
    repeated.push_back(StructField("x", types.Int()));
    Check(!types.DefineStructFields(invalidFields, repeated, &fieldError) && !fieldError.empty(),
        "duplicate field names are rejected");
    Check(!types.Get(invalidFields).fieldsDefined && types.Get(invalidFields).fields.empty(),
        "failed validation leaves the declaration unresolved and empty");
    Check(!types.DefineStructFields(invalidFields, std::vector<StructField>(1, StructField("", types.Int()))),
        "empty field names are rejected");
    Check(!types.DefineStructFields(invalidFields, std::vector<StructField>(1, StructField("x", InvalidType))),
        "invalid field types are rejected");
    Check(!types.DefineStructFields(invalidFields, std::vector<StructField>(1, StructField("x", types.Void()))),
        "void cannot be a field type");
    Check(!types.DefineStructFields(invalidFields, std::vector<StructField>(1, StructField("x", types.Null()))),
        "bare null cannot be a field type");
    Check(types.DefineStructFields(invalidFields, coordinates), "a rejected definition can be corrected");
    const TypeId emptyStruct = types.DeclareStruct("EmptyStruct");
    Check(types.DefineStructFields(emptyStruct, std::vector<StructField>()) && types.Get(emptyStruct).fieldsDefined,
        "empty structures are distinct from incomplete declarations");
    Check(!types.DefineStructFields(emptyStruct, coordinates), "empty structures are also defined only once");

    const TypeId nestedStruct = types.DeclareStruct("NestedStruct");
    std::vector<StructField> nestedFields;
    nestedFields.push_back(StructField("position", position));
    nestedFields.push_back(StructField("weight", optionalFloat));
    nestedFields.push_back(StructField("name", types.String()));
    Check(types.DefineStructFields(nestedStruct, nestedFields), "nested values and nullable scalar fields are valid");
    Check(types.FindStructField(nestedStruct, "position")->type == position,
        "a struct field retains its nominal value identity");
    const TypeId self = types.DeclareStruct("Self");
    Check(!types.DefineStructFields(self, std::vector<StructField>(1, StructField("next", self))),
        "direct recursive values are rejected");
    Check(!types.DefineStructFields(self, std::vector<StructField>(1, StructField("next", types.NullableOf(self)))),
        "nullable wrapping does not permit a recursive value layout");

    const TypeId first = types.DeclareStruct("First");
    const TypeId second = types.DeclareStruct("Second");
    Check(types.DefineStructFields(first, std::vector<StructField>(1, StructField("next", second))),
        "fields may refer to a forward-declared nominal type");
    Check(!types.DefineStructFields(second, std::vector<StructField>(1, StructField("previous", first))),
        "the definition that closes a mutual value cycle is rejected");
    Check(!types.Get(second).fieldsDefined && types.Get(first).fieldsDefined,
        "cycle rejection preserves earlier valid definitions");
    Check(types.DefineStructFields(second, coordinates), "an acyclic replacement completes forward field declarations");

    std::vector<TypeId> chain;
    for (int i = 0; i < 512; ++i) chain.push_back(types.DeclareStruct("Chain" + std::to_string(i)));
    bool chainDefined = true;
    for (std::size_t i = 0; i + 1 < chain.size(); ++i)
        chainDefined = types.DefineStructFields(chain[i], std::vector<StructField>(1, StructField("next", chain[i + 1]))) && chainDefined;
    Check(chainDefined && !types.DefineStructFields(chain.back(),
        std::vector<StructField>(1, StructField("next", types.NullableOf(chain.front())))),
        "deep indirect nullable cycles are rejected without recursive traversal");
    Check(types.DefineStructFields(chain.back(), coordinates), "deep forward value chains can be completed without a cycle");
    const TypeId tree = types.DeclareStruct("Tree");
    Check(types.DefineStructFields(tree, std::vector<StructField>(1, StructField("children", types.ListOf(tree)))),
        "dynamic container handles do not form an inline value-layout cycle");

    const TypeDesc* saved = &types.Get(mapInt);
    TypeId nested = types.Int();
    for (int i = 0; i < 1024; ++i) nested = types.ListOf(nested);
    Check(saved == &types.Get(mapInt) && saved->key == types.String() && saved->value == types.Int(),
        "AST-held type descriptors survive registry growth");
    Check(savedField == types.FindStructField(position, "y") && savedField->name == "y" && savedField->type == types.Float(),
        "field metadata survives registry growth and unrelated definitions");

    std::printf("neocore_types: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
