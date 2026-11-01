#include "ConsoleTooling.h"
#include "ConsoleHost.h"
#include "HttpTransport.h"
#include "../../NeoSource/NeoCoreDebugger.h"
#include "../../NeoSource/NeoCoreCompiler.h"
#include "../../NeoSource/NeoCoreModules.h"
#include "../../NeoSource/NeoCoreBuiltins.h"
#include "../../ThirdParty/yyjson/yyjson.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <unordered_map>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#include <poll.h>
#define _dup dup
#define _dup2 dup2
#define _close close
#define _read read
#define _fileno fileno
#define _fdopen fdopen
#define _O_BINARY 0
static int _pipe(int fd[2], unsigned, int) { return pipe(fd); }
#endif

using NeoCore::IDebugListener;
using NeoCore::DebugStopReason;
using NeoCore::DebugLocation;
using NeoCore::DebugStackFrame;
using NeoCore::DebugVariable;
namespace
{
struct BuiltinInfo
{
    std::string module;
    std::string name;
    int argCount = -1;
    std::string ret;
    std::vector<std::string> params;
    std::string typeParameters;
};
static std::string FullPathOrSelf(const std::string& path)
{
#ifdef _WIN32
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1, nullptr, 0);
    if (!count) return path;
    std::vector<wchar_t> wide(static_cast<std::size_t>(count));
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1, wide.data(), count);
    const DWORD size = GetFullPathNameW(wide.data(), 0, nullptr, nullptr);
    std::vector<wchar_t> full(static_cast<std::size_t>(size) + 1);
    const DWORD length = GetFullPathNameW(wide.data(), static_cast<DWORD>(full.size()), full.data(), nullptr);
    if (!length || length >= full.size()) return path;
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, full.data(), static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(bytes), '\0');
    if (bytes) WideCharToMultiByte(CP_UTF8, 0, full.data(), static_cast<int>(length), &result[0], bytes, nullptr, nullptr);
    std::replace(result.begin(), result.end(), '\\', '/');
    return result;
#else
    char* resolved = realpath(path.c_str(), nullptr);
    if (!resolved) return path;
    const std::string result(resolved);
    std::free(resolved);
    return result;
#endif
}

static bool ReadSource(const std::string& path, std::string& source)
{
    FILE* file = nullptr;
#ifdef _WIN32
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1, nullptr, 0);
    if (!count) return false;
    std::vector<wchar_t> wide(static_cast<std::size_t>(count));
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1, wide.data(), count);
    _wfopen_s(&file, wide.data(), L"rb");
#else
    file = std::fopen(path.c_str(), "rb");
#endif
    if (!file) return false;
    source.clear();
    char buffer[4096];
    std::size_t countRead;
    while ((countRead = std::fread(buffer, 1, sizeof(buffer), file)) != 0) source.append(buffer, countRead);
    const bool ok = std::ferror(file) == 0;
    std::fclose(file);
    return ok;
}

static std::string Directory(const std::string& path)
{
    const std::size_t end = path.find_last_of("/\\");
    if (end == std::string::npos) return ".";
    if (end == 0) return path.substr(0, 1);
    if (end == 2 && path[1] == ':') return path.substr(0, 3);
    return path.substr(0, end);
}

static bool LoadModule(const std::string& name, const std::string& importer, const std::string& library,
    NeoCore::ModuleSource& result, NeoCore::Error& error,
    const std::map<std::string, std::string>* overlays = nullptr)
{
    std::vector<std::string> candidates;
    std::string directory = Directory(importer);
    for (unsigned level = 0; level < 12; ++level)
    {
        candidates.push_back(directory + "/" + name + ".nc");
        candidates.push_back(directory + "/Lib/" + name + ".nc");
        const std::string parent = Directory(directory);
        if (parent == directory) break;
        directory = parent;
    }
    if (!library.empty()) candidates.insert(candidates.begin(), library + "/" + name + ".nc");
    for (const std::string& path : candidates)
    {
        const std::string full = FullPathOrSelf(path);
        if (overlays)
        {
            const auto found = overlays->find(full);
            if (found != overlays->end()) { result.source = found->second; result.sourceName = full; return true; }
        }
        if (ReadSource(full, result.source)) { result.sourceName = full; return true; }
    }
    error.message = "module not found: " + name;
    return false;
}

static std::string JsonEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s)
    {
        switch (c)
        {
        case '\t': out += "\\t"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        default:
            if (c < 0x20) { char buf[7]; snprintf(buf, sizeof(buf), "\\u%04x", c); out += buf; }
            else out += static_cast<char>(c);
            break;
        }
    }
    return out;
}

static int Utf16Column(const std::string& source, int line, int column);

static FILE* g_DapOutput = stdout;
static std::mutex g_DapOutputMutex;
static constexpr size_t ProtocolMaxBody = 8 * 1024 * 1024;
static constexpr size_t ProtocolMaxHeader = 8192;

static bool ProtocolContentLength(const std::string& header, size_t& length)
{
    bool found = false;
    std::istringstream input(header);
    std::string line;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t colon = line.find(':');
        if (colon == std::string::npos) return false;
        std::string key = line.substr(0, colon);
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
        if (key != "content-length") continue;
        if (found) return false;
        found = true;
        size_t pos = colon + 1;
        while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
        length = 0;
        const size_t start = pos;
        while (pos < line.size() && line[pos] >= '0' && line[pos] <= '9')
        {
            size_t digit = static_cast<size_t>(line[pos++] - '0');
            if (length > (ProtocolMaxBody - digit) / 10) return false;
            length = length * 10 + digit;
        }
        if (pos == start) return false;
        while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
        if (pos != line.size() || length == 0) return false;
    }
    return found;
}

static bool DapReadMessage(std::string& body)
{
    std::string header;
    char c;
    while (std::cin.get(c))
    {
        header += c;
        if (header.size() > ProtocolMaxHeader) return false;
        if (header.size() >= 4 && header.compare(header.size() - 4, 4, "\r\n\r\n") == 0) break;
    }
    if (header.size() < 4 || header.compare(header.size() - 4, 4, "\r\n\r\n") != 0) return false;
    size_t length = 0;
    if (!ProtocolContentLength(header.substr(0, header.size() - 4), length)) return false;
    body.resize(length);
    std::cin.read(&body[0], static_cast<std::streamsize>(length));
    return static_cast<size_t>(std::cin.gcount()) == length;
}

// VM과 디버거는 같은 스레드에서 처리하고 짧은 실행 구간 사이에 요청을 읽는다.
class DapInput
{
    std::string buffered;
    size_t expected = 0;
public:
    int Poll(std::string& body)
    {
        if (!expected)
        {
            size_t end = buffered.find("\r\n\r\n");
            if (end != std::string::npos)
            {
                if (end > ProtocolMaxHeader || !ProtocolContentLength(buffered.substr(0, end), expected)) return -1;
                buffered.erase(0, end + 4);
            }
            else if (buffered.size() > ProtocolMaxHeader) return -1;
        }
        if (expected && buffered.size() >= expected)
        {
            body.assign(buffered.data(), expected);
            buffered.erase(0, expected);
            expected = 0;
            return 1;
        }
#ifdef _WIN32
        HANDLE input = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stdin)));
        DWORD available = 8192;
        if (GetFileType(input) == FILE_TYPE_PIPE)
        {
            if (!PeekNamedPipe(input, nullptr, 0, nullptr, &available, nullptr)) return -1;
            if (!available) return 0;
        }
        char bytes[8192];
        const int count = _read(_fileno(stdin), bytes, static_cast<unsigned>(std::min<DWORD>(available, sizeof(bytes))));
#else
        struct pollfd input = {fileno(stdin), POLLIN, 0};
        const int state = poll(&input, 1, 0);
        if (state < 0) return -1;
        if (!state) return 0;
        char bytes[8192];
        const int count = static_cast<int>(read(fileno(stdin), bytes, sizeof(bytes)));
#endif
        if (count <= 0) return -1;
        buffered.append(bytes, static_cast<size_t>(count));
        return 0;
    }
};

