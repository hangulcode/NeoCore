#include "NeoCoreDebugger.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <utility>

namespace NeoCore
{
struct Debugger::Impl : NeoScript::IDebugListener
{
    struct Target
    {
        Instance* instance = nullptr;
        std::weak_ptr<int> lifetime;
        NeoScript::InstanceHandle handle;
        bool stopped = false;
        bool faulted = false;
        DebugLocation location;
        std::vector<DebugStackFrame> frames;
        std::map<int, std::vector<DebugVariable>> variables;
    };
    Runtime::Impl* state;
    NeoScript::IDebugger* backend;
    NeoCore::IDebugListener* listener = nullptr;
    std::vector<Target> targets;

    explicit Impl(Runtime::Impl* owner) : state(owner), backend(owner->runtime->GetDebugger())
    { if (backend) backend->SetListener(this); }
    ~Impl() { if (backend) backend->SetListener(nullptr); }

    Target* Find(Instance& instance)
    {
        if (!instance.impl_ || instance.impl_->module->runtimeState.get() != state) return nullptr;
        for (Target& target : targets)
            if (!target.lifetime.expired() && target.instance == &instance &&
                target.handle.id == instance.impl_->instance.id && target.handle.generation == instance.impl_->instance.generation &&
                target.lifetime.lock() == instance.impl_->debugLifetime) return &target;
        return nullptr;
    }
    static std::string Normalize(std::string path)
    {
        std::replace(path.begin(), path.end(), '\\', '/');
#ifdef _WIN32
        std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
        return path;
    }
    static DebugLocation Location(const Program::Impl& program, int line)
    {
        DebugLocation value;
        value.sourceName = program.sourceName; value.line = line;
        if (line > 0 && static_cast<std::size_t>(line) <= program.bundle.lines.size())
        {
            const ModuleSourceLine& source = program.bundle.lines[static_cast<std::size_t>(line - 1)];
            value.sourceName = source.sourceName; value.line = source.line;
        }
        return value;
    }
    static std::string Name(const Program::Impl& program, const std::string& name)
    {
        const auto found = program.bundle.symbolNames.find(name);
        return found == program.bundle.symbolNames.end() ? name : found->second;
    }
    static DebugVariable Variable(const NeoScript::DebugVariable& source)
    {
        DebugVariable result;
        result.name = source.name; result.type = source.type; result.value = source.value;
        for (const auto& child : source.children) result.children.push_back(Variable(child));
        return result;
    }
    static void Value(const Program::Impl& program, DebugVariable& value, TypeId id, unsigned depth = 0)
    {
        if (depth > 16) return;
        const TypeRegistry& types = program.checked.types;
        const TypeDesc& type = types.Get(id);
        value.type = types.DiagnosticName(id);
        // null 을 담을 수 있는 타입의 null 값은 자식 없이 그대로 보인다.
        if (value.value == "null" && types.AllowsNull(id)) return;
        if (type.kind == TypeKind::Struct)
        {
            value.type = Name(program, type.name); value.value = value.type;
            for (std::size_t i = 0; i < value.children.size() && i < type.fields.size(); ++i)
            { value.children[i].name = type.fields[i].name; Value(program, value.children[i], type.fields[i].type, depth + 1); }
        }
        else if (type.kind == TypeKind::List || type.kind == TypeKind::Array || type.kind == TypeKind::Array32 || type.kind == TypeKind::Set)
            for (auto& child : value.children) Value(program, child, type.element, depth + 1);
        else if (type.kind == TypeKind::Map)
            for (auto& child : value.children) Value(program, child, type.value, depth + 1);
    }
    static void Variables(const Program::Impl& program, const std::vector<NeoScript::DebugVariable>& raw, std::vector<DebugVariable>& output)
    {
        output.clear(); std::set<std::string> names;
        for (const auto& source : raw)
        {
            const auto found = std::find_if(program.checked.debugSymbols.begin(), program.checked.debugSymbols.end(),
                [&](const StaticDebugSymbol& symbol) { return symbol.backendName == source.name; });
            if (found == program.checked.debugSymbols.end()) continue;
            DebugVariable value = Variable(found->boxed && !source.children.empty() ? source.children.front() : source);
            value.name = Name(program, found->name);
            if (!names.insert(value.name).second) continue;
            Value(program, value, found->type);
            output.push_back(std::move(value));
        }
    }
    void Capture(Target& target, const NeoScript::DebugLocation& location, bool faulted)
    {
        const Program::Impl& program = *target.instance->impl_->module;
        target.stopped = true; target.faulted = faulted;
        target.location = Location(program, location.line);
        target.frames.clear(); target.variables.clear();
        std::vector<NeoScript::DebugStackFrame> frames;
        backend->GetStackTrace(target.handle, frames);
        if (frames.size() > 64) frames.resize(64);
        for (const auto& frame : frames)
        {
            // 0행은 명시적으로 표시한 공통 도우미다. 사용자 모듈 함수와 람다 프레임은 그대로 남긴다.
            if (frame.line == 0) continue;
            DebugStackFrame translated;
            translated.frameId = frame.frameId; translated.functionName = Name(program, frame.functionName);
            translated.location = Location(program, frame.line); target.frames.push_back(translated);
            std::vector<NeoScript::DebugVariable> variables;
            backend->GetFrameVariables(target.handle, frame.frameId, variables);
            Variables(program, variables, target.variables[frame.frameId]);
        }
    }
    void OnStopped(NeoScript::InstanceHandle handle, const NeoScript::DebugLocation& location, NeoScript::DebugStopReason reason) override
    {
        for (Target& target : targets)
        {
            if (target.lifetime.expired() || target.handle.id != handle.id || target.handle.generation != handle.generation) continue;
            Capture(target, location, reason == NeoScript::DebugStopReason::Exception);
            const DebugLocation stoppedLocation = target.location;
            if (listener)
            {
                // 정지 알림에서는 조회만 허용하고 같은 런타임의 실행 재진입을 막는다.
                struct NotificationScope
                {
                    explicit NotificationScope(unsigned& count) : count_(count) { ++count_; }
                    ~NotificationScope() { --count_; }
                    unsigned& count_;
                } notification(state->debugNotifications);
                listener->OnStopped(*target.instance, stoppedLocation, static_cast<DebugStopReason>(reason));
            }
            return;
        }
    }
    bool Control(Instance& instance, int operation)
    {
        Target* target = Find(instance);
        if (!target || !backend || instance.IsExecuting()) return false;
        if (operation != 4 && (!backend->IsPaused(target->handle) || target->faulted)) return false;
        switch (operation)
        {
        case 0: backend->Continue(target->handle); break;
        case 1: backend->StepInto(target->handle); break;
        case 2: backend->StepOver(target->handle); break;
        case 3: backend->StepOut(target->handle); break;
        case 4:
            if (target->faulted)
            {
                backend->Continue(target->handle);
                target->faulted = target->stopped = false;
                target->frames.clear(); target->variables.clear();
            }
            backend->Pause(target->handle); return true;
        default: return false;
        }
        target->stopped = false; target->frames.clear(); target->variables.clear();
        return true;
    }
};

Debugger::Debugger(Runtime& runtime) : impl_(new Impl(runtime.impl_.get())) {}
Debugger::~Debugger() = default;
bool Debugger::Attach(Instance& instance, Error& error)
{
    error = Error();
    if (!impl_->backend || !instance.impl_ || instance.impl_->module->runtimeState.get() != impl_->state)
    { error.message = "debug instance must belong to this runtime"; return false; }
    if (instance.IsExecuting()) { error.message = "cannot attach while the instance is executing"; return false; }
    if (impl_->Find(instance)) return true;
    impl_->targets.erase(std::remove_if(impl_->targets.begin(), impl_->targets.end(), [](const Impl::Target& target) { return target.lifetime.expired(); }), impl_->targets.end());
    Impl::Target target;
    target.instance = &instance; target.lifetime = instance.impl_->debugLifetime; target.handle = instance.impl_->instance;
    impl_->targets.push_back(target);
    // 디버거 생성 뒤 만들어진 워커에도 정지 알림 어댑터를 설치한다.
    impl_->backend->SetBreakpoints(target.handle, std::vector<NeoScript::DebugBreakpoint>());
    return true;
}
void Debugger::Detach(Instance& instance)
{
    Impl::Target* target = impl_->Find(instance);
    if (!target) return;
    if (impl_->backend)
    {
        impl_->backend->SetBreakpoints(target->handle, std::vector<NeoScript::DebugBreakpoint>());
        impl_->backend->SetSliceBudget(target->handle, 0);
        impl_->backend->Continue(target->handle);
    }
    instance.impl_->debugSliceBudget = 0;
    impl_->targets.erase(std::remove_if(impl_->targets.begin(), impl_->targets.end(), [&](const Impl::Target& value) { return value.instance == &instance; }), impl_->targets.end());
}
void Debugger::SetListener(IDebugListener* listener) { impl_->listener = listener; }
void Debugger::ClearStop(Instance& instance)
{
    Impl::Target* target = impl_->Find(instance);
    if (!target) return;
    // 오류 컨텍스트는 이미 반납되었으므로 다음 실행에는 이전 정지 모드를 남기지 않는다.
    if (target->faulted) impl_->backend->Continue(target->handle);
    target->stopped = false; target->faulted = false;
    target->location = DebugLocation(); target->frames.clear(); target->variables.clear();
}
bool Debugger::SetBreakpoints(Instance& instance, const std::vector<DebugLocation>& points, Error& error)
{
    error = Error();
    Impl::Target* target = impl_->Find(instance);
    if (!target || instance.IsExecuting()) { error.message = "debug instance is not attached or is executing"; return false; }
    std::vector<NeoScript::DebugBreakpoint> backend;
    const ModuleBundle& bundle = instance.impl_->module->bundle;
    for (const DebugLocation& point : points)
    {
        if (point.line <= 0) { error.message = "breakpoint line must be positive"; return false; }
        const std::string path = Impl::Normalize(point.sourceName);
        for (std::size_t i = 0; i < bundle.lines.size(); ++i)
        {
            if (bundle.lines[i].line != point.line || Impl::Normalize(bundle.lines[i].sourceName) != path) continue;
            NeoScript::DebugBreakpoint bp; bp.file = 0; bp.line = static_cast<int>(i + 1); backend.push_back(bp);
        }
    }
    impl_->backend->SetBreakpoints(target->handle, backend); return true;
}
bool Debugger::Continue(Instance& instance) { return impl_->Control(instance, 0); }
bool Debugger::StepInto(Instance& instance) { return impl_->Control(instance, 1); }
bool Debugger::StepOver(Instance& instance) { return impl_->Control(instance, 2); }
bool Debugger::StepOut(Instance& instance) { return impl_->Control(instance, 3); }
bool Debugger::Pause(Instance& instance) { return impl_->Control(instance, 4); }
bool Debugger::IsPaused(Instance& instance) const
{ Impl::Target* target = impl_->Find(instance); return target && impl_->backend && impl_->backend->IsPaused(target->handle); }
bool Debugger::SetSliceBudget(Instance& instance, std::uint32_t budget)
{
    Impl::Target* target = impl_->Find(instance);
    if (!target || instance.IsExecuting()) return false;
    impl_->backend->SetSliceBudget(target->handle, budget);
    instance.impl_->debugSliceBudget = budget;
    return true;
}
bool Debugger::GetLocation(Instance& instance, DebugLocation& location) const
{
    location = DebugLocation(); Impl::Target* target = impl_->Find(instance);
    if (!target || !target->stopped || (!target->faulted && !impl_->backend->IsPaused(target->handle))) return false;
    location = target->location; return true;
}
bool Debugger::GetStackTrace(Instance& instance, std::vector<DebugStackFrame>& frames) const
{
    frames.clear(); Impl::Target* target = impl_->Find(instance);
    if (!target || !target->stopped || (!target->faulted && !impl_->backend->IsPaused(target->handle))) return false;
    frames = target->frames; return true;
}
bool Debugger::GetFrameVariables(Instance& instance, int frameId, std::vector<DebugVariable>& variables) const
{
    variables.clear(); Impl::Target* target = impl_->Find(instance);
    if (!target || !target->stopped || (!target->faulted && !impl_->backend->IsPaused(target->handle))) return false;
    const auto found = target->variables.find(frameId);
    if (found == target->variables.end()) return false;
    variables = found->second; return true;
}
bool Debugger::GetExecutableLocations(const Program& program, std::vector<DebugLocation>& locations) const
{
    locations.clear();
    if (!impl_->backend || !program.impl_ || program.impl_->runtimeState.get() != impl_->state) return false;
    std::vector<NeoScript::DebugLocation> raw; impl_->backend->GetExecutableLocations(program.impl_->program, raw);
    std::set<std::pair<std::string, int>> seen;
    for (const auto& item : raw)
    {
        DebugLocation location = Impl::Location(*program.impl_, item.line);
        if (location.line > 0 && seen.insert(std::make_pair(location.sourceName, location.line)).second) locations.push_back(location);
    }
    return true;
}
bool Debugger::GetSourceFiles(const Program& program, std::vector<std::string>& files) const
{
    files.clear();
    if (!program.impl_ || program.impl_->runtimeState.get() != impl_->state) return false;
    files.push_back(program.impl_->sourceName);
    for (const auto& line : program.impl_->bundle.lines)
        if (std::find(files.begin(), files.end(), line.sourceName) == files.end()) files.push_back(line.sourceName);
    return true;
}
}
