#include "NeoCoreC.h"
#include "NeoCore.h"

#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

struct NcProgram { std::unique_ptr<NeoCore::Program> value; };
struct NcInstance { std::unique_ptr<NeoCore::Instance> value; };
struct NcArguments { std::vector<NeoCore::Scalar> values; };
struct NcResult { NeoCore::Result value; };

namespace
{
std::string ReadText(const char* text, uint32_t length)
{
    if (!text && length) throw std::invalid_argument("null UTF-8 input with nonzero length");
    return length ? std::string(text, length) : std::string();
}

NcResult* Failure(const char* message)
{
    try
    {
        std::unique_ptr<NcResult> result(new NcResult());
        result->value.status = NeoCore::ExecutionStatus::Failed;
        result->value.error.message = message;
        return result.release();
    }
    catch (...) { return 0; }
}

NcResult* CopyResult(const NeoCore::Result& value)
{
    std::unique_ptr<NcResult> result(new NcResult());
    result->value = value;
    return result.release();
}

NcResult* CopyError(const NeoCore::Error& error)
{
    NeoCore::Result result;
    result.error = error;
    if (!error.ok()) result.status = NeoCore::ExecutionStatus::Failed;
    return CopyResult(result);
}

const char* TextResult(const std::string& text, uint32_t* length)
{
    if (length) *length = static_cast<uint32_t>(text.size());
    return text.c_str();
}

const char* EmptyText(uint32_t* length)
{
    if (length) *length = 0;
    return "";
}
}

