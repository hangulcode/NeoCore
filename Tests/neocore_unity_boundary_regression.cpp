#include "NeoCoreC.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

namespace
{
typedef std::unique_ptr<NcProgram, void (*)(NcProgram*)> Program;
typedef std::unique_ptr<NcInstance, void (*)(NcInstance*)> Instance;
typedef std::unique_ptr<NcArguments, void (*)(NcArguments*)> Arguments;
typedef std::unique_ptr<NcResult, void (*)(NcResult*)> Result;
int checks = 0;

void Require(bool condition, const std::string& message)
{
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

std::string Error(NcResult* result)
{
    uint32_t length = 0;
    const char* text = NcResultError(result, &length);
    return std::string(text, length);
}

void Status(NcResult* result, int expected, const char* label)
{
    Require(result && NcResultStatus(result) == expected, std::string(label) + ": " + Error(result));
}

Program Compile(const char* source, bool debug)
{
    NcResult* raw = 0;
    Program program(NcCompile(source, static_cast<uint32_t>(std::strlen(source)),
        "boundary.nc", 11, debug ? 1 : 0, &raw), &NcProgramDestroy);
    Result result(raw, &NcResultDestroy);
    Status(result.get(), NC_COMPLETED, "compile");
    Require(program.get() != 0, "compiled program");
    return program;
}

Instance Create(NcProgram* program)
{
    NcResult* raw = 0;
    Instance instance(NcInstanceCreate(program, &raw), &NcInstanceDestroy);
    Result result(raw, &NcResultDestroy);
    Status(result.get(), NC_COMPLETED, "create instance");
    Require(instance.get() != 0, "created instance");
    return instance;
}

Result Call(NcInstance* instance, const char* name, NcArguments* arguments = 0)
{
    return Result(NcCall(instance, name, static_cast<uint32_t>(std::strlen(name)), arguments), &NcResultDestroy);
}

void Int(NcResult* result, int64_t expected, const char* label)
{
    Status(result, NC_COMPLETED, label);
    Require(NcResultKind(result) == NC_INT && NcResultInt(result) == expected, label);
}

void Reject(NcResult* result, const char* label)
{
    Status(result, NC_FAILED, label);
    Require(!Error(result).empty(), std::string(label) + " diagnostic");
}

void Values(bool debug)
{
    Program program = Compile(
        "int calls = 0;\n"
        "export int EchoInt(int x) { calls += 1; return x; }\n"
        "export float EchoFloat(float x) { calls += 1; return x; }\n"
        "export string EchoText(string x) { calls += 1; return x; }\n"
        "export bool EchoBool(bool x) { calls += 1; return x; }\n"
        "export int Count() { return calls; }\n"
        "export string Nothing() { return null; }\n"
        "export void NoResult() {}\n", debug);
    Instance instance = Create(program.get());
    for (int64_t value : { (std::numeric_limits<int64_t>::min)(), INT64_C(-1), INT64_C(0),
        INT64_C(9007199254740993), (std::numeric_limits<int64_t>::max)() })
    {
        Arguments args(NcArgumentsCreate(), &NcArgumentsDestroy);
        Require(NcArgumentsInt(args.get(), value) != 0, "append exact integer");
        Result result = Call(instance.get(), "EchoInt", args.get());
        Int(result.get(), value, "int64 boundary");
    }
    for (double value : { -0.0, 0.0, 0.1, (std::numeric_limits<double>::max)(),
        (std::numeric_limits<double>::infinity)(), -(std::numeric_limits<double>::infinity)(),
        (std::numeric_limits<double>::quiet_NaN)() })
    {
        Arguments args(NcArgumentsCreate(), &NcArgumentsDestroy);
        Require(NcArgumentsFloat(args.get(), value) != 0, "append double");
        Result result = Call(instance.get(), "EchoFloat", args.get());
        Status(result.get(), NC_COMPLETED, "double boundary");
        const double actual = NcResultFloat(result.get());
        Require(NcResultKind(result.get()) == NC_FLOAT &&
            (std::isnan(value) ? std::isnan(actual) : actual == value), "double preserves payload");
        if (value == 0.0) Require(std::signbit(actual) == std::signbit(value), "signed zero preserved");
    }

    const char bytes[] = {'a', '\0', '\xe2', '\x98', '\x83', 'z'};
    Arguments args(NcArgumentsCreate(), &NcArgumentsDestroy);
    Require(NcArgumentsString(args.get(), bytes, sizeof(bytes)) != 0, "append length-delimited UTF-8");
    Result text = Call(instance.get(), "EchoText", args.get());
    Status(text.get(), NC_COMPLETED, "embedded null string");
    args.reset();
    uint32_t length = 0;
    const char* retained = NcResultString(text.get(), &length);
    Require(length == sizeof(bytes) && std::memcmp(retained, bytes, length) == 0, "binary-safe string result");
    Result count = Call(instance.get(), "Count");
    Int(count.get(), 13, "successful argument calls counted once");
    instance.reset();
    program.reset();
    Require(length == sizeof(bytes) && std::memcmp(retained, bytes, length) == 0,
        "owned result survives argument, instance and program destruction");
    Int(count.get(), 13, "integer result survives runtime destruction");
}

void RejectBeforeExecution(bool debug)
{
    Program program = Compile(
        "int calls = 0; export int Accept(int x) { calls += 1; return x; }\n"
        "export int Count() { return calls; }\n"
        "export string Nullable(string x) { return x; }\n"
        "int Private() { return 17; }\n", debug);
    Instance instance = Create(program.get());
    Result missing = Call(instance.get(), "Accept");
    Reject(missing.get(), "missing argument");
    Arguments extra(NcArgumentsCreate(), &NcArgumentsDestroy);
    Require(NcArgumentsInt(extra.get(), 1) && NcArgumentsInt(extra.get(), 2), "append excess arguments");
    Result tooMany = Call(instance.get(), "Accept", extra.get());
    Reject(tooMany.get(), "excess arguments");
    Arguments wrong(NcArgumentsCreate(), &NcArgumentsDestroy);
    Require(NcArgumentsString(wrong.get(), "wrong", 5) != 0, "append wrong type");
    Result mismatch = Call(instance.get(), "Accept", wrong.get());
    Reject(mismatch.get(), "wrong argument type");
    Arguments null(NcArgumentsCreate(), &NcArgumentsDestroy);
    Require(NcArgumentsNull(null.get()) != 0, "append null");
    Result notNullable = Call(instance.get(), "Accept", null.get());
    Reject(notNullable.get(), "null into an int argument");
    Result nullable = Call(instance.get(), "Nullable", null.get());
    Status(nullable.get(), NC_COMPLETED, "null string argument");
    Require(NcResultKind(nullable.get()) == NC_NULL, "null string result kind");
    Result unknown = Call(instance.get(), "Missing");
    Reject(unknown.get(), "unknown function");
    Result privateFunction = Call(instance.get(), "Private");
    Reject(privateFunction.get(), "private function inaccessible");
    Result count = Call(instance.get(), "Count");
    Int(count.get(), 0, "rejected calls have no script side effects");
}

void SuspendedOwnership(bool debug)
{
    Program program = Compile(
        "int count = 0; sleep(0); count = 10;\n"
        "export int Wait(int value) { count += value; sleep(0); return count; }\n"
        "export int Read() { return count; }\n"
        "export string WaitText(string value) { sleep(0); return value; }\n", debug);
    Instance first = Create(program.get());
    Instance second = Create(program.get());
    Arguments args(NcArgumentsCreate(), &NcArgumentsDestroy);
    Require(NcArgumentsInt(args.get(), 7) != 0, "deferred argument");
    Result start = Call(first.get(), "Wait", args.get());
    Status(start.get(), NC_SUSPENDED, "global initialization suspension");
    args.reset();
    program.reset();
    Result busy = Call(first.get(), "Read");
    Reject(busy.get(), "busy instance rejects extra calls");
    Result invalidDelta(NcResume(first.get(), -1.0), &NcResultDestroy);
    Reject(invalidDelta.get(), "negative delta rejected without losing pending call");
    Result middle(NcResume(first.get(), 0.0), &NcResultDestroy);
    Status(middle.get(), NC_SUSPENDED, "deferred call reaches own suspension");
    Result finish(NcResume(first.get(), 0.0), &NcResultDestroy);
    Int(finish.get(), 17, "deferred arguments survive caller destruction");
    Result secondStart = Call(second.get(), "Read");
    Status(secondStart.get(), NC_SUSPENDED, "other instance owns separate initialization");
    Result secondFinish(NcResume(second.get(), 0.0), &NcResultDestroy);
    Int(secondFinish.get(), 10, "other instance globals remain independent");
    Arguments text(NcArgumentsCreate(), &NcArgumentsDestroy);
    Require(NcArgumentsString(text.get(), "retained", 8) != 0, "suspended string argument");
    Result textStart = Call(first.get(), "WaitText", text.get());
    Status(textStart.get(), NC_SUSPENDED, "string return suspended");
    text.reset();
    second.reset();
    Result textFinish(NcResume(first.get(), 0.0), &NcResultDestroy);
    Status(textFinish.get(), NC_COMPLETED, "string return resumes after peer disposal");
    first.reset();
    uint32_t length = 0;
    const char* value = NcResultString(textFinish.get(), &length);
    Require(length == 8 && std::memcmp(value, "retained", 8) == 0, "resumed string owns its bytes");
}

void InvalidBoundary()
{
    NcResult* raw = 0;
    Program missingSource(NcCompile(0, 1, "bad.nc", 6, 1, &raw), &NcProgramDestroy);
    Result error(raw, &NcResultDestroy);
    Require(!missingSource, "null source with nonzero length rejected");
    Reject(error.get(), "invalid source pointer diagnostic");
    Instance missing(Create(Compile("export int Value() { return 1; }", true).get()));
    Arguments args(NcArgumentsCreate(), &NcArgumentsDestroy);
    Require(!NcArgumentsString(args.get(), 0, 1), "invalid argument pointer rejected");
    Require(!NcArgumentsInt(0, 1), "null argument builder rejected");
    Result result = Call(missing.get(), "Value", args.get());
    Int(result.get(), 1, "failed append does not alter arguments");
    uint32_t length = 99;
    const char* empty = NcResultString(result.get(), &length);
    Require(empty && length == 0, "wrong result accessor returns empty view");
    Require(NcResultFloat(result.get()) == 0 && !NcResultBool(result.get()), "wrong scalar accessors return defaults");
    Result nullRun(NcRun(0), &NcResultDestroy);
    Reject(nullRun.get(), "null instance run rejected");
    Result nullResume(NcResume(0, 0), &NcResultDestroy);
    Reject(nullResume.get(), "null instance resume rejected");
    NcProgramDestroy(0);
    NcInstanceDestroy(0);
    NcArgumentsDestroy(0);
    NcResultDestroy(0);
}

void Vectors(bool debug)
{
    Program program = Compile(
        "export Vector2 V2(Vector2 x) { return x; }\n"
        "export Vector3 V3(Vector3 x) { return x; }\n"
        "export Vector4 V4(Vector4 x) { return x; }\n"
        "export Quaternion Q(Quaternion x) { return x; }\n"
        "export Vector3 Wait(Vector3 x) { sleep(0); return x; }\n", debug);
    Instance instance = Create(program.get());
    const int kinds[] = {NC_VECTOR2, NC_VECTOR3, NC_VECTOR4, NC_QUATERNION};
    const int dimensions[] = {2, 3, 4, 4};
    const char* names[] = {"V2", "V3", "V4", "Q"};
    for (int index = 0; index < 4; ++index)
    {
        float input[] = {1.0f, 2.0f, 3.0f, 4.0f};
        Arguments arguments(NcArgumentsCreate(), &NcArgumentsDestroy);
        Require(NcArgumentsVector(arguments.get(), kinds[index], input) != 0, "append sized vector");
        input[0] = 99.0f;
        Result result = Call(instance.get(), names[index], arguments.get());
        Status(result.get(), NC_COMPLETED, "sized vector result");
        Require(NcResultKind(result.get()) == kinds[index], "nominal vector result kind");
        float output[] = {-1.0f, -2.0f, -3.0f, -4.0f, 1234.0f};
        Require(NcResultVector(result.get(), output, 4) != 0, "read sized vector");
        for (int component = 0; component < dimensions[index]; ++component)
            Require(output[component] == static_cast<float>(component + 1), "vector argument copied by value");
        for (int component = dimensions[index]; component < 4; ++component)
            Require(output[component] == static_cast<float>(-component - 1), "vector read only writes requested dimension");
        Require(output[4] == 1234.0f, "vector read preserves trailing canary");
        Require(!NcResultVector(result.get(), output, dimensions[index] - 1), "undersized vector output rejected");
        Require(!NcResultVector(result.get(), 0, 4), "null vector output rejected");
        const int other = index == 0 ? 1 : 0;
        Result mismatch = Call(instance.get(), names[other], arguments.get());
        Reject(mismatch.get(), "wrong vector dimension rejected by signature");
        if (index >= 2)
        {
            Result nominal = Call(instance.get(), names[index == 2 ? 3 : 2], arguments.get());
            Reject(nominal.get(), "Vector4 and Quaternion remain nominally distinct");
        }
    }
    const float value[] = {5.0f, 6.0f, 7.0f};
    Arguments args(NcArgumentsCreate(), &NcArgumentsDestroy);
    Require(!NcArgumentsVector(args.get(), NC_INT, value), "invalid vector kind rejected");
    Require(!NcArgumentsVector(args.get(), NC_VECTOR3, 0), "null vector input rejected");
    Require(NcArgumentsVector(args.get(), NC_VECTOR3, value) != 0, "append suspended vector");
    Result wait = Call(instance.get(), "Wait", args.get());
    Status(wait.get(), NC_SUSPENDED, "vector result suspension");
    args.reset();
    Result resumed(NcResume(instance.get(), 0.0), &NcResultDestroy);
    Status(resumed.get(), NC_COMPLETED, "vector result resumes");
    instance.reset();
    program.reset();
    float output[] = {0, 0, 0};
    Require(NcResultKind(resumed.get()) == NC_VECTOR3 && NcResultVector(resumed.get(), output, 3) &&
        output[0] == 5 && output[1] == 6 && output[2] == 7, "owned vector survives disposed runtime");
}

void ExecutionControls(bool debug)
{
    Program program = Compile(
        "int entries = 0; int value = 10;\n"
        "export int Work(int limit) { entries += 1; int sum = 0; for (int i in 0, limit) sum += i; return sum; }\n"
        "export int Read() { return value; }\n"
        "export int Count() { return entries; }\n"
        "export int Wait() { value += 3; sleep(0); value += 100; return value; }\n", debug);
    Instance instance = Create(program.get());
    Instance peer = Create(program.get());
    Arguments args(NcArgumentsCreate(), &NcArgumentsDestroy);
    Require(NcArgumentsInt(args.get(), 1000) != 0, "budget argument");
    Result invalid(NcCallLimited(instance.get(), "Work", 4, args.get(), -2, 0), &NcResultDestroy);
    Reject(invalid.get(), "invalid timeout rejected");
    Result untouched = Call(instance.get(), "Count");
    Int(untouched.get(), 0, "invalid timeout has no side effects");
    Result budget(NcCallLimited(instance.get(), "Work", 4, args.get(), -1, 7), &NcResultDestroy);
    Status(budget.get(), NC_SUSPENDED, "instruction budget suspension");
    Require(NcIsSuspended(instance.get()) && !NcIsRunning(instance.get()), "suspended execution is not actively running");
    Result peerValue = Call(peer.get(), "Read");
    Int(peerValue.get(), 10, "peer instance runs during suspension");
    unsigned slices = 0;
    do
    {
        budget.reset(NcResumeBudget(instance.get(), 0.0, 19));
        Require(budget && NcResultStatus(budget.get()) != NC_FAILED, "budget resume succeeds");
        ++slices;
    } while (NcResultStatus(budget.get()) == NC_SUSPENDED && slices < 10000);
    Int(budget.get(), 499500, "budgeted calculation matches unlimited result");
    Require(slices > 1 && !NcIsSuspended(instance.get()), "multiple slices finish");
    Result count = Call(instance.get(), "Count");
    Int(count.get(), 1, "resuming does not repeat function entry");
    Result unlimited = Call(instance.get(), "Work", args.get());
    Int(unlimited.get(), 499500, "later calls do not inherit previous budget");
    Result waiting = Call(instance.get(), "Wait");
    Status(waiting.get(), NC_SUSPENDED, "cancel fixture suspended");
    Result cancel(NcCancel(instance.get()), &NcResultDestroy);
    Status(cancel.get(), NC_COMPLETED, "cancel pending execution");
    Require(!NcIsSuspended(instance.get()), "cancel clears suspended state");
    Result cancelledValue = Call(instance.get(), "Read");
    Int(cancelledValue.get(), 13, "cancel preserves prior global effects and skips remainder");
    Result cannotResume(NcResume(instance.get(), 0.0), &NcResultDestroy);
    Reject(cannotResume.get(), "cancelled invocation cannot resume");
    Result waitingAgain = Call(instance.get(), "Wait");
    Status(waitingAgain.get(), NC_SUSPENDED, "reset fixture suspended");
    Result reset(NcReset(instance.get()), &NcResultDestroy);
    Status(reset.get(), NC_COMPLETED, "reset suspended execution");
    Require(!NcIsSuspended(instance.get()), "reset clears suspended state");
    Result initialValue = Call(instance.get(), "Read");
    Int(initialValue.get(), 10, "reset restores initial global state");
    Result initialEntries = Call(instance.get(), "Count");
    Int(initialEntries.get(), 0, "reset restores invocation counter");
    Result peerRead = Call(peer.get(), "Read");
    Int(peerRead.get(), 10, "reset leaves peer state intact");
    Result timed(NcCallLimited(instance.get(), "Work", 4, args.get(), 0, 0), &NcResultDestroy);
    Status(timed.get(), NC_SUSPENDED, "zero timeout cooperatively yields");
    slices = 0;
    do
    {
        timed.reset(NcResume(instance.get(), 0.0));
        Require(timed && NcResultStatus(timed.get()) != NC_FAILED, "timeout resume succeeds");
        ++slices;
    } while (NcResultStatus(timed.get()) == NC_SUSPENDED && slices < 10000);
    Int(timed.get(), 499500, "timeout slices retain correct result");
}
}

int main()
{
    try
    {
        for (bool debug : {false, true})
        {
            Values(debug);
            RejectBeforeExecution(debug);
            SuspendedOwnership(debug);
            Vectors(debug);
            ExecutionControls(debug);
        }
        InvalidBoundary();
        std::printf("NeoCore Unity boundary regression passed: %d checks\n", checks);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "NeoCore Unity boundary regression failed: %s\n", error.what());
        return 1;
    }
}