static void DapSendMessage(const std::string& body)
{
    std::lock_guard<std::mutex> lock(g_DapOutputMutex);
    FILE* out = g_DapOutput ? g_DapOutput : stdout;
    fprintf(out, "Content-Length: %zu\r\n\r\n", body.size());
    fwrite(body.data(), 1, body.size(), out);
    fflush(out);
}

class ProtocolJson
{
    yyjson_doc* doc = nullptr;
public:
    explicit ProtocolJson(const std::string& body) : doc(yyjson_read(body.data(), body.size(), 0)) {}
    ~ProtocolJson() { if (doc) yyjson_doc_free(doc); }
    yyjson_val* Root() const { return doc ? yyjson_doc_get_root(doc) : nullptr; }
};
static yyjson_val* JsonMember(yyjson_val* object, const char* key) { return yyjson_obj_get(object, key); }
static int JsonInt(yyjson_val* object, const char* key, int fallback = 0)
{
    yyjson_val* v = JsonMember(object, key);
    if (!yyjson_is_int(v)) return fallback;
    if (yyjson_is_uint(v) && yyjson_get_uint(v) > INT_MAX) return fallback;
    int64_t n = yyjson_get_sint(v);
    return n < INT_MIN || n > INT_MAX ? fallback : static_cast<int>(n);
}
static bool JsonBool(yyjson_val* object, const char* key, bool fallback = false)
{
    yyjson_val* v = JsonMember(object, key);
    return yyjson_is_bool(v) ? yyjson_get_bool(v) : fallback;
}
static std::string JsonString(yyjson_val* object, const char* key)
{
    yyjson_val* v = JsonMember(object, key);
    return yyjson_is_str(v) ? std::string(yyjson_get_str(v), yyjson_get_len(v)) : std::string();
}
static std::string JsonValueText(yyjson_val* value)
{
    if (!value) return "null";
    size_t length = 0;
    char* text = yyjson_val_write(value, 0, &length);
    std::string result = text ? std::string(text, length) : "null";
    free(text);
    return result;
}

struct ProtocolBreakpoint { int line; bool supported; };
static std::vector<ProtocolBreakpoint> JsonBreakpoints(yyjson_val* arguments)
{
    std::vector<ProtocolBreakpoint> result;
    yyjson_val* list = JsonMember(arguments, "breakpoints");
    for (size_t i = 0; i < yyjson_arr_size(list); ++i)
    {
        yyjson_val* bp = yyjson_arr_get(list, i);
        bool supported = !JsonMember(bp, "condition") && !JsonMember(bp, "hitCondition") && !JsonMember(bp, "logMessage");
        result.push_back({JsonInt(bp, "line", -1), supported});
    }
    return result;
}

// 파이프 읽기 경계에서 잘린 UTF-8 바이트를 보관하고 EOF의 미완성 문자만 대체한다.
static size_t Utf8CompletePrefix(const std::string& text)
{
    if (text.empty()) return 0;
    size_t start = text.size() - 1;
    while (start && (static_cast<unsigned char>(text[start]) & 0xc0) == 0x80) --start;
    unsigned char lead = static_cast<unsigned char>(text[start]);
    size_t width = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
    return text.size() - start < width ? start : text.size();
}


class NeoDapSession : public IDebugListener
{
public:
	std::string libraryPath;
    std::unique_ptr<NeoCore::Program> owner;

	int seq = 1;
    std::unique_ptr<NeoCore::Runtime> runtime;
    NeoCore::Debugger* dbg = nullptr;
    std::unique_ptr<NeoCore::Instance> instance;
	std::string sourcePath;
	NeoCore::Error compileError;
	std::vector<std::string> sourceFiles;
	std::map<int, std::vector<int>> breakpointsByFile;
	std::set<int> executableLines;
	std::map<int, std::set<int>> executableLinesByFile;
	DebugStopReason lastStopReason = DebugStopReason::Pause;
	DebugLocation lastStopLocation;
	bool terminated = false;
	bool initialRunStarted = false;
	bool noDebugMode = false;
	int nextVariableReference = 1000000;
    std::map<int, const std::vector<DebugVariable>*> variableReferences;
    std::map<const std::vector<DebugVariable>*, int> childReferences;
    std::map<int, std::vector<DebugVariable>> frameVariables;
    std::vector<DebugStackFrame> stoppedFrames;
    std::mutex sendMutex;
    bool running = false;
    bool pendingStop = false;
    bool faultStopped = false;
    std::string lastStopError;
    int savedStdout = -1;
    int captureWrite = -1;
    std::thread outputThread;
    std::ios::fmtflags oldCoutFlags{};

    ~NeoDapSession() { StopCapture(); if (dbg) dbg->SetListener(nullptr); instance.reset(); owner.reset(); runtime.reset(); }


	void ClearVariableReferences()
	{
		variableReferences.clear();
        childReferences.clear();
        frameVariables.clear();
        stoppedFrames.clear();
		nextVariableReference = 1000000;
	}

    const std::vector<DebugVariable>& GetFrameVariables(int frame)
    {
        static const std::vector<DebugVariable> empty;
        if (frame < 0 || frame >= 64) return empty;
        auto found = frameVariables.find(frame);
        if (found != frameVariables.end()) return found->second;
        if (faultStopped || !dbg) return empty;
        auto& vars = frameVariables[frame];
        dbg->GetFrameVariables(*instance, frame, vars);
        return vars;
    }
    int StoreVariableChildren(const std::vector<DebugVariable>& children)
    {
        if (children.empty()) return 0;
        auto found = childReferences.find(&children);
        if (found != childReferences.end()) return found->second;
        const int reference = nextVariableReference++;
        childReferences[&children] = reference;
        variableReferences[reference] = &children;
        return reference;
    }

	void WriteVariables(std::ostringstream& os, const std::vector<DebugVariable>& vars)
	{
		os << "{\"variables\":[";
		for (size_t i = 0; i < vars.size(); ++i)
		{
			if (i) os << ",";
			const int reference = StoreVariableChildren(vars[i].children);
			os << "{\"name\":\"" << JsonEscape(vars[i].name) << "\",\"type\":\"" << JsonEscape(vars[i].type)
				<< "\",\"value\":\"" << JsonEscape(vars[i].value) << "\",\"variablesReference\":" << reference << "}";
		}
		os << "]}";
	}

    virtual void OnStopped(NeoCore::Instance&, const DebugLocation& location, DebugStopReason reason) override
    {
        ClearVariableReferences();
        faultStopped = false;
        lastStopReason = reason;
        lastStopLocation = location;
        pendingStop = true;
        lastStopError.clear();
        if (dbg)
        {
            dbg->GetStackTrace(*instance, stoppedFrames);
            if (stoppedFrames.size() > 64) stoppedFrames.resize(64);
            if (reason == DebugStopReason::Exception)
                for (const auto& frame : stoppedFrames) GetFrameVariables(frame.frameId);
        }
        faultStopped = reason == DebugStopReason::Exception;
    }
    void PublishStopped()
    {
        const char* reason = lastStopReason == DebugStopReason::Breakpoint ? "breakpoint" :
            lastStopReason == DebugStopReason::Step ? "step" :
            lastStopReason == DebugStopReason::Exception ? "exception" : "pause";
        std::string body = "{\"reason\":\"" + std::string(reason) + "\",\"threadId\":1,\"allThreadsStopped\":true";
        if (!lastStopError.empty()) body += ",\"description\":\"" + JsonEscape(lastStopError) + "\"";
        SendEvent("stopped", body + "}");
        pendingStop = false;
    }

	void SendResponse(int requestSeq, const std::string& command, const std::string& body = "{}", bool success = true, const std::string& message = "")
	{
        std::lock_guard<std::mutex> lock(sendMutex);
		std::ostringstream os;
		os << "{\"seq\":" << seq++ << ",\"type\":\"response\",\"request_seq\":" << requestSeq
			<< ",\"success\":" << (success ? "true" : "false") << ",\"command\":\"" << JsonEscape(command) << "\"";
		if (!message.empty())
			os << ",\"message\":\"" << JsonEscape(message) << "\"";
		os << ",\"body\":" << body << "}";
		DapSendMessage(os.str());
	}

