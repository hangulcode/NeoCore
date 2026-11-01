#include "NeoCore.h"
#include "NeoCoreHost.h"
#include "NeoCoreImage.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
int checks = 0;

void Require(bool condition, const std::string& label)
{
    ++checks;
    if (!condition) throw std::runtime_error(label);
}

void Int(const NeoCore::Result& result, std::int64_t expected, const char* label)
{
    Require(result.ok() && result.status == NeoCore::ExecutionStatus::Completed &&
        result.value.kind() == NeoCore::Scalar::Kind::Int && result.value.asInt() == expected,
        std::string(label) + ": " + result.error.message);
}

std::vector<std::uint8_t> Save(const NeoCore::Program& program)
{
    NeoCore::Error error;
    std::vector<std::uint8_t> image;
    Require(program.SaveImage(image, error) && image.size() > 24, "save typed image: " + error.message);
    return image;
}

void RoundTrip(bool debug)
{
    int moduleLoads = 0;
    NeoCore::CompileOptions options;
    options.moduleLoader = [&moduleLoads](const std::string& name, const std::string&, NeoCore::ModuleSource& source, NeoCore::Error&) {
        ++moduleLoads;
        if (name != "child") return false;
        source.sourceName = "modules/child.nc";
        source.source =
            "public int Add(int x) { return x + 4; }\n"
            "public int Broken() {\n"
            "    int zero = 0;\n"
            "    return 1 / zero;\n"
            "}\n";
        return true;
    };
    NeoCore::Error error;
    std::unique_ptr<NeoCore::Program> original = NeoCore::Program::Compile(
        "import child;\n"
        "struct Point { float x; float y; };\n"
        "struct Config { int limit = 7; string tag = \"t\"; Vector3 up = Vector3(0, 1, 0); };\n"
        "Point point = Point(1.5, 2.5);\n"
        "list<int> values = [2, 3];\n"
        "export int count = 10;\n"
        "export int Step(int amount) { count += amount; return child.Add(count); }\n"
        "export int Check() { fn(int)->int twice = fn(int x)->int { return x * 2; }; return twice(values[0]) + toint(point.x + point.y); }\n"
        "export int Wait(int x) { sleep(0); return x + count; }\n"
        "export int Broken() { return child.Broken(); }\n"
        "export string Echo(string text) { return text; }\n"
        "export int Pick(int state) { switch (state) { case 1, 2: return 20; case 3: return 30; default: return 90; } }\n"
        "export int Defaults() { Config c; return c.limit * 100 + c.tag.len() * 10 + toint(c.up.y); }\n", error, "image.nc", debug, options);
    Require(original && error.ok(), "compile typed image fixture: " + error.message);
    Require(moduleLoads == 1, "source imports resolved once before saving");
    Int(original->Call("Step", {NeoCore::Scalar::Int(1)}), 15, "original program executes");
    const std::vector<std::uint8_t> image = Save(*original);
    const std::vector<std::uint8_t> again = Save(*original);
    Require(image == again, "repeated image writes are deterministic");
    original.reset();
    options.moduleLoader = [&moduleLoads](const std::string&, const std::string&, NeoCore::ModuleSource&, NeoCore::Error&) {
        ++moduleLoads;
        throw std::runtime_error("image loading must not request source modules");
        return false;
    };
    std::unique_ptr<NeoCore::Program> loaded = NeoCore::Program::LoadImage(image, error, options);
    Require(loaded && error.ok(), "load typed image: " + error.message);
    Require(moduleLoads == 1, "load bypasses source/module compilation");
    Int(loaded->Call("Check"), 8, "struct, collection and function type metadata survives");
    Int(loaded->Call("Pick", {NeoCore::Scalar::Int(2)}), 20, "switch jump table survives image reload");
    Int(loaded->Call("Pick", {NeoCore::Scalar::Int(3)}), 30, "switch table second entry after reload");
    Int(loaded->Call("Pick", {NeoCore::Scalar::Int(7)}), 90, "switch default after reload");
    Int(loaded->Call("Defaults"), 711, "struct field defaults survive image reload");
    Int(loaded->Call("Step", {NeoCore::Scalar::Int(2)}), 16, "loaded image initializes fresh globals");
    Require(!loaded->Call("Step", {NeoCore::Scalar::String("wrong")}).ok(), "loaded signature rejects invalid argument");
    Int(loaded->Call("Step", {NeoCore::Scalar::Int(0)}), 16, "rejected image call has no script effects");
    NeoCore::Result wait = loaded->Call("Wait", {NeoCore::Scalar::Int(7)});
    Require(wait.ok() && wait.suspended(), "loaded invocation suspends");
    Int(loaded->Resume(), 19, "loaded pending signature survives suspension");
    const NeoCore::Result failure = loaded->Call("Broken");
    Require(!failure.ok(), "loaded program reports runtime error");
    if (debug) Require(failure.error.sourceName == "modules/child.nc" && failure.error.line == 4,
        "loaded image preserves original module source location: " + failure.error.sourceName + ":" + std::to_string(failure.error.line));
    const NeoCore::Result text = loaded->Call("Echo", {NeoCore::Scalar::String("owned image result")});
    Require(text.ok() && text.value.asString() == "owned image result", "loaded string return");
    std::vector<std::uint8_t> savedAgain = Save(*loaded);
    Require(savedAgain == image, "loaded image saves without source or typed IR reconstruction");
    std::unique_ptr<NeoCore::Instance> peer = loaded->CreateInstance(error);
    Require(peer && error.ok(), "loaded program creates independent instances");
    loaded.reset();
    Int(peer->Call("Step", {NeoCore::Scalar::Int(1)}), 15, "instance retains loaded program after factory disposal");
}