extern "C"
{
NcProgram* NcCompile(const char* source, uint32_t sourceLength,
    const char* sourceName, uint32_t nameLength, int32_t debug, NcResult** result)
{
    if (!result) return 0;
    *result = 0;
    try
    {
        NeoCore::Error error;
        std::unique_ptr<NcProgram> program(new NcProgram());
        program->value = NeoCore::Program::Compile(ReadText(source, sourceLength), error,
            ReadText(sourceName, nameLength), debug != 0);
        *result = CopyError(error);
        return program->value ? program.release() : 0;
    }
    catch (const std::exception& error) { *result = Failure(error.what()); }
    catch (...) { *result = Failure("unexpected native compile failure"); }
    return 0;
}

void NcProgramDestroy(NcProgram* program) { delete program; }

NcInstance* NcInstanceCreate(NcProgram* program, NcResult** result)
{
    if (!result) return 0;
    *result = 0;
    try
    {
        if (!program) throw std::invalid_argument("program is null");
        NeoCore::Error error;
        std::unique_ptr<NcInstance> instance(new NcInstance());
        instance->value = program->value->CreateInstance(error);
        *result = CopyError(error);
        return instance->value ? instance.release() : 0;
    }
    catch (const std::exception& error) { *result = Failure(error.what()); }
    catch (...) { *result = Failure("unexpected native instance failure"); }
    return 0;
}

void NcInstanceDestroy(NcInstance* instance) { delete instance; }

NcResult* NcRun(NcInstance* instance)
{
    try
    {
        if (!instance) return Failure("instance is null");
        return CopyResult(instance->value->Run());
    }
    catch (const std::exception& error) { return Failure(error.what()); }
    catch (...) { return Failure("unexpected native execution failure"); }
}

NcResult* NcCall(NcInstance* instance, const char* name, uint32_t nameLength,
    const NcArguments* arguments)
{
    try
    {
        if (!instance) return Failure("instance is null");
        const std::vector<NeoCore::Scalar> empty;
        return CopyResult(instance->value->Call(ReadText(name, nameLength),
            arguments ? arguments->values : empty));
    }
    catch (const std::exception& error) { return Failure(error.what()); }
    catch (...) { return Failure("unexpected native call failure"); }
}

NcResult* NcResume(NcInstance* instance, double deltaTime)
{
    try
    {
        if (!instance) return Failure("instance is null");
        return CopyResult(instance->value->Resume(deltaTime));
    }
    catch (const std::exception& error) { return Failure(error.what()); }
    catch (...) { return Failure("unexpected native resume failure"); }
}

NcArguments* NcArgumentsCreate(void)
{
    try { return new NcArguments(); }
    catch (...) { return 0; }
}
void NcArgumentsDestroy(NcArguments* arguments) { delete arguments; }

int32_t NcArgumentsInt(NcArguments* arguments, int64_t value)
{
    try { if (!arguments) return 0; arguments->values.push_back(NeoCore::Scalar::Int(value)); return 1; }
    catch (...) { return 0; }
}
int32_t NcArgumentsFloat(NcArguments* arguments, double value)
{
    try { if (!arguments) return 0; arguments->values.push_back(NeoCore::Scalar::Float(value)); return 1; }
    catch (...) { return 0; }
}
int32_t NcArgumentsBool(NcArguments* arguments, int32_t value)
{
    try { if (!arguments) return 0; arguments->values.push_back(NeoCore::Scalar::Bool(value != 0)); return 1; }
    catch (...) { return 0; }
}
int32_t NcArgumentsString(NcArguments* arguments, const char* value, uint32_t length)
{
    try { if (!arguments) return 0; arguments->values.push_back(NeoCore::Scalar::String(ReadText(value, length))); return 1; }
    catch (...) { return 0; }
}
int32_t NcArgumentsNull(NcArguments* arguments)
{
    try { if (!arguments) return 0; arguments->values.push_back(NeoCore::Scalar::Null()); return 1; }
    catch (...) { return 0; }
}

void NcResultDestroy(NcResult* result) { delete result; }
int32_t NcResultStatus(const NcResult* result)
{
    if (!result) return NC_FAILED;
    switch (result->value.status)
    {
    case NeoCore::ExecutionStatus::Completed: return NC_COMPLETED;
    case NeoCore::ExecutionStatus::Suspended: return NC_SUSPENDED;
    case NeoCore::ExecutionStatus::Failed: return NC_FAILED;
    }
    return NC_FAILED;
}
int32_t NcResultKind(const NcResult* result)
{
    if (!result) return NC_VOID;
    switch (result->value.value.kind())
    {
    case NeoCore::Scalar::Kind::Void: return NC_VOID;
    case NeoCore::Scalar::Kind::Int: return NC_INT;
    case NeoCore::Scalar::Kind::Float: return NC_FLOAT;
    case NeoCore::Scalar::Kind::Bool: return NC_BOOL;
    case NeoCore::Scalar::Kind::String: return NC_STRING;
    case NeoCore::Scalar::Kind::Null: return NC_NULL;
    }
    return NC_VOID;
}
int64_t NcResultInt(const NcResult* result)
{
    return result && NcResultKind(result) == NC_INT ? result->value.value.asInt() : 0;
}
double NcResultFloat(const NcResult* result)
{
    return result && NcResultKind(result) == NC_FLOAT ? result->value.value.asFloat() : 0;
}
int32_t NcResultBool(const NcResult* result)
{
    return result && NcResultKind(result) == NC_BOOL && result->value.value.asBool() ? 1 : 0;
}
const char* NcResultString(const NcResult* result, uint32_t* length)
{
    return result && NcResultKind(result) == NC_STRING
        ? TextResult(result->value.value.asString(), length) : EmptyText(length);
}
const char* NcResultError(const NcResult* result, uint32_t* length)
{
    return result ? TextResult(result->value.error.message, length) : EmptyText(length);
}
const char* NcResultSource(const NcResult* result, uint32_t* length)
{
    return result ? TextResult(result->value.error.sourceName, length) : EmptyText(length);
}
int32_t NcResultLine(const NcResult* result) { return result ? result->value.error.line : 0; }
int32_t NcResultColumn(const NcResult* result) { return result ? result->value.error.column : 0; }
}
