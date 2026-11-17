struct CNeoVMWorker::WorkerProfile {
    typedef std::chrono::steady_clock Clock;
    struct Frame { CoroutineInfo* context; int base; std::size_t entry; };
    struct EntryState { unsigned depth = 0; std::uint64_t since = 0; };
    NeoCore::ProfileSnapshot snapshot;
    // 함수 번호는 조밀하다. 호출마다 맵을 검색하지 않고 최초 등록한 항목을 바로 읽는다.
    std::vector<std::size_t> functions;
    std::map<std::string, std::size_t> hostNames;
    std::vector<EntryState> states;
    std::vector<Frame> hosts;
    std::vector<Frame> active;
    Clock::time_point since;
    unsigned running = 0;

    // 활성 실행 시간만 진행하는 시계다. self는 맨 위 프레임에만 더하고,
    // inclusive는 해당 함수의 재귀 깊이가 0으로 돌아올 때 한 번 정산한다.
    void Flush() {
        if (!snapshot.enabled || !running) return;
        const auto now = Clock::now();
        const auto elapsed = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now - since).count());
        since = now;
        if (active.empty()) return;
        snapshot.activeNanoseconds += elapsed;
        snapshot.entries[active.back().entry].selfNanoseconds += elapsed;
    }
    void Push(const Frame& frame) {
        active.push_back(frame);
        EntryState& state = states[frame.entry];
        if (state.depth++ == 0) state.since = snapshot.activeNanoseconds;
    }
    void Pop() {
        const std::size_t entry = active.back().entry;
        EntryState& state = states[entry];
        assert(state.depth > 0);
        if (--state.depth == 0)
            snapshot.entries[entry].inclusiveNanoseconds += snapshot.activeNanoseconds - state.since;
        active.pop_back();
    }
    void ClearActive() { while (!active.empty()) Pop(); }
    void Select(const Frame& frame) {
        // 같은 컨텍스트에서 호출은 push 하나, 복귀는 끝난 프레임의 pop만 필요하다.
        while (!active.empty() && active.back().context == frame.context && active.back().base > frame.base) Pop();
        if (!active.empty() && active.back().context == frame.context && active.back().base == frame.base) {
            if (active.back().entry == frame.entry) return;
            Pop();
        }
        Push(frame);
    }
    std::size_t AddEntry(NeoCore::ProfileEntry entry) {
        states.emplace_back();
        try { snapshot.entries.push_back(std::move(entry)); }
        catch (...) { states.pop_back(); throw; }
        return snapshot.entries.size() - 1;
    }
    std::size_t Function(CNeoVMProgram& program, int index) {
        const std::size_t missing = static_cast<std::size_t>(-1);
        if (functions.empty()) functions.resize(program.functions.size(), missing);
        if (functions[index] != missing) return functions[index];
        NeoCore::ProfileEntry entry;
        entry.functionId = index;
        const auto name = program.debugFunctionNames.find(index);
        entry.name = name != program.debugFunctionNames.end() ? name->second : "function#" + std::to_string(index);
        if (name == program.debugFunctionNames.end())
            for (const auto& exported : program.exportFunctions)
                if (exported.second == index) { entry.name = exported.first; break; }
        const int op = program.functions[index]._codePtr / sizeof(SVMOperation);
        if (op >= 0 && op < static_cast<int>(program.debugData.size())) entry.line = program.debugData[op]._lineseq;
        return functions[index] = AddEntry(std::move(entry));
    }
};

// 실행 루프 선택은 한 번의 설정 읽기로 끝낸다. 상태 객체가 있어도 비활성일 수 있다.
bool CNeoVMWorker::IsProfilingEnabled() const {
    return m_profile && m_profile->snapshot.enabled;
}

