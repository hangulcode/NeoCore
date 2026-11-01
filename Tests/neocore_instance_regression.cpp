#include "NeoCore.h"
#include <cstdio>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
void Require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}
void CompleteInt(const NeoCore::Result& result, std::int64_t expected, const char* context)
{
    Require(result.ok() && result.status == NeoCore::ExecutionStatus::Completed &&
        result.value.kind() == NeoCore::Scalar::Kind::Int && result.value.asInt() == expected,
        std::string(context) + ": " + result.error.message);
}
void Suspended(const NeoCore::Result& result, const char* context)
{
    Require(result.ok() && result.suspended(), std::string(context) + ": " + result.error.message);
}
std::unique_ptr<NeoCore::Program> Compile(const std::string& source, bool debug)
{
    NeoCore::Error error;
    std::unique_ptr<NeoCore::Program> result = NeoCore::Program::Compile(source, error, "instances.nc", debug);
    Require(result && error.ok(), "compile: " + error.message);
    return result;
}
void TestInstances(bool debug)
{
    std::unique_ptr<NeoCore::Program> program = Compile(
        "int count = 0;\n"
        "export int Step() { count += 1; return count; }\n"
        "export int Wait(int value) { count += 1; sleep(0); count += value; return count; }\n"
        "export string Text(string value) { sleep(0); return value; }\n"
        "export float Number(float value) { sleep(0); return value; }\n"
        "export bool Flag(bool value) { sleep(0); return value; }\n"
        "export void Pause(int duration) { sleep(duration); }\n"
        "export int Broken() { sleep(0); int zero = 0; return 5 / zero; }\n", debug);
    NeoCore::Error error;
    std::unique_ptr<NeoCore::Instance> first = program->CreateInstance(error);
    Require(first && error.ok(), "create first");
    std::unique_ptr<NeoCore::Instance> second = program->CreateInstance(error);
    Require(second && error.ok(), "create second");
    CompleteInt(program->Call("Step"), 1, "default instance first step");
    CompleteInt(first->Call("Step"), 1, "first independent globals");
    CompleteInt(second->Call("Step"), 1, "second independent globals");
    Suspended(first->Call("Wait", {NeoCore::Scalar::Int(5)}), "first yields");
    Require(first->IsSuspended(), "pending first");
    const NeoCore::Result busy = first->Call("Step");
    Require(!busy.ok() && busy.status == NeoCore::ExecutionStatus::Failed && first->IsSuspended(), "busy call rejected");
    CompleteInt(second->Call("Step"), 2, "other instance remains runnable");
    Require(!first->Resume(-1.0).ok() && first->IsSuspended(), "negative frame delta preserves pending call");
    Require(!first->Resume((std::numeric_limits<double>::quiet_NaN)()).ok() && first->IsSuspended(), "NaN frame delta rejected");
    Require(!first->Resume((std::numeric_limits<double>::infinity)()).ok() && first->IsSuspended(), "infinite frame delta rejected");
    CompleteInt(first->Resume(), 7, "resume returns retained int");
    Require(!first->IsSuspended() && !first->Resume().ok(), "resume requires pending execution");
    CompleteInt(first->Call("Step"), 8, "busy call had no effects");
    Suspended(program->Call("Wait", {NeoCore::Scalar::Int(4)}), "default instance yields");
    Require(program->IsSuspended(), "default pending status");
    CompleteInt(program->Resume(), 6, "default instance resumes");

    // 팩토리 수명과 인스턴스 수명은 독립적이고 반환 문자열도 호출 프레임을 빌리지 않는다.
    program.reset();
    Suspended(first->Call("Text", {NeoCore::Scalar::String("retained text")}), "string yields");
    const NeoCore::Result text = first->Resume();
    Require(text.ok() && !text.suspended() && text.value.asString() == "retained text", "resumed string copied");
    CompleteInt(first->Call("Step"), 9, "execution after factory destruction");
    Require(text.value.asString() == "retained text", "copied string survives another call");
    Suspended(first->Call("Number", {NeoCore::Scalar::Float(1.25)}), "float yields");
    const NeoCore::Result number = first->Resume();
    Require(number.ok() && number.value.asFloat() == 1.25, "resumed float");
    Suspended(first->Call("Flag", {NeoCore::Scalar::Bool(true)}), "bool yields");
    const NeoCore::Result flag = first->Resume();
    Require(flag.ok() && flag.value.asBool(), "resumed bool");
    Suspended(first->Call("Pause", {NeoCore::Scalar::Int(-1)}), "negative duration clamps to cooperative zero");
    const NeoCore::Result pause = first->Resume();
    Require(pause.ok() && !pause.suspended() && pause.value.kind() == NeoCore::Scalar::Kind::Void, "void resume");
    Require(!first->Call("Pause", {NeoCore::Scalar::Int(2147483648LL)}).ok(), "sleep duration upper bound");
    CompleteInt(first->Call("Step"), 10, "error does not poison later calls");
    Suspended(first->Call("Broken"), "error function yields first");
    const NeoCore::Result failure = first->Resume();
    Require(!failure.ok() && failure.status == NeoCore::ExecutionStatus::Failed && !first->IsSuspended(), "resumed runtime error");
    Require(failure.error.message.find("zero") != std::string::npos, "resumed error message preserved");
    if (debug) Require(failure.error.line == 8, "resumed error source line preserved");
    CompleteInt(first->Call("Step"), 11, "call after resumed runtime error");
    Suspended(second->Call("Pause", {NeoCore::Scalar::Int(2147483647LL)}), "long sleep returns without blocking");
    Suspended(second->Resume(86400.0), "frame delta does not advance real sleep timer");
    second.reset();
    CompleteInt(first->Call("Step"), 12, "destroying suspended peer preserves runtime");
}
void TestInitialization(bool debug)
{
    std::unique_ptr<NeoCore::Program> program = Compile(
        "int count = 1; sleep(0); count += 2; sleep(0); count += 3;\n"
        "export int Add(int amount) { count += amount; return count; }\n", debug);
    // 최초 Call의 인자와 실행 요청이 초기화 대기를 넘어 보존된다.
    std::vector<NeoCore::Scalar> arguments(1, NeoCore::Scalar::Int(7));
    Suspended(program->Call("Add", arguments), "implicit init yields");
    arguments[0] = NeoCore::Scalar::Int(100);
    Suspended(program->Resume(), "second initialization sleep");
    CompleteInt(program->Resume(), 13, "deferred exported call executes once");
    CompleteInt(program->Call("Add", {NeoCore::Scalar::Int(1)}), 14, "init is not replayed");
    NeoCore::Error error;
    std::unique_ptr<NeoCore::Instance> explicitInit = program->CreateInstance(error);
    Require(explicitInit && error.ok(), "create explicit init instance");
    Suspended(explicitInit->Run(), "explicit init yields");
    Suspended(explicitInit->Run(), "repeated Run keeps pending initialization");
    Suspended(explicitInit->Resume(), "explicit second init yield");
    const NeoCore::Result initialized = explicitInit->Resume();
    Require(initialized.ok() && !initialized.suspended() && initialized.value.kind() == NeoCore::Scalar::Kind::Void, "explicit init completes");
    CompleteInt(explicitInit->Call("Add", {NeoCore::Scalar::Int(1)}), 7, "independent explicit initialization");
    std::unique_ptr<NeoCore::Program> deferredSleep = Compile(
        "sleep(0); export int Work(int x) { sleep(0); return x; }", debug);
    Suspended(deferredSleep->Call("Work", {NeoCore::Scalar::Int(23)}), "deferred request init pending");
    Suspended(deferredSleep->Resume(), "deferred request itself sleeps");
    CompleteInt(deferredSleep->Resume(), 23, "deferred request result retained");
    std::unique_ptr<NeoCore::Program> failedInit = Compile(
        "int zero = 0; sleep(0); int value = 7 / zero; export int Read() { return value; }", debug);
    Suspended(failedInit->Call("Read"), "failing init yields first");
    const NeoCore::Result initError = failedInit->Resume();
    const NeoCore::Result cachedError = failedInit->Call("Read");
    Require(!initError.ok() && !cachedError.ok(), "failed init does not run requested function");
    if (debug) Require(initError.error.line == 1 && cachedError.error.line == 1 &&
        initError.error.sourceName == "instances.nc", "resumed initialization source location cached");
}

