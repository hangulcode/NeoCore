struct CNeoVMWorker::WorkerProfile {
    typedef std::chrono::steady_clock Clock;
    struct Host { CoroutineInfo* context; int base; std::size_t entry; };
    NeoCore::ProfileSnapshot snapshot;
    std::map<int, std::size_t> functions;
    std::map<std::string, std::size_t> hostNames;
    std::vector<Host> hosts;
    std::vector<std::size_t> active;
    Clock::time_point since;
    unsigned running = 0;

    void Flush() {
        if (!snapshot.enabled || !running) return;
        const auto now = Clock::now();
        const auto elapsed = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now - since).count());
        since = now;
        if (active.empty()) return;
        snapshot.activeNanoseconds += elapsed;
        snapshot.entries[active.back()].selfNanoseconds += elapsed;
        for (std::size_t entry : active) snapshot.entries[entry].inclusiveNanoseconds += elapsed;
    }
    std::size_t Function(CNeoVMProgram& program, int index) {
        const auto found = functions.find(index);
        if (found != functions.end()) return found->second;
        NeoCore::ProfileEntry entry;
        entry.functionId = index;
        const auto name = program.debugFunctionNames.find(index);
        entry.name = name != program.debugFunctionNames.end() ? name->second : "function#" + std::to_string(index);
        if (name == program.debugFunctionNames.end())
            for (const auto& exported : program.exportFunctions)
                if (exported.second == index) { entry.name = exported.first; break; }
        const int op = program.functions[index]._codePtr / sizeof(SVMOperation);
        if (op >= 0 && op < static_cast<int>(program.debugData.size())) entry.line = program.debugData[op]._lineseq;
        const std::size_t result = snapshot.entries.size();
        snapshot.entries.push_back(std::move(entry));
        functions[index] = result;
        return result;
    }
};

bool CNeoVMWorker::ConfigureProfile(bool enabled, bool reset) {
    if (m_bInRun || m_pMainCtx != nullptr) return false;
    if (!m_profile && (enabled || reset)) m_profile.reset(new WorkerProfile());
    if (!m_profile) return true;
    if (reset) *m_profile = WorkerProfile();
    m_profile->snapshot.enabled = enabled;
    m_profile->running = 0;
    m_profile->active.clear();
    m_profile->hosts.clear();
    return true;
}
NeoCore::ProfileSnapshot CNeoVMWorker::ReadProfile() const {
    return m_profile ? m_profile->snapshot : NeoCore::ProfileSnapshot();
}
void CNeoVMWorker::ProfileRefresh(int base) {
    WorkerProfile& p = *m_profile;
    if (!p.snapshot.enabled) return;
    p.Flush();
    p.active.clear();
    auto context = [&](CoroutineInfo* ctx, bool current) {
        if (!ctx) return;
        std::map<int, std::size_t> frames;
        for (int i = 0; i < ctx->m_sCallStack.size(); ++i) {
            const SCallStack& frame = ctx->m_sCallStack[i];
            if (frame._functionIndex >= 0) frames[frame._iSP_Vars] = p.Function(*_pProgram, frame._functionIndex);
        }
        for (int i = 0; i < ctx->m_sNativeFrames.size(); ++i) {
            const SNativeFrame& frame = ctx->m_sNativeFrames[i];
            if (frame._functionIndex >= 0) frames[frame._iSP_Vars] = p.Function(*_pProgram, frame._functionIndex);
        }
        const int function = current ? _functionIndex : ctx->_info._functionIndex;
        const int sp = current ? (base >= 0 ? base : _iSP_Vars) : ctx->_info._iSP_Vars;
        bool nativeWindow = false;
        for (int i = 0; i < ctx->m_sNativeFrames.size(); ++i)
            if (ctx->m_sNativeFrames[i]._window == sp) nativeWindow = true;
        if (function >= 0 && !nativeWindow) frames[sp] = p.Function(*_pProgram, function);
        for (const auto& host : p.hosts) if (host.context == ctx) frames[host.base] = host.entry;
        for (const auto& frame : frames) p.active.push_back(frame.second);
    };
    for (auto it = m_sCoroutines.rbegin(); it != m_sCoroutines.rend(); ++it) context(*it, false);
    context(m_pCur, true);
}
void CNeoVMWorker::ProfileEntered(int functionIndex) {
    if (!m_profile->snapshot.enabled) return;
    ++m_profile->snapshot.entries[m_profile->Function(*_pProgram, functionIndex)].calls;
}
void CNeoVMWorker::ProfileRunBegin() {
    if (!m_profile || !m_profile->snapshot.enabled) return;
    ProfileRefresh();
    if (!m_profile->running++) m_profile->since = WorkerProfile::Clock::now();
}
void CNeoVMWorker::ProfileRunEnd() {
    if (!m_profile || !m_profile->snapshot.enabled) return;
    m_profile->Flush();
    --m_profile->running;
}
class CNeoVMWorker::ProfileRunScope {
    CNeoVMWorker& worker;
public:
    explicit ProfileRunScope(CNeoVMWorker& value) : worker(value) { worker.ProfileRunBegin(); }
    ~ProfileRunScope() { worker.ProfileRunEnd(); }
};
bool CNeoVMWorker::ProfileHostBegin(const std::string& name) {
    if (!m_profile || !m_profile->snapshot.enabled) return false;
    WorkerProfile& p = *m_profile;
    p.Flush();
    auto found = p.hostNames.find(name);
    if (found == p.hostNames.end()) {
        NeoCore::ProfileEntry entry;
        entry.kind = NeoCore::ProfileKind::Host;
        entry.name = name;
        const std::size_t index = p.snapshot.entries.size();
        p.snapshot.entries.push_back(std::move(entry));
        found = p.hostNames.insert(std::make_pair(name, index)).first;
    }
    ++p.snapshot.entries[found->second].calls;
    WorkerProfile::Host host = {m_pCur, _iSP_Vars, found->second};
    p.hosts.push_back(host);
    ProfileRefresh();
    return true;
}
void CNeoVMWorker::ProfileHostEnd() {
    m_profile->Flush();
    m_profile->hosts.pop_back();
    try { ProfileRefresh(); }
    catch (...) { m_profile->snapshot.enabled = false; m_profile->snapshot.complete = false; }
}