	void SendEvent(const std::string& event, const std::string& body = "{}")
	{
        std::lock_guard<std::mutex> lock(sendMutex);
		std::ostringstream os;
		os << "{\"seq\":" << seq++ << ",\"type\":\"event\",\"event\":\"" << JsonEscape(event) << "\",\"body\":" << body << "}";
		DapSendMessage(os.str());
	}

	void SendOutput(const std::string& output)
	{
		if (output.empty())
			return;
		SendEvent("output", "{\"category\":\"stdout\",\"output\":\"" + JsonEscape(output) + "\"}");
	}

	void SendErrorOutput(const std::string& output)
	{
		if (output.empty())
			return;
		SendEvent("output", "{\"category\":\"stderr\",\"output\":\"" + JsonEscape(output) + "\"}");
	}

	static std::string NormalizePathForCompare(std::string path)
	{
		std::replace(path.begin(), path.end(), '\\', '/');
#ifdef _WIN32
		std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return (char)tolower(c); });
#endif
		return path;
	}

	int FileIdFromSourcePath(const std::string& path) const
	{
		if (path.empty())
			return 0;
		std::string needle = NormalizePathForCompare(path);
		for (int i = 0; i < (int)sourceFiles.size(); ++i)
		{
			if (NormalizePathForCompare(sourceFiles[i]) == needle)
				return i;
		}
		if (NormalizePathForCompare(sourcePath) == needle)
			return 0;
		return -1;
	}

	bool IsExecutableLine(int file, int line) const
	{
		std::map<int, std::set<int>>::const_iterator itFile = executableLinesByFile.find(file);
		if (itFile != executableLinesByFile.end())
			return itFile->second.find(line) != itFile->second.end();
		return instance && file == 0 && executableLines.find(line) != executableLines.end();
	}

	std::string SourcePathFromFileId(int fileId) const
	{
		if (fileId >= 0 && fileId < (int)sourceFiles.size() && sourceFiles[fileId].empty() == false)
			return sourceFiles[fileId];
		return sourcePath;
	}

	static std::string SourceNameFromPath(const std::string& path)
	{
		std::string name = path;
		size_t slash = name.find_last_of("\\/");
		if (slash != std::string::npos)
			name = name.substr(slash + 1);
		return name;
	}

    void ApplyBreakpoints()
    {
        if (!instance || !dbg) return;
        std::vector<DebugLocation> points;
        for (const auto& file : breakpointsByFile)
            for (int line : file.second)
                if (IsExecutableLine(file.first, line))
                { DebugLocation point; point.sourceName = SourcePathFromFileId(file.first); point.line = line; points.push_back(point); }
        NeoCore::Error error;
        dbg->SetBreakpoints(*instance, points, error);
    }

	bool LoadProgram(const std::string& path, std::string& err, bool enableDebug)
    {
        StopCapture();
        if (dbg) dbg->SetListener(nullptr);
        instance.reset(); owner.reset(); runtime.reset(); dbg = nullptr;
        running = pendingStop = faultStopped = initialRunStarted = false;
        sourceFiles.clear(); sourcePath.clear();
        executableLines.clear(); executableLinesByFile.clear(); breakpointsByFile.clear();
        ClearVariableReferences();
        compileError = NeoCore::Error();
        const std::string fullPath = FullPathOrSelf(path);
        if (fullPath.size() < 3 || fullPath.substr(fullPath.size() - 3) != ".nc")
        { err = "NeoCore debugging requires a .nc source file"; return false; }
        std::string source;
        if (!ReadSource(fullPath, source)) { err = "cannot open program: " + fullPath; return false; }
        NeoCore::CompileOptions options;
        options.moduleLoader = [this](const std::string& name, const std::string& importer,
            NeoCore::ModuleSource& result, NeoCore::Error& error)
        { return LoadModule(name, importer, libraryPath, result, error); };
#ifdef NEOCORE_CONSOLE_HTTP
        options.httpTransport = CreateConsoleHttpTransport();
#endif
        runtime = NeoCore::Runtime::Create(compileError, options);
        if (!runtime) { err = compileError.message; return false; }
        owner = runtime->Compile(source, compileError, fullPath, enableDebug, options);
        if (!owner) { err = compileError.message; return false; }
        instance = owner->CreateInstance(compileError);
        if (!instance) { err = compileError.message; return false; }
        sourcePath = fullPath;
        dbg = runtime->GetDebugger();
        if (!dbg || !dbg->Attach(*instance, compileError))
        { err = compileError.message.empty() ? "debugger is not available in this build" : compileError.message; return false; }
        dbg->GetSourceFiles(*owner, sourceFiles);
        dbg->SetSliceBudget(*instance, 4096);
        if (enableDebug)
        {
            dbg->SetListener(this);
            std::vector<DebugLocation> locations;
            dbg->GetExecutableLocations(*owner, locations);
            for (const DebugLocation& location : locations)
                executableLinesByFile[FileIdFromSourcePath(location.sourceName)].insert(location.line);
        }
        lastStopReason = DebugStopReason::Pause;
        lastStopLocation = DebugLocation();
        return true;
    }

    void StartCapture()
    {
        if (savedStdout >= 0) return;
        fflush(stdout);
        savedStdout = _dup(_fileno(stdout));
        int pipeFd[2] = {-1, -1};
        if (savedStdout < 0 || _pipe(pipeFd, 4096, _O_BINARY) != 0)
        {
            if (savedStdout >= 0) _close(savedStdout);
            savedStdout = -1;
            return;
        }
        captureWrite = pipeFd[1];
        _dup2(captureWrite, _fileno(stdout));
        oldCoutFlags = std::cout.flags();
        std::cout.setf(std::ios::unitbuf);
        const int readFd = pipeFd[0];
        outputThread = std::thread([this, readFd]()
        {
            std::string pending;
            char buffer[512];
            for (;;)
            {
                int count = _read(readFd, buffer, sizeof(buffer));
                if (count <= 0) break;
                pending.append(buffer, static_cast<size_t>(count));
                size_t complete = Utf8CompletePrefix(pending);
                SendOutput(pending.substr(0, complete));
                pending.erase(0, complete);
            }
            if (!pending.empty()) SendOutput("\xef\xbf\xbd");
            _close(readFd);
        });
    }
    void StopCapture()
    {
        if (savedStdout < 0) return;
        fflush(stdout);
        _dup2(savedStdout, _fileno(stdout));
        _close(captureWrite);
        captureWrite = -1;
        if (outputThread.joinable()) outputThread.join();
        std::cout.flags(oldCoutFlags);
        _close(savedStdout);
        savedStdout = -1;
    }
    void RunCurrent(bool initial)
    {
        if (!instance || terminated || faultStopped) { running = false; return; }
        if (initial) SendOutput(noDebugMode ? "[NeoCore] Run started.\n" : "[NeoCore] Debug started.\n");
        StartCapture();
        const NeoCore::Result result = initial ? instance->Run() : instance->Resume(0.0, 4096);
        const bool paused = dbg && dbg->IsPaused(*instance);
        running = result.suspended() && !paused && !pendingStop;
        if (running) { fflush(stdout); return; }
        StopCapture(); // 정지 이벤트보다 앞선 출력을 모두 전송한다.
        if (!result.ok())
        {
            lastStopError = result.error.message;
            if (!lastStopError.empty()) SendErrorOutput(lastStopError + "\n");
        }
        if (pendingStop) { PublishStopped(); return; }
        if (!paused && !terminated)
        {
            SendOutput(noDebugMode ? "[NeoCore] Run finished.\n" : "[NeoCore] Debug finished.\n");
            terminated = true;
            SendEvent("terminated");
        }
    }

	void Handle(const std::string& body)
	{
        ProtocolJson json(body);
        yyjson_val* root = json.Root();
        const int requestSeq = JsonInt(root, "seq", 0);
        const std::string command = JsonString(root, "command");
        yyjson_val* arguments = JsonMember(root, "arguments");
        if (!yyjson_is_obj(root) || JsonString(root, "type") != "request" || command.empty())
        { SendResponse(requestSeq, command, "{}", false, "Invalid DAP request"); return; }
        if (running && command != "pause" && command != "disconnect" && command != "threads")
        { SendResponse(requestSeq, command, "{}", false, "Script is running"); return; }
        if (command == "continue" || command == "stepIn" || command == "next" || command == "stepOut")
        {
            if (!initialRunStarted || !instance || !dbg || !dbg->IsPaused(*instance) || faultStopped)
            { SendResponse(requestSeq, command, "{}", false, "No resumable paused script"); return; }
        }
		if (command == "initialize")
		{
			SendResponse(requestSeq, command, "{\"supportsConfigurationDoneRequest\":true,\"supportsStepInTargetsRequest\":false,\"supportsExceptionInfoRequest\":true,\"supportsEvaluateForHovers\":true}");
		}
		else if (command == "launch")
		{
			std::string path = JsonString(arguments, "program");
			if (path.empty())
				path = JsonString(arguments, "path");
			bool noDebug = JsonBool(arguments, "noDebug", false);
			noDebugMode = noDebug;
			std::string libPath = JsonString(arguments, "libPath");
            libraryPath = libPath;
			std::string err;
			bool ok = LoadProgram(path, err, !noDebug);
			if (!ok)
			{
				std::string out = err;
				if (!out.empty() && out.back() != '\n')
					out += "\n";
				SendErrorOutput(out);

				int line = 0;
				int column = 0;
				std::string message;
				if (compileError.line > 0 && compileError.column > 0)
				{
					// 가져온 모듈의 진단은 실제 원본 파일 위치를 사용한다.
                    message = compileError.message;
					std::string fullPath = FullPathOrSelf(compileError.sourceName.empty() ? path : compileError.sourceName);
					if (compileError.line) line = (int)compileError.line;
					if (compileError.column) column = (int)compileError.column;
                    std::string original;
                    if (ReadSource(fullPath, original)) column = Utf16Column(original, line, column) + 1;
					std::ostringstream eventBody;
					eventBody << "{\"source\":{\"path\":\"" << JsonEscape(fullPath) << "\"},\"line\":" << line
						<< ",\"column\":" << column << ",\"message\":\"" << JsonEscape(message)
						<< "\",\"raw\":\"" << JsonEscape(err) << "\"}";
					SendEvent("neoCoreCompileError", eventBody.str());
				}
			}
			SendResponse(requestSeq, command, "{}", ok, ok ? "" : "NeoCore compile failed. See Debug Console.");
			if (ok && noDebug)
			{
				initialRunStarted = true;
				RunCurrent(true);
			}
			else if (ok)
				SendEvent("initialized");
		}
		else if (command == "setBreakpoints")
		{
			std::string bpSourcePath = JsonString(JsonMember(arguments, "source"), "path");
			int bpFile = FileIdFromSourcePath(bpSourcePath);
			            const auto requested = JsonBreakpoints(arguments);
            std::vector<int> breakpoints;
            for (const auto& bp : requested) if (bp.supported && bp.line > 0) breakpoints.push_back(bp.line);
			if (bpFile >= 0)
				breakpointsByFile[bpFile] = breakpoints;
			ApplyBreakpoints();
			std::ostringstream os;
			os << "{\"breakpoints\":[";
			for (size_t i = 0; i < requested.size(); ++i)
			{
				if (i) os << ",";
				bool verified = requested[i].supported && (bpFile >= 0) && IsExecutableLine(bpFile, requested[i].line);
				os << "{\"verified\":" << (verified ? "true" : "false") << ",\"line\":" << requested[i].line;
				if (!verified)
					os << ",\"message\":\"No executable instruction or unsupported breakpoint condition/log message\"";
				os << "}";
			}
			os << "]}";
			SendResponse(requestSeq, command, os.str());
		}
		else if (command == "configurationDone")
		{
            if (!instance) { SendResponse(requestSeq, command, "{}", false, "No loaded script"); return; }
			SendResponse(requestSeq, command);
			if (!initialRunStarted)
			{
				initialRunStarted = true;
				RunCurrent(true);
			}
		}
		else if (command == "threads")
		{
			SendResponse(requestSeq, command, "{\"threads\":[{\"id\":1,\"name\":\"Neo VM\"}]}");
		}
		else if (command == "continue")
		{
			if (dbg) dbg->Continue(*instance);
			SendResponse(requestSeq, command, "{\"allThreadsContinued\":true}");
			RunCurrent(false);
		}
		else if (command == "stepIn")
		{
			if (dbg) dbg->StepInto(*instance);
			SendResponse(requestSeq, command);
			RunCurrent(false);
		}
		else if (command == "next")
		{
			if (dbg) dbg->StepOver(*instance);
			SendResponse(requestSeq, command);
			RunCurrent(false);
		}
		else if (command == "stepOut")
		{
			if (dbg) dbg->StepOut(*instance);
			SendResponse(requestSeq, command);
			RunCurrent(false);
		}
		else if (command == "stackTrace")
		{
            const auto& frames = stoppedFrames;
			std::ostringstream os;
			os << "{\"stackFrames\":[";
			for (size_t i = 0; i < frames.size(); ++i)
			{
				if (i) os << ",";
				std::string name = frames[i].functionName.empty() ? "<function>" : frames[i].functionName;
				std::string frameSourcePath = frames[i].location.sourceName;
				std::string frameSourceName = SourceNameFromPath(frameSourcePath);
				os << "{\"id\":" << frames[i].frameId << ",\"name\":\"" << JsonEscape(name)
					<< "\",\"line\":" << frames[i].location.line << ",\"column\":1,\"source\":{\"path\":\""
					<< JsonEscape(frameSourcePath) << "\",\"name\":\"" << JsonEscape(frameSourceName) << "\"}}";
			}
			os << "],\"totalFrames\":" << frames.size() << "}";
			SendResponse(requestSeq, command, os.str());
		}
		else if (command == "scopes")
		{
			int frameId = JsonInt(arguments, "frameId", 0);
            if (frameId < 0 || frameId >= static_cast<int>(stoppedFrames.size()))
            { SendResponse(requestSeq, command, "{}", false, "Invalid stack frame"); return; }
			std::ostringstream os;
			os << "{\"scopes\":[{\"name\":\"Locals\",\"variablesReference\":" << (1000 + frameId) << ",\"expensive\":false}]}";
			SendResponse(requestSeq, command, os.str());
		}
		else if (command == "variables")
		{
			int ref = JsonInt(arguments, "variablesReference", 1000);
            static const std::vector<DebugVariable> empty;
            const std::vector<DebugVariable>* vars = &empty;
            auto childIt = variableReferences.find(ref);
            if (childIt != variableReferences.end()) vars = childIt->second;
            else if (ref >= 1000 && ref < 1064) vars = &GetFrameVariables(ref - 1000);
            std::ostringstream os;
            WriteVariables(os, *vars);
			SendResponse(requestSeq, command, os.str());
		}
		else if (command == "evaluate")
		{
			std::string expression = JsonString(arguments, "expression");
			int frameId = JsonInt(arguments, "frameId", 0);
			while (!expression.empty() && isspace((unsigned char)expression.front()))
				expression.erase(expression.begin());
			while (!expression.empty() && isspace((unsigned char)expression.back()))
				expression.pop_back();

            const auto& vars = GetFrameVariables(frameId);
			const DebugVariable* found = nullptr;
			for (const DebugVariable& var : vars)
			{
				if (var.name == expression)
				{
					found = &var;
					break;
				}
			}

			std::ostringstream os;
			if (found)
			{
				const int reference = StoreVariableChildren(found->children);
				os << "{\"result\":\"" << JsonEscape(found->value) << "\",\"type\":\""
					<< JsonEscape(found->type) << "\",\"variablesReference\":" << reference << "}";
			}
			else
			{
				os << "{\"result\":\"not available\",\"variablesReference\":0}";
			}
			SendResponse(requestSeq, command, os.str());
		}
		else if (command == "exceptionInfo")
		{
            const std::string& description = lastStopError;
			std::ostringstream os;
			os << "{\"exceptionId\":\"NeoCore.RuntimeError\",\"description\":\"" << JsonEscape(description)
				<< "\",\"breakMode\":\"always\"}";
			SendResponse(requestSeq, command, os.str());
		}
        else if (command == "pause")
        {
            if (!running || !dbg || noDebugMode) SendResponse(requestSeq, command, "{}", false, "No running debug script");
            else { dbg->Pause(*instance); SendResponse(requestSeq, command); }
        }
        else if (command == "setExceptionBreakpoints") SendResponse(requestSeq, command);
        else if (command == "disconnect")
		{
			SendResponse(requestSeq, command);
            running = false;
            StopCapture();
			terminated = true;
		}
		else
		{
			SendResponse(requestSeq, command, "{}", false, "Unsupported DAP request");
		}
	}
};