// 호출 결과를 변수로 직접 받을 때도 일시 정지·실패 중에는 기존 값을 보존한다.
void TestDirectCallWrites(bool debug)
{
    auto program=Compile(
        "export int target=9;export string text=\"old\";\n"
        "int Wait(int value){sleep(0);return value;}\n"
        "int Broken(){sleep(0);int zero=0;return 1/zero;}\n"
        "string Delayed(string value){sleep(0);return value;}\n"
        "export int Assign(){target=Wait(target+4);return target;}\n"
        "export int Fail(){target=Broken();return target;}\n"
        "export int Local(int value){value=Wait(value+1);return value;}\n"
        "export string Text(){text=Delayed(text..\"!\");return text;}\n",debug);
    NeoCore::Error error;
    std::vector<std::uint8_t> image;
    Require(program->SaveImage(image,error),"save direct call writes image");
    for(int pass=0;pass<2;++pass)
    {
        if(pass) program=NeoCore::Program::LoadImage(image,error);
        Require(bool(program),"load direct call writes image: "+error.message);
        auto instance=program->CreateInstance(error);
        Require(bool(instance),"create direct call writes instance");
        Suspended(instance->Call("Assign"),"direct call assignment waits");
        Require(instance->Cancel(error),"cancel pending direct assignment");
        CompleteInt(instance->ReadGlobal("target"),9,"cancelled pending call preserves global destination");
        Suspended(instance->Call("Assign"),"direct call assignment waits again");
        CompleteInt(instance->Resume(),13,"direct call stores on return");
        Suspended(instance->Call("Fail"),"failing direct call waits");
        Require(instance->Cancel(error),"cancel pending failing assignment");
        CompleteInt(instance->ReadGlobal("target"),13,"cancelled pending failure preserves global destination");
        Suspended(instance->Call("Fail"),"failing direct call waits again");
        Require(!instance->Resume().ok(),"direct callee failure propagates");
        CompleteInt(instance->ReadGlobal("target"),13,"failed call preserves global destination");
        Suspended(instance->Call("Local",{NeoCore::Scalar::Int(7)}),"parameter call assignment waits");
        CompleteInt(instance->Resume(),8,"parameter destination survives reentry and resume");
        Suspended(instance->Call("Text"),"string call assignment waits");
        Require(instance->Cancel(error),"cancel pending string assignment");
        const auto old=instance->ReadGlobal("text");
        Require(old.ok() && old.value.asString()=="old","cancelled pending call retains the old string");
        Suspended(instance->Call("Text"),"string call assignment waits again");
        const auto text=instance->Resume();
        Require(text.ok() && text.value.asString()=="old!","string call stores owned return value");
    }
}
}
int main()
{
    try
    {
        for (bool debug : {false, true}) { TestInstances(debug); TestInitialization(debug); TestDirectCallWrites(debug); }
        NeoCore::Error error;
        Require(!NeoCore::Program::Compile("export void F() { sleep(0.5); }", error), "sleep rejects float");
        Require(!NeoCore::Program::Compile("export void F() { sleep(); }", error), "sleep rejects wrong arity");
        std::puts("NeoCore instance regression passed");
        return 0;
    }
    catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