struct Host
{
    int calls = 0;
    std::int64_t value;
    NeoCore::HostSchema schema;
    NeoCore::TypeId type;
    std::unique_ptr<NeoCore::HostBindings> bindings;
    explicit Host(std::int64_t initial, bool mismatch = false) : value(initial)
    {
        type = schema.AddObject("ImageHost");
        const NeoCore::TypeId propertyType = mismatch ? schema.Types().Float() : schema.Types().Int();
        Require(schema.AddProperty(type, "Value", propertyType), "declare image host property");
        Require(schema.AddGlobal("Host", type), "declare image host global");
        bindings.reset(new NeoCore::HostBindings(schema,
            [](void* data, NeoCore::TypeId, NeoCore::ObjectHandle id) -> void* { return id.value == 1 ? data : 0; }, this));
        Require(bindings->BindGlobal("Host", NeoCore::ObjectHandle(1)), "bind image host ID");
        const NeoCore::HostProperty* property = schema.FindProperty(type, "Value");
        Require(bindings->Bind(property->getter, [this, mismatch](NeoCore::HostCall& call) {
            ++calls;
            if (mismatch) call.ReturnFloat(static_cast<double>(value));
            else call.ReturnInt(value);
            return true;
        }), "bind image getter");
        Require(bindings->Bind(property->setter, [this, mismatch](NeoCore::HostCall& call) {
            ++calls;
            value = mismatch ? static_cast<std::int64_t>(call.Float(0)) : call.Int(0);
            return true;
        }), "bind image setter");
    }
};

void HostRoundTrip(bool debug)
{
    NeoCore::Error error;
    Host source(40);
    std::unique_ptr<NeoCore::Program> original = NeoCore::Program::Compile(
        "export int Step() { Host.Value += 2; return Host.Value; }", *source.bindings, error, "host-image.nc", debug);
    Require(original && error.ok(), "compile host image: " + error.message);
    const std::vector<std::uint8_t> image = Save(*original);
    original.reset();
    Host replacement(100);
    std::unique_ptr<NeoCore::Program> loaded = NeoCore::Program::LoadImage(image, *replacement.bindings, error);
    Require(loaded && error.ok(), "load matching host schema: " + error.message);
    Int(loaded->Call("Step"), 102, "image reconnects supplied callbacks and object IDs");
    Require(source.calls == 0 && source.value == 40 && replacement.calls == 3, "serialized image contains no old callback pointers");
    Require(!NeoCore::Program::LoadImage(image, error), "host image requires host contract");
    Require(error.message.find("schema") != std::string::npos, "missing schema diagnostic");
    Host wrong(200, true);
    Require(!NeoCore::Program::LoadImage(image, *wrong.bindings, error), "changed property type rejects image");
    Require(error.message.find("schema") != std::string::npos && wrong.calls == 0, "schema rejected before execution");
    Require(loaded->SetHostObject("Host", NeoCore::ObjectHandle(99), error), "replace bound ID after image load");
    const NeoCore::Result invalid = loaded->Call("Step");
    Require(!invalid.ok(), "loaded host access revalidates object liveness");
    if (debug) Require(invalid.error.sourceName == "host-image.nc" && invalid.error.line == 1,
        "loaded host failure preserves source location");
}