int RunDebugAdapterImpl()
{
#ifdef _WIN32
	_setmode(_fileno(stdin), _O_BINARY);
	_setmode(_fileno(stdout), _O_BINARY);
#endif
	int dapOutFd = _dup(_fileno(stdout));
	if (dapOutFd >= 0)
	{
#ifdef _WIN32
		_setmode(dapOutFd, _O_BINARY);
#endif
		g_DapOutput = _fdopen(dapOutFd, "wb");
	}
	else
	{
		g_DapOutput = stdout;
	}
	NeoDapSession session;

    DapInput input;
    std::string body;
    while (!session.terminated)
    {
        int state = input.Poll(body);
        if (state < 0) break;
        if (state > 0) session.Handle(body);
        if (!session.terminated && session.running) session.RunCurrent(false);
        else if (state == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    session.StopCapture();
    if (session.dbg) session.dbg->SetListener(nullptr);
    session.owner.reset();
    session.runtime = nullptr;
    session.dbg = nullptr;
	if (g_DapOutput && g_DapOutput != stdout)
		fclose(g_DapOutput);
	g_DapOutput = stdout;
	return 0;
}

static bool IsNeoIdentifierChar(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		(c >= '0' && c <= '9') || c == '_';
}

static std::string GetLspLinePrefix(const std::string& source, int line, int character, bool wholePrefix = false)
{
    if (line < 0 || character < 0) return "";
    size_t begin = 0;
    for (int current = 0; current < line; ++current)
    {
        begin = source.find('\n', begin);
        if (begin == std::string::npos) return "";
        ++begin;
    }
    size_t cursor = begin;
    int units = 0;
    while (cursor < source.size() && source[cursor] != '\n' && source[cursor] != '\r' && units < character)
    {
        unsigned char lead = static_cast<unsigned char>(source[cursor]);
        size_t width = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
        int cost = width == 4 ? 2 : 1;
        if (units + cost > character || width > source.size() - cursor) break;
        cursor += width;
        units += cost;
    }
    return wholePrefix ? source.substr(0, cursor) : source.substr(begin, cursor - begin);
}

static void GetLspCompletionContext(const std::string& linePrefix, std::string& module, std::string& prefix)
{
	module.clear();
	prefix.clear();

	size_t end = linePrefix.size();
	while (end > 0 && IsNeoIdentifierChar(linePrefix[end - 1]))
		--end;
	prefix = linePrefix.substr(end);
	if (end == 0 || linePrefix[end - 1] != '.')
		return;

	size_t moduleEnd = end - 1;
	size_t moduleBegin = moduleEnd;
	while (moduleBegin > 0 && IsNeoIdentifierChar(linePrefix[moduleBegin - 1]))
		--moduleBegin;
	module = linePrefix.substr(moduleBegin, moduleEnd - moduleBegin);
}

static const BuiltinInfo* FindLspSignature(const std::vector<BuiltinInfo>& builtins,
	const std::string& linePrefix, int& activeParameter)
{
    activeParameter = 0;
    struct Delimiter { char kind; size_t offset; int commas; };
    std::vector<Delimiter> stack;
    char quote = 0;
    bool escaped = false, lineComment = false, blockComment = false;
    for (size_t i = 0; i < linePrefix.size(); ++i)
    {
        char c = linePrefix[i];
        char next = i + 1 < linePrefix.size() ? linePrefix[i + 1] : 0;
        if (lineComment) { if (c == '\n') lineComment = false; continue; }
        if (blockComment) { if (c == '*' && next == '/') { blockComment = false; ++i; } continue; }
        if (quote)
        {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == quote) quote = 0;
            continue;
        }
        if (c == '/' && next == '/') { lineComment = true; ++i; continue; }
        if (c == '/' && next == '*') { blockComment = true; ++i; continue; }
        if (c == '"' || c == '\'') { quote = c; continue; }
        bool typeArgument = c == '<' && !stack.empty() && stack.back().kind == '<';
        if (c == '<' && !typeArgument)
        {
            std::string owner, name;
            GetLspCompletionContext(linePrefix.substr(0, i), owner, name);
            for (const BuiltinInfo& info : builtins)
                if (!info.typeParameters.empty() && info.module == owner && info.name == name)
                { typeArgument = true; break; }
        }
        if (c == '(' || c == '[' || c == '{' || typeArgument) stack.push_back({c, i, 0});
        else if (c == '>' && !stack.empty() && stack.back().kind == '<') stack.pop_back();
        else if (c == ')' || c == ']' || c == '}')
        {
            if (!stack.empty()) stack.pop_back();
        }
        else if (c == ',' && !stack.empty()) ++stack.back().commas;
    }
    auto call = std::find_if(stack.rbegin(), stack.rend(), [](const Delimiter& d) { return d.kind == '('; });
    if (call == stack.rend()) return nullptr;
    const size_t openParen = call->offset;
    activeParameter = call->commas;

	size_t nameEnd = openParen;
	while (nameEnd > 0 && isspace((unsigned char)linePrefix[nameEnd - 1]))
		--nameEnd;
    const bool hasTypeArguments = nameEnd > 0 && linePrefix[nameEnd - 1] == '>';
    if (hasTypeArguments)
    {
        int depth = 0;
        do
        {
            const char c = linePrefix[--nameEnd];
            if (c == '>') ++depth;
            else if (c == '<') --depth;
        } while (nameEnd > 0 && depth != 0);
        if (depth != 0) return nullptr;
        while (nameEnd > 0 && isspace((unsigned char)linePrefix[nameEnd - 1])) --nameEnd;
    }
	size_t nameBegin = nameEnd;
	while (nameBegin > 0 && IsNeoIdentifierChar(linePrefix[nameBegin - 1]))
		--nameBegin;
	if (nameBegin == nameEnd)
		return nullptr;
	const std::string name = linePrefix.substr(nameBegin, nameEnd - nameBegin);

	std::string module;
	if (nameBegin > 0 && linePrefix[nameBegin - 1] == '.')
	{
		size_t moduleEnd = nameBegin - 1;
		size_t moduleBegin = moduleEnd;
		while (moduleBegin > 0 && IsNeoIdentifierChar(linePrefix[moduleBegin - 1]))
			--moduleBegin;
		module = linePrefix.substr(moduleBegin, moduleEnd - moduleBegin);
	}

	for (const BuiltinInfo& info : builtins)
	{
		if (info.name == name && info.module == module && !info.params.empty() &&
            (!hasTypeArguments || !info.typeParameters.empty()))
			return &info;
	}
	return nullptr;
}

static bool LspStartsWith(const std::string& value, const std::string& prefix)
{
	return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

static std::string BuildLspFunctionDetail(const BuiltinInfo& info)
{
	std::ostringstream detail;
	if (!info.ret.empty())
		detail << info.ret << " ";
	if (!info.module.empty())
		detail << info.module << ".";
	detail << info.name << info.typeParameters << "(";
	for (size_t i = 0; i < info.params.size(); ++i)
	{
		if (i > 0)
			detail << ", ";
		detail << info.params[i];
	}
	detail << ")";
	return detail.str();
}

static void AppendLspCompletionItem(std::ostringstream& os, bool& first, std::set<std::string>& emitted,
	const std::string& label, int kind, const std::string& detail, const std::string& documentation = "")
{
	if (!emitted.insert(label).second)
		return;
	if (!first)
		os << ",";
	first = false;
	os << "{\"label\":\"" << JsonEscape(label) << "\",\"kind\":" << kind
		<< ",\"detail\":\"" << JsonEscape(detail) << "\",\"insertText\":\"" << JsonEscape(label) << "\"";
	if (!documentation.empty())
		os << ",\"documentation\":\"" << JsonEscape(documentation) << "\"";
	os << "}";
}


static void AddBuiltin(std::vector<BuiltinInfo>& builtins, const std::string& owner, const std::string& name,
    const std::string& result, std::initializer_list<const char*> parameters, const std::string& typeParameters = "")
{
    BuiltinInfo info;
    info.module = owner; info.name = name; info.ret = result;
    info.typeParameters = typeParameters;
    for (const char* parameter : parameters) info.params.push_back(parameter);
    info.argCount = static_cast<int>(info.params.size());
    builtins.push_back(std::move(info));
}

static void TypedBuiltins(std::vector<BuiltinInfo>& builtins)
{
    NeoCore::TypeRegistry types;
    std::size_t count = 0;
    const NeoCore::BuiltinSignature* signatures = NeoCore::GetBuiltinSignatures(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        const NeoCore::BuiltinSignature& signature = signatures[i];
        BuiltinInfo info;
        info.module = signature.owner; info.name = signature.name;
        info.ret = types.DisplayName(NeoCore::BuiltinSignatureType(types, signature.result));
        for (unsigned p = 0; p < signature.maximum; ++p)
        {
            std::string parameter = types.DisplayName(NeoCore::BuiltinSignatureType(types, signature.parameters[p]));
            parameter += " arg" + std::to_string(p + 1);
            if (p >= signature.minimum) parameter += " (optional)";
            info.params.push_back(parameter);
        }
        info.argCount = static_cast<int>(signature.maximum);
        builtins.push_back(std::move(info));
    }
    AddBuiltin(builtins, "", "print", "void", {"scalar value", "scalar format (optional)"});
    AddBuiltin(builtins, "", "tostring", "string", {"scalar value"});
    AddBuiltin(builtins, "", "toint", "int", {"number/bool/string value"});
    AddBuiltin(builtins, "", "tofloat", "float", {"number/bool/string value"});
    AddBuiltin(builtins, "", "sleep", "void", {"int milliseconds"});
    AddBuiltin(builtins, "", "Vector2", "Vector2", {"float x", "float y"});
    AddBuiltin(builtins, "", "Vector3", "Vector3", {"float x", "float y", "float z"});
    AddBuiltin(builtins, "", "Vector4", "Vector4", {"float x", "float y", "float z", "float w"});
    AddBuiltin(builtins, "", "Quaternion", "Quaternion", {"float w", "float x", "float y", "float z"});
    AddBuiltin(builtins, "string", "len", "int", {});
    AddBuiltin(builtins, "system", "set", "set<T>", {"list<T> values"});
    AddBuiltin(builtins, "system", "jsonEncode", "string", {"T value", "bool sortKeys (optional)"});
    AddBuiltin(builtins, "system", "jsonDecode", "T", {"string text"}, "<T>");
    AddBuiltin(builtins, "system", "jsonDecodeOr", "T", {"string text", "T fallback"}, "<T>");
    AddBuiltin(builtins, "system", "jsonDecodeLenient", "T", {"string text"}, "<T>");
    AddBuiltin(builtins, "system", "jsonDecodeLenientOr", "T", {"string text", "T fallback"}, "<T>");
    AddBuiltin(builtins, "system", "jsonError", "string", {});
    AddBuiltin(builtins, "coroutine", "create", "coroutine<fn(...) -> void>", {"fn(...) -> void entry"});
    AddBuiltin(builtins, "coroutine", "resume", "void", {"coroutine<F> task", "entry arguments..."});
    AddBuiltin(builtins, "coroutine", "status", "string", {"coroutine<F> task"});
    AddBuiltin(builtins, "coroutine", "close", "void", {"coroutine<F> task"});
    for (const char* owner : {"list", "array", "array32", "map", "set"})
    {
        AddBuiltin(builtins, owner, "len", "int", {});
        AddBuiltin(builtins, owner, "clear", "void", {});
        AddBuiltin(builtins, owner, "clone", owner, {});
        AddBuiltin(builtins, owner, "contains", "bool", {"element/key value"});
    }
    for (const char* owner : {"list", "array", "array32", "map"}) AddBuiltin(builtins, owner, "reserve", "void", {"int capacity"});
    for (const char* owner : {"list", "array", "array32"})
    {
        AddBuiltin(builtins, owner, "find", "int", {"T value"});
        AddBuiltin(builtins, owner, "append", "void", {"T value"});
        AddBuiltin(builtins, owner, "resize", "void", {"int size"});
    }
    AddBuiltin(builtins, "array", "fill", "void", {"T value"});
    AddBuiltin(builtins, "array32", "fill", "void", {"T value"});
    AddBuiltin(builtins, "list", "insert", "void", {"int index", "T value"});
    AddBuiltin(builtins, "list", "remove", "T", {"int index"});
    AddBuiltin(builtins, "list", "remove_swap", "T", {"int index"});
    AddBuiltin(builtins, "list", "pop", "T", {});
    for (const char* owner : {"list", "map"}) AddBuiltin(builtins, owner, "sort", "void", {"fn(T, T) -> bool comparator"});
    AddBuiltin(builtins, "map", "remove", "bool", {"K key"});
    AddBuiltin(builtins, "map", "keys", "list<K>", {});
    AddBuiltin(builtins, "map", "values", "list<V>", {});
    AddBuiltin(builtins, "set", "add", "bool", {"T value"});
    AddBuiltin(builtins, "set", "remove", "bool", {"T value"});
    AddBuiltin(builtins, "set", "values", "list<T>", {});
}

static int HexDigit(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static std::string UriPath(const std::string& uri)
{
    if (uri.compare(0, 7, "file://") != 0) return "";
    std::string result;
    for (std::size_t i = 7; i < uri.size(); ++i)
    {
        if (uri[i] == '%' && i + 2 < uri.size() && HexDigit(uri[i + 1]) >= 0 && HexDigit(uri[i + 2]) >= 0)
        { result += static_cast<char>(HexDigit(uri[i + 1]) * 16 + HexDigit(uri[i + 2])); i += 2; }
        else result += uri[i];
    }
#ifdef _WIN32
    if (result.size() > 2 && result[0] == '/' && result[2] == ':') result.erase(0, 1);
    else if (!result.empty() && result[0] != '/') result = "//" + result;
#endif
    return FullPathOrSelf(result);
}

static std::string PathUri(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    std::string result = "file://";
    if (path.empty() || path[0] != '/') result += '/';
    const char* hex = "0123456789ABCDEF";
    for (unsigned char c : path)
    {
        if (std::isalnum(c) || c == '/' || c == ':' || c == '-' || c == '_' || c == '.' || c == '~') result += static_cast<char>(c);
        else { result += '%'; result += hex[c >> 4]; result += hex[c & 15]; }
    }
    return result;
}

static int Utf16Column(const std::string& source, int line, int column)
{
    std::size_t begin = 0;
    for (int current = 1; current < line; ++current)
    {
        begin = source.find('\n', begin);
        if (begin == std::string::npos) return 0;
        ++begin;
    }
    if (begin == 0 && source.compare(0, 3, "\xef\xbb\xbf") == 0) begin = 3;
    const std::size_t end = begin + static_cast<std::size_t>(std::max(0, column - 1));
    int units = 0;
    for (std::size_t i = begin; i < source.size() && i < end && source[i] != '\r' && source[i] != '\n';)
    {
        const unsigned char lead = static_cast<unsigned char>(source[i]);
        const std::size_t width = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
        if (i + width > end) break;
        units += width == 4 ? 2 : 1;
        i += width;
    }
    return units;
}

class NeoLspSession
{
public:
	std::unordered_map<std::string, std::string> documents;
	std::vector<BuiltinInfo> builtins;
	bool terminated = false;
    std::map<std::string, std::map<std::string, std::vector<std::string>>> diagnosticsByRoot;
    std::unique_ptr<ConsoleHost> host;
    std::vector<NeoCore::CompileDefine> defines;
    std::size_t diagnosticLimit;
    std::string implementationPath;

    NeoLspSession(const std::string& schemaPath, const std::string& implementation, const std::string& resourceRoot, std::size_t limit)
        : diagnosticLimit(limit), implementationPath(implementation.empty()?"":FullPathOrSelf(implementation))
    {
        TypedBuiltins(builtins);
        if (!schemaPath.empty())
        {
            NeoCore::HostSchemaDocument document; std::string error;
            if (!NeoCore::HostSchemaDocument::LoadFile(FullPathOrSelf(schemaPath),document,error)) throw std::runtime_error(error);
            defines=document.defines;
            host.reset(new ConsoleHost(document,implementation.empty()?"":FullPathOrSelf(implementation),resourceRoot));
        }
    }


    std::vector<BuiltinInfo> DocumentBuiltins(const std::string& source)
    {
        std::vector<BuiltinInfo> result = builtins;
        std::vector<NeoCore::StaticDeclaration> declarations;
        NeoCore::CollectStaticDeclarations(source, declarations);
        for (const NeoCore::StaticDeclaration& declaration : declarations)
        {
            if (declaration.function)
            {
                BuiltinInfo info;
                info.name = declaration.name; info.ret = declaration.type; info.params = declaration.parameters;
                info.argCount = static_cast<int>(info.params.size()); result.push_back(info);
            }
            else
            {
                std::string owner = declaration.type.substr(0, declaration.type.find_first_of("<?"));
                for (const BuiltinInfo& method : builtins)
                    if (method.module == owner) { BuiltinInfo copy = method; copy.module = declaration.name; result.push_back(std::move(copy)); }
            }
        }
        return result;
    }

    void PublishDiagnosticTarget(const std::string& target)
    {
        // 여러 열린 파일이 같은 모듈을 가져와도 한 파일의 성공이 다른 진단을 지우지 않는다.
        std::set<std::string> diagnostics;
        for (const auto& root : diagnosticsByRoot)
        {
            const auto found = root.second.find(target);
            if (found != root.second.end()) diagnostics.insert(found->second.begin(),found->second.end());
        }
        std::string values;
        for (const std::string& diagnostic : diagnostics)
        {
            if (!values.empty()) values += ",";
            values += diagnostic;
        }
        DapSendMessage("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"" +
            JsonEscape(target) + "\",\"diagnostics\":[" + values + "]}}");
    }

    void PublishDiagnostics(const std::string& uri)
    {
        const auto current = documents.find(uri);
        if (current == documents.end()) return;
        std::map<std::string, std::string> overlays;
        for (const auto& document : documents) overlays[UriPath(document.first)] = document.second;
        const std::string path = UriPath(uri);
        if (path.empty()) return;
        NeoCore::ModuleBundle bundle;
        NeoCore::Error error;
        NeoCore::ModuleLoader loader = [&](const std::string& name, const std::string& importer, NeoCore::ModuleSource& result, NeoCore::Error& failure)
        { return LoadModule(name, importer, "", result, failure, &overlays); };
        std::vector<std::string> external;
        if (host)
        {
            for (const auto& global:host->Schema().Globals()) external.push_back(global.name);
            for (const auto& type:host->Schema().Types().NamedTypes()) external.push_back(type.first);
        }
        std::string bundleSource=current->second, bundlePath=path;
        if (!implementationPath.empty())
        {
            const auto overlay=overlays.find(implementationPath);
            std::string implementationSource;
            if (overlay!=overlays.end()) implementationSource=overlay->second;
            else if (!ReadSource(implementationPath,implementationSource))
            {
                error.message="cannot read host implementation: "+implementationPath;
                error.sourceName=implementationPath;
            }
            const NeoCore::ModuleLoader fallback=loader;
            const std::string implementation=implementationPath, application=bundleSource;
            bundlePath+="#console-host";
            const std::string launcher=bundlePath;
            loader=[fallback,implementation,implementationSource,path,application,launcher](
                const std::string& name,const std::string& importer,NeoCore::ModuleSource& out,NeoCore::Error& failure){
                if(importer==launcher && name=="__console_host_impl"){out.sourceName=implementation;out.source=implementationSource;return true;}
                if(importer==launcher && name=="__console_host_app"){out.sourceName=path;out.source=application;return true;}
                return fallback(name,importer,out,failure);
            };
            bundleSource="import __console_host_impl;\nimport __console_host_app;\n";
        }
        if (error.ok() && NeoCore::BundleModules(bundleSource, bundlePath, loader, bundle, error,external,&defines,diagnosticLimit))
        {
            NeoCore::StaticCompileResult checked;
            const auto config=host?host->Config():std::shared_ptr<const NeoCore::ScriptHostCompileConfig>();
            const auto allowed=[&](int line){return config && line>0 && static_cast<std::size_t>(line)<=bundle.lines.size() && config->allowsSource(bundle.lines[static_cast<std::size_t>(line-1)].sourceName);};
            if (!NeoCore::CompileStaticSource(bundle.source, checked,host?&host->Schema():nullptr,nullptr,diagnosticLimit,config.get(),allowed))
            {
                error.sourceName = path; error.message = checked.errorMessage; error.line = checked.errorLine; error.column = checked.errorColumn;
                bundle.Translate(error);
                for (const auto& diagnostic:checked.diagnostics)
                {
                    NeoCore::Error mapped; mapped.message=diagnostic.message; mapped.sourceName=path; mapped.line=diagnostic.line; mapped.column=diagnostic.column;
                    bundle.Translate(mapped);
                    NeoCore::CompileDiagnostic value; value.message=mapped.message; value.sourceName=mapped.sourceName; value.line=mapped.line; value.column=mapped.column;
                    error.diagnostics.push_back(value);
                }
            }
        }
        std::set<std::string> affected;
        for (const auto& previous : diagnosticsByRoot[uri]) affected.insert(previous.first);
        diagnosticsByRoot[uri].clear();
        if (!error.ok())
        {
            if (error.diagnostics.empty())
            {
                NeoCore::CompileDiagnostic first; first.message=error.message; first.sourceName=error.sourceName; first.line=error.line; first.column=error.column;
                error.diagnostics.push_back(first);
            }
            for (const auto& diagnostic:error.diagnostics)
            {
            std::string target = PathUri(diagnostic.sourceName.empty() ? path : diagnostic.sourceName);
            std::string source;
            for (const auto& document : documents)
                if (UriPath(document.first) == diagnostic.sourceName) { target = document.first; source = document.second; break; }
            if (source.empty()) ReadSource(diagnostic.sourceName, source);
            if (diagnostic.sourceName == path) target = uri;
            affected.insert(target);
            const int line = std::max(0, diagnostic.line - 1);
            const int character = Utf16Column(source, diagnostic.line, diagnostic.column);
            std::ostringstream message;
            message << "{\"severity\":1,\"source\":\"NeoCore\",\"message\":\"" << JsonEscape(diagnostic.message)
                << "\",\"range\":{\"start\":{\"line\":" << line << ",\"character\":" << character
                << "},\"end\":{\"line\":" << line << ",\"character\":" << character << "}}}";
            diagnosticsByRoot[uri][target].push_back(message.str());
            }
        }
        else affected.insert(uri);
        for (const std::string& target : affected) PublishDiagnosticTarget(target);
    }

	void SendResponse(const std::string& id, const std::string& result)
	{
		std::ostringstream os;
		os << "{\"jsonrpc\":\"2.0\",\"id\":" << id << ",\"result\":" << result << "}";
		DapSendMessage(os.str());
	}

	std::string BuildCompletionResult(const std::string& uri, int line, int character)
	{
		std::string source;
		auto document = documents.find(uri);
		if (document != documents.end())
			source = document->second;

        const std::vector<BuiltinInfo> available = DocumentBuiltins(source);

		std::string module;
		std::string prefix;
		GetLspCompletionContext(GetLspLinePrefix(source, line, character), module, prefix);

		std::ostringstream os;
		os << "{\"isIncomplete\":false,\"items\":[";
		bool first = true;
		std::set<std::string> emitted;
		if (!module.empty())
		{
			for (const BuiltinInfo& info : available)
			{
				if (info.module == module && LspStartsWith(info.name, prefix))
					AppendLspCompletionItem(os, first, emitted, info.name, 3, BuildLspFunctionDetail(info));
			}

		}
		else
		{
			static const char* keywords[] = {
				"int", "float", "bool", "string", "void", "struct", "fn", "list", "map", "array", "array32", "set", "Vector2", "Vector3", "Vector4", "Quaternion",
                "TimeSpan", "Stopwatch", "AsyncRequest", "export", "if", "else", "switch", "case", "default", "while", "for", "foreach", "in", "continue", "break", "return",
				"true", "false", "null", "import", "public", "const", "yield", "sleep"
			};
			for (const char* keyword : keywords)
			{
				if (LspStartsWith(keyword, prefix))
					AppendLspCompletionItem(os, first, emitted, keyword, 14, "NeoCore keyword");
			}

			std::set<std::string> modules;
			for (const BuiltinInfo& info : available)
			{
				if (info.module.empty())
				{
					if (LspStartsWith(info.name, prefix))
						AppendLspCompletionItem(os, first, emitted, info.name, 3, BuildLspFunctionDetail(info));
				}
				else if (info.module == "math" || info.module == "system" || info.module == "coroutine")
					modules.insert(info.module);
			}
			for (const std::string& builtinModule : modules)
			{
				if (LspStartsWith(builtinModule, prefix))
					AppendLspCompletionItem(os, first, emitted, builtinModule, 9, "NeoCore module");
			}

            std::vector<NeoCore::StaticDeclaration> declarations;
            NeoCore::CollectStaticDeclarations(source, declarations);
            for (const NeoCore::StaticDeclaration& declaration : declarations)
                if (!declaration.function && LspStartsWith(declaration.name, prefix))
                    AppendLspCompletionItem(os, first, emitted, declaration.name, 6, declaration.type + " " + declaration.name);
		}
		os << "]}";
		return os.str();
	}

	std::string BuildSignatureHelpResult(const std::string& uri, int line, int character)
	{
		std::string source;
		auto document = documents.find(uri);
		if (document != documents.end())
			source = document->second;

		int activeParameter = 0;
        const std::vector<BuiltinInfo> available = DocumentBuiltins(source);
		const BuiltinInfo* info = FindLspSignature(available, GetLspLinePrefix(source, line, character, true), activeParameter);
		if (info == nullptr)
			return "null";

		std::ostringstream os;
		os << "{\"signatures\":[{\"label\":\"" << JsonEscape(BuildLspFunctionDetail(*info)) << "\",\"parameters\":[";
		for (size_t i = 0; i < info->params.size(); ++i)
		{
			if (i > 0)
				os << ",";
			os << "{\"label\":\"" << JsonEscape(info->params[i]) << "\"}";
		}
		os << "]}],\"activeSignature\":0,\"activeParameter\":" << activeParameter << "}";
		return os.str();
	}

    std::string BuildHoverResult(const std::string& uri, int line, int character)
    {
        const auto document = documents.find(uri);
        if (document == documents.end()) return "null";
        const std::string& source = document->second;
        std::string before = GetLspLinePrefix(source, line, character, true);
        std::size_t end = before.size();
        while (end < source.size() && IsNeoIdentifierChar(source[end])) before += source[end++];
        std::string owner, name;
        GetLspCompletionContext(before, owner, name);
        std::string detail;
        const std::vector<BuiltinInfo> available = DocumentBuiltins(source);
        for (const BuiltinInfo& info : available)
            if (info.module == owner && info.name == name) { detail = BuildLspFunctionDetail(info); break; }
        if (detail.empty() && owner.empty())
        {
            std::vector<NeoCore::StaticDeclaration> declarations;
            NeoCore::CollectStaticDeclarations(source, declarations);
            for (const NeoCore::StaticDeclaration& declaration : declarations)
                if (declaration.name == name && declaration.offset <= end) detail = declaration.type + " " + declaration.name;
        }
        if (detail.empty()) return "null";
        return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"```neocore\\n" + JsonEscape(detail) + "\\n```\"}}";
    }

    void SendError(const std::string& id, int code, const std::string& message)
    {
        DapSendMessage("{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"error\":{\"code\":" +
            std::to_string(code) + ",\"message\":\"" + JsonEscape(message) + "\"}}");
    }
    void Handle(const std::string& body)
    {
        ProtocolJson json(body);
        yyjson_val* root = json.Root();
        if (!root) { SendError("null", -32700, "Invalid JSON"); return; }
        yyjson_val* idValue = JsonMember(root, "id");
        const bool request = idValue != nullptr;
        const std::string id = JsonValueText(idValue);
        const std::string method = JsonString(root, "method");
        if (!yyjson_is_obj(root) || JsonString(root, "jsonrpc") != "2.0" || method.empty() ||
            (request && !yyjson_is_str(idValue) && !yyjson_is_int(idValue) && !yyjson_is_null(idValue)))
        { SendError("null", -32600, "Invalid request"); return; }
        yyjson_val* params = JsonMember(root, "params");
        yyjson_val* document = JsonMember(params, "textDocument");
        const std::string uri = JsonString(document, "uri");
        yyjson_val* position = JsonMember(params, "position");
        if (method == "initialize")
        {
            if (request) SendResponse(id, "{\"capabilities\":{\"positionEncoding\":\"utf-16\",\"textDocumentSync\":1,\"hoverProvider\":true,\"completionProvider\":{\"triggerCharacters\":[\".\"]},\"signatureHelpProvider\":{\"triggerCharacters\":[\"(\",\",\"]}}}");
        }
        else if (method == "textDocument/didOpen")
        {
            if (UriPath(uri).size() >= 3 && UriPath(uri).substr(UriPath(uri).size() - 3) == ".nc")
            {
                documents[uri] = JsonString(document, "text");
                for (const auto& open : documents) PublishDiagnostics(open.first);
            }
        }
        else if (method == "textDocument/didChange")
        {
            yyjson_val* changes = JsonMember(params, "contentChanges");
            for (size_t i = 0; i < yyjson_arr_size(changes); ++i)
            {
                yyjson_val* change = yyjson_arr_get(changes, i);
                if (documents.count(uri) && !JsonMember(change, "range")) documents[uri] = JsonString(change, "text");
            }
            for (const auto& open : documents) PublishDiagnostics(open.first);
        }
        else if (method == "textDocument/didClose")
        {
            documents.erase(uri);
            const auto previous = diagnosticsByRoot[uri];
            diagnosticsByRoot.erase(uri);
            for (const auto& target : previous) PublishDiagnosticTarget(target.first);
            for (const auto& open : documents) PublishDiagnostics(open.first);
        }
        else if (method == "textDocument/completion")
        {
            if (request) SendResponse(id, BuildCompletionResult(uri, JsonInt(position, "line"), JsonInt(position, "character")));
        }
        else if (method == "textDocument/signatureHelp")
        {
            if (request) SendResponse(id, BuildSignatureHelpResult(uri, JsonInt(position, "line"), JsonInt(position, "character")));
        }
        else if (method == "textDocument/hover")
        {
            if (request) SendResponse(id, BuildHoverResult(uri, JsonInt(position, "line"), JsonInt(position, "character")));
        }
        else if (method == "shutdown") { if (request) SendResponse(id, "null"); }
        else if (method == "exit") terminated = true;
        else if (request) SendError(id, -32601, "Method not found");
    }

};

int RunLanguageServerImpl(const std::string& hostSchema,const std::string& hostImplementation,const std::string& resourceRoot,std::size_t diagnosticLimit)
{
#ifdef _WIN32
	_setmode(_fileno(stdin), _O_BINARY);
	_setmode(_fileno(stdout), _O_BINARY);
#endif
	NeoLspSession session(hostSchema,hostImplementation,resourceRoot,diagnosticLimit);
	std::string body;
	while (!session.terminated && DapReadMessage(body))
		session.Handle(body);
	return 0;
}

}
int RunNeoCoreDebugAdapter() { return RunDebugAdapterImpl(); }
int RunNeoCoreLanguageServer(const std::string& hostSchema,const std::string& hostImplementation,const std::string& resourceRoot,std::size_t diagnosticLimit)
{
    try { return RunLanguageServerImpl(hostSchema,hostImplementation,resourceRoot,diagnosticLimit); }
    catch(const std::exception& error) { std::fprintf(stderr,"%s\n",error.what()); return 2; }
}