bool CNeoVMWorker::ConfigureProfile(bool enabled, bool reset) {
    if (m_bInRun || m_pMainCtx != nullptr) return false;
    if (!m_profile && (enabled || reset)) m_profile.reset(new WorkerProfile());
    if (!m_profile) return true;
    if (reset) *m_profile = WorkerProfile();
    m_profile->ClearActive();
    m_profile->snapshot.enabled = enabled;
    m_profile->running = 0;
    m_profile->hosts.clear();
    return true;
}
NeoCore::ProfileSnapshot CNeoVMWorker::ReadProfile() const {
    if (!m_profile) return NeoCore::ProfileSnapshot();
    NeoCore::ProfileSnapshot result = m_profile->snapshot;
    // 아직 복귀하지 않은 함수도 마지막 Flush까지의 값을 보여 준다. 읽기는 시계를 진행하지 않는다.
    for (std::size_t i = 0; i < m_profile->states.size(); ++i)
        if (m_profile->states[i].depth)
            result.entries[i].inclusiveNanoseconds += result.activeNanoseconds - m_profile->states[i].since;
    return result;
}
void CNeoVMWorker::ProfileRefresh(int base) {
    WorkerProfile& p = *m_profile;
    if (!p.snapshot.enabled) return;
    p.Flush();
    if (!m_pCur) { p.ClearActive(); return; }
    const int sp = base >= 0 ? base : _iSP_Vars;
    if (!p.active.empty() && p.active.back().context == m_pCur) {
        // 네이티브 호출 창은 스크립트 프레임이 아니다. 재진입한 자식만 걷고 호스트 항목은 유지한다.
        const int nativeCount = m_pCur->m_sNativeFrames.size();
        if (nativeCount && m_pCur->m_sNativeFrames[nativeCount - 1]._window == sp) {
            while (!p.active.empty() && p.active.back().context == m_pCur && p.active.back().base > sp) p.Pop();
        } else if (_functionIndex >= 0) {
            const WorkerProfile::Frame frame = {m_pCur, sp, p.Function(*_pProgram, _functionIndex)};
            p.Select(frame);
        }
        return;
    }

    // 새 실행/중단 재개/코루틴 전환만 전체 경로를 복원한다. 평상시 호출·반환은 위에서 끝난다.
    // 같은 가상 시각에 pop/push하므로 전환 전후의 재귀 포함 시간도 겹쳐 세지 않는다.
    p.ClearActive();
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
        const int frameBase = current ? sp : ctx->_info._iSP_Vars;
        const int nativeCount = ctx->m_sNativeFrames.size();
        const bool nativeWindow = nativeCount && ctx->m_sNativeFrames[nativeCount - 1]._window == frameBase;
        if (function >= 0 && !nativeWindow) frames[frameBase] = p.Function(*_pProgram, function);
        for (const auto& host : p.hosts) if (host.context == ctx) frames[host.base] = host.entry;
        for (const auto& frame : frames) {
            const WorkerProfile::Frame active = {ctx, frame.first, frame.second};
            p.Push(active);
        }
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
    // sleep·디버거·명령 예산으로 중단되어도 이 구간은 닫는다. 재개 때 시간만 이어 가고 호출 수는 늘리지 않는다.
    if (--m_profile->running == 0) m_profile->ClearActive();
}
class CNeoVMWorker::ProfileRunScope {
    CNeoVMWorker& worker;
public:
    explicit ProfileRunScope(CNeoVMWorker& value) : worker(value) { worker.ProfileRunBegin(); }
    ~ProfileRunScope() { worker.ProfileRunEnd(); }
};
bool CNeoVMWorker::ProfileHostBegin(const std::string& name) {
    if (!m_profile || !m_profile->snapshot.enabled) return false;
    ProfileRefresh();
    WorkerProfile& p = *m_profile;
    auto found = p.hostNames.find(name);
    if (found == p.hostNames.end()) {
        NeoCore::ProfileEntry entry;
        entry.kind = NeoCore::ProfileKind::Host;
        entry.name = name;
        const std::size_t index = p.AddEntry(std::move(entry));
        found = p.hostNames.insert(std::make_pair(name, index)).first;
    }
    const WorkerProfile::Frame host = {m_pCur, _iSP_Vars, found->second};
    p.hosts.push_back(host);
    p.Select(host);
    ++p.snapshot.entries[host.entry].calls;
    return true;
}
void CNeoVMWorker::ProfileHostEnd() {
    WorkerProfile& p = *m_profile;
    p.Flush();
    const WorkerProfile::Frame host = p.hosts.back();
    p.hosts.pop_back();
    // 호스트 안에서 실패한 자식이 남아 있어도 여기서 함께 닫는다. 해제 경로에는 할당이 없다.
    while (!p.active.empty()) {
        const WorkerProfile::Frame frame = p.active.back();
        p.Pop();
        if (frame.context == host.context && frame.base == host.base && frame.entry == host.entry) return;
    }
    p.snapshot.enabled = false;
    p.snapshot.complete = false;
}