void MalformedImages()
{
    NeoCore::Error error;
    std::unique_ptr<NeoCore::Program> program = NeoCore::Program::Compile("export int Answer() { return 42; }", error);
    Require(program && error.ok(), "compile malformed-image fixture");
    const std::vector<std::uint8_t> image = Save(*program);
    for (std::size_t length : {std::size_t(0), std::size_t(1), std::size_t(8), std::size_t(23), image.size() / 2, image.size() - 1})
    {
        const std::vector<std::uint8_t> truncated(image.begin(), image.begin() + length);
        Require(!NeoCore::Program::LoadImage(truncated, error) && !error.ok(), "truncated image rejected");
    }
    for (std::size_t offset : {std::size_t(0), std::size_t(8), std::size_t(12), std::size_t(16),
        std::size_t(24), image.size() / 2, image.size() - 1})
    {
        std::vector<std::uint8_t> corrupted = image;
        corrupted[offset] ^= 0x40;
        Require(!NeoCore::Program::LoadImage(corrupted, error) && !error.ok(), "corrupted image rejected");
    }
    std::vector<std::uint8_t> appended = image;
    appended.push_back(0);
    Require(!NeoCore::Program::LoadImage(appended, error), "trailing bytes rejected");
    Int(program->Call("Answer"), 42, "failed loads do not damage existing program");

    NeoCore::Image::Data data;
    Require(NeoCore::Image::Read(image, 0, data, error), "read metadata mutation fixture");
    NeoCore::Image::Data edited = data;
    edited.checked.functions.push_back(edited.checked.functions.front());
    std::vector<std::uint8_t> rebuilt;
    Require(NeoCore::Image::Write(edited, 0, rebuilt, error), "write duplicate signature fixture");
    Require(!NeoCore::Program::LoadImage(rebuilt, error), "duplicate function metadata rejected with valid checksum");
    edited = data;
    edited.checked.functions.front().returnType = NeoCore::InvalidType;
    Require(NeoCore::Image::Write(edited, 0, rebuilt, error), "write invalid return type fixture");
    Require(!NeoCore::Program::LoadImage(rebuilt, error), "invalid return type rejected with valid checksum");
    edited = data;
    edited.checked.functions.front().parameterTypes.push_back(edited.checked.types.Int());
    Require(NeoCore::Image::Write(edited, 0, rebuilt, error), "write mismatched arity fixture");
    Require(!NeoCore::Program::LoadImage(rebuilt, error) && error.message.find("contract") != std::string::npos,
        "image signatures checked against actual bytecode arity");
    edited = data;
    edited.checked.functions.front().name = "Other";
    Require(NeoCore::Image::Write(edited, 0, rebuilt, error), "write mismatched export fixture");
    Require(!NeoCore::Program::LoadImage(rebuilt, error), "image export names checked against actual bytecode");
    edited = data;
    edited.checked.hostPrefix = "user_prefix_";
    Require(NeoCore::Image::Write(edited, 0, rebuilt, error), "write invalid host prefix fixture");
    Require(!NeoCore::Program::LoadImage(rebuilt, error), "internal host prefix validated");
    edited = data;
    edited.bytecode[0] ^= 0x40;
    Require(NeoCore::Image::Write(edited, 0, rebuilt, error), "write invalid backend image fixture");
    Require(!NeoCore::Program::LoadImage(rebuilt, error), "backend bytecode validation still runs");
    edited = data;
    NeoCore::TypeId deep = edited.checked.types.Int();
    for (unsigned i = 0; i < 260; ++i) deep = edited.checked.types.ListOf(deep);
    Require(NeoCore::Image::Write(edited, 0, rebuilt, error), "write excessive type depth fixture");
    Require(!NeoCore::Program::LoadImage(rebuilt, error), "excessive composite type nesting rejected");
}
}

int main()
{
    try
    {
        for (bool debug : {false, true}) { RoundTrip(debug); HostRoundTrip(debug); }
        MalformedImages();
        std::printf("NeoCore image regression passed: %d checks\n", checks);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "NeoCore image regression failed: %s\n", error.what());
        return 1;
    }
}
