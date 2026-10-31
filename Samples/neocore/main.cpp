#include "NeoCore.h"
#include "NeoCoreCompiler.h"
#include "NeoCoreModules.h"
#include "SampleHost.h"
#include "CompilerErrors.h"
#include "Benchmarks.h"
#include "ConsoleDiagnostics.h"
#include "ConsoleTooling.h"
#include <chrono>
#include <thread>
#include <cstdlib>
#include <iostream>
#ifdef NEOCORE_CONSOLE_HTTP
#include "HttpTransport.h"
#endif
#include <cstdio>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#ifdef _WIN32
static std::string Utf8(const wchar_t* input);
#endif

struct SampleEntry
{
    const char* name;
    const char* description;
    bool automatic;
};

// 기존 콘솔의 0~20번을 유지하고 새 예제는 뒤에 붙인다.
static const SampleEntry Samples[] = {
    {"hello", "Declarations, functions and loops", true},
    {"performance", "Full performance workload [manual benchmark]", false},
    {"callback", "C++ and script function calls", true},
    {"map_callback", "Host Vector3 property getter/setter", true},
    {"9_times", "Multiplication table", true},
    {"string", "UTF-8 and string operations", true},
    {"list", "Typed list operations", true},
    {"map", "Typed maps and sorting", true},
    {"contailer", "Nested collections and sets", true},
    {"slice_run", "Instruction-budget suspension", true},
    {"time_limit", "Bounded calls and sleep/resume", true},
    {"divide_by_zero", "Expected runtime error", true},
    {"delegate", "Function values and closures", true},
    {"coroutine", "Resume, yield and sleep", true},
    {"module", "Imports and separate compilation", true},
    {"http", "Offline HTTP transport checks", true},
    {"regression", "Language regression examples", true},
    {"literal_totype", "Literals and conversions", true},
    {"cycle", "Collection and closure lifetimes", true},
    {"regex", "Regular expressions", true},
    {"time", "UTC, TimeSpan and Stopwatch", true},
    {"fibonacci", "Recursive function calls", true},
    {"structs", "Struct values and copies", true},
    {"vectors", "Vector arithmetic and components", true},
    {"particles", "Particle correctness checks (untimed)", true},
    {"https", "HTTPS request [manual, live network]", false}
};
static const std::size_t SampleCount = sizeof(Samples) / sizeof(Samples[0]);

static void PrintError(const NeoCore::Error& error)
{
    std::fprintf(stderr, "%s:%d:%d: %s\n", error.sourceName.c_str(),
        error.line, error.column, error.message.c_str());
}

static FILE* Open(const std::string& path)
{
#ifdef _WIN32
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), static_cast<int>(path.size()), nullptr, 0);
    if (count <= 0) return nullptr;
    std::wstring wide(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), static_cast<int>(path.size()), &wide[0], count);
    FILE* file = nullptr;
    _wfopen_s(&file, wide.c_str(), L"rb");
    return file;
#else
    return std::fopen(path.c_str(), "rb");
#endif
}

static std::string Directory(const std::string& path)
{
    std::size_t root = !path.empty() && path[0] == '/' ? 1 : 0;
    if (path.size() >= 3 && path[1] == ':' && path[2] == '/') root = 3;
    if (path.compare(0, 2, "//") == 0)
    {
        const std::size_t server = path.find('/', 2);
        const std::size_t share = server == std::string::npos ? server : path.find('/', server + 1);
        root = share == std::string::npos ? path.size() : share;
    }
    const std::size_t end = path.find_last_of("/\\");
    if (end == std::string::npos) return ".";
    return path.substr(0, end < root ? root : end);
}

static bool Read(const std::string& path, std::string& source)
{
    FILE* file = Open(path);
    if (!file) return false;
    source.clear();
    char buffer[4096];
    std::size_t count;
    while ((count = std::fread(buffer, 1, sizeof(buffer), file)) != 0) source.append(buffer, count);
    const bool ok = std::ferror(file) == 0;
    std::fclose(file);
    return ok;
}

static std::string Canonical(const std::string& path)
{
#ifdef _WIN32
    FILE* file = Open(path);
    if (!file) return path;
    const HANDLE handle = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(file)));
    const DWORD size = GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED);
    std::vector<wchar_t> wide(static_cast<std::size_t>(size) + 1);
    const DWORD count = size ? GetFinalPathNameByHandleW(handle, wide.data(), size + 1, FILE_NAME_NORMALIZED) : 0;
    std::fclose(file);
    if (!count || count > size) return path;
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(count), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return path;
    std::string result(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(count), &result[0], bytes, nullptr, nullptr);
    for (char& ch : result) if (ch == '\\') ch = '/';
    if (result.compare(0, 8, "//?/UNC/") == 0) result = "//" + result.substr(8);
    else if (result.compare(0, 4, "//?/") == 0) result.erase(0, 4);
    // 같은 파일을 대소문자만 달리 import해도 초기화는 한 번만 수행한다.
    for (char& ch : result) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    return result;
#else
    char* resolved = realpath(path.c_str(), nullptr);
    if (!resolved) return path;
    const std::string result(resolved);
    std::free(resolved);
    return result;
#endif
}

static bool LoadModuleSource(const std::string& name, const std::string& importer,
    NeoCore::ModuleSource& result, NeoCore::Error& error)
{
    std::string directory = Directory(importer);
    for (unsigned level = 0; level < 12; ++level)
    {
        const std::string candidates[] = {directory + "/" + name + ".nc", directory + "/Lib/" + name + ".nc"};
        for (const std::string& path : candidates)
            if (Read(path, result.source)) { result.sourceName = Canonical(path); return true; }
        const std::string parent = Directory(directory);
        if (parent == directory) break;
        directory = parent;
    }
    error.message = "module not found: " + name;
    return false;
}

static std::string WorkingDirectory()
{
#ifdef _WIN32
    const DWORD size = GetCurrentDirectoryW(0, nullptr);
    std::vector<wchar_t> path(size ? size : 1);
    const DWORD count = GetCurrentDirectoryW(static_cast<DWORD>(path.size()), path.data());
    return count && count < path.size() ? Utf8(path.data()) : ".";
#else
    char* path = getcwd(nullptr, 0);
    if (!path) return ".";
    const std::string result(path);
    std::free(path);
    return result;
#endif
}

static std::string ExecutableName(const std::string& argument)
{
#ifdef _WIN32
    std::vector<wchar_t> path(32768);
    const DWORD count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (count && count < path.size()) return Utf8(path.data());
#endif
    return argument;
}

static std::string FindSamples(const std::string& executable)
{
    const std::string bases[] = {WorkingDirectory(), Directory(Canonical(ExecutableName(executable)))};
    for (std::string directory : bases)
    {
        for (char& ch : directory) if (ch == '\\') ch = '/';
        for (;;)
        {
            const std::string candidates[] = {directory + "/Samples/neocore/scripts", directory + "/scripts", directory};
            for (const std::string& candidate : candidates)
            {
                FILE* file = Open(candidate + "/hello.nc");
                if (file)
                {
                    std::fclose(file);
                    return Directory(Canonical(candidate + "/hello.nc"));
                }
            }
            const std::string parent = Directory(directory);
            if (parent == directory) break;
            directory = parent;
        }
    }
#ifdef NEOCORE_SAMPLE_DIR
    FILE* file = Open(NEOCORE_SAMPLE_DIR "/hello.nc");
    if (file)
    {
        std::fclose(file);
        return Directory(Canonical(NEOCORE_SAMPLE_DIR "/hello.nc"));
    }
#endif
    return std::string();
}

static const SampleEntry* FindSample(const std::string& choice)
{
    if (choice == "performace") return &Samples[1];
    for (const SampleEntry& sample : Samples)
        if (choice == sample.name) return &sample;
    if (choice.empty()) return nullptr;
    std::size_t number = 0;
    for (char ch : choice)
    {
        if (ch < '0' || ch > '9') return nullptr;
        number = number * 10 + static_cast<unsigned>(ch - '0');
        if (number >= SampleCount) return nullptr;
    }
    return &Samples[number];
}

struct ConsoleSettings
{
    bool debug;
    bool emitAsm;
    bool stats;
    ConsoleSettings() : debug(false), emitAsm(false), stats(false) {}
};

struct StatsOnExit
{
    bool enabled;
    explicit StatsOnExit(bool value) : enabled(value) {}
    ~StatsOnExit() { if (enabled) PrintAllocationStats(); }
};

static int Execute(const std::string& mode, const std::string& argument,
    const ConsoleSettings& settings, const std::string& sampleDirectory)
{
    // 프로그램과 호스트를 먼저 정리한 뒤 프로세스 전체 할당 상태를 출력한다.
    StatsOnExit stats(settings.stats);
    const bool sample = mode == "--run";
    std::string path = argument;
    if (sample)
    {
        if (argument.find_first_of("/\\.") != std::string::npos)
        { std::fprintf(stderr, "sample name must be a simple name\n"); return 1; }
        if (sampleDirectory.empty())
        { std::fprintf(stderr, "cannot locate sample scripts; run from the repository or its build directory\n"); return 1; }
        path = sampleDirectory + "/" + argument + ".nc";
    }
    path = Canonical(path);
    std::string source;
    if (!Read(path, source)) { std::fprintf(stderr, "cannot read file: %s\n", path.c_str()); return 1; }
    NeoCore::Error error;
    NeoCore::CompileOptions options;
    options.moduleLoader = LoadModuleSource;
    options.emitAsm = settings.emitAsm;
#ifdef NEOCORE_CONSOLE_HTTP
    options.httpTransport = CreateConsoleHttpTransport();
#endif
    SampleHost host(sample ? argument : std::string());
    if (sample && argument == "http") options.httpTransport = CreateSampleHttpTransport();
    if (mode == "--emit-backend")
    {
        NeoCore::ModuleBundle bundle;
        if (!NeoCore::BundleModules(source, path, options.moduleLoader, bundle, error)) { PrintError(error); return 1; }
        NeoCore::StaticCompileResult result;
        if (!NeoCore::CompileStaticSource(bundle.source, result))
        {
            error.sourceName = path; error.message = result.errorMessage;
            error.line = result.errorLine; error.column = result.errorColumn;
            bundle.Translate(error); PrintError(error); return 1;
        }
        if (std::fwrite(result.loweredSource.data(), 1, result.loweredSource.size(), stdout) != result.loweredSource.size() || std::fflush(stdout) != 0)
        { std::fprintf(stderr, "cannot write backend output\n"); return 1; }
        return 0;
    }
    const NeoCore::HostBindings* bindings = host.Bindings();
    std::unique_ptr<NeoCore::Program> program = bindings
        ? NeoCore::Program::Compile(source, *bindings, error, path, settings.debug, options)
        : NeoCore::Program::Compile(source, error, path, settings.debug, options);
    if (!program) { PrintError(error); return 1; }
    if (mode == "--check") { std::puts("Type check and backend compilation passed."); return 0; }
    NeoCore::Result result = program->Run();
    for (unsigned poll = 0; result.ok() && result.suspended() && poll < 30000; ++poll)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        result = program->Resume();
    }
    if (sample && argument == "divide_by_zero")
    {
        if (!result.ok() && result.error.message.find("division by zero") != std::string::npos)
        { std::puts("Expected division-by-zero error handled."); return 0; }
        std::fprintf(stderr, "expected division-by-zero error was not observed\n"); return 1;
    }
    if (!result.ok()) { PrintError(result.error); return 1; }
    if (result.suspended()) { std::fprintf(stderr, "script is still suspended after the console wait limit\n"); return 1; }
    if (sample)
    {
        if (!host.Verify(argument, *program, error)) { PrintError(error); return 1; }
        if (argument == "https") std::puts("\nsample https completed; see the request status above.");
        else std::printf("\nsample %s passed\n", argument.c_str());
    }
    return 0;
}

static bool ValidMode(const std::string& mode)
{
    return mode == "--file" || mode == "--check" || mode == "--emit-backend" || mode == "--run";
}

static void PrintSamples()
{
    std::puts("\nNeoCore sample console");
    for (std::size_t i = 0; i < SampleCount; ++i)
        std::printf("%2u. %-16s %s\n", static_cast<unsigned>(i), Samples[i].name, Samples[i].description);
    std::puts("\nEnter a sample number or name. Commands:");
    std::puts("  all / smoke       Run all offline functional samples");
    std::puts("  debug-smoke       Run functional and source-location checks with debug info");
    std::puts("  compiler-errors   Check typed compiler diagnostics");
    std::puts("  bench-check       Check benchmark results with small untimed inputs");
    std::puts("  pooltrim-check    Check memory-pool reclamation without timing");
    std::puts("  bench / pooltrim  Run manual benchmark / memory-pool diagnostics");
    std::puts("  list              Show this menu again");
    std::puts("  r                 Repeat the last sample");
    std::puts("  q / quit / exit   Exit");
}

static int RunSmoke(const ConsoleSettings& settings, const std::string& sampleDirectory)
{
    unsigned passed = 0, failed = 0;
    for (const SampleEntry& sample : Samples)
    {
        if (!sample.automatic) continue;
        std::printf("\n--- %s ---\n", sample.name);
        std::fflush(stdout);
        if (Execute("--run", sample.name, settings, sampleDirectory) == 0) ++passed;
        else { ++failed; std::printf("sample %s FAILED\n", sample.name); }
    }
    std::printf("\nFunctional samples: %u passed, %u failed.\n", passed, failed);
    std::puts("Skipped: performance (benchmark), https (live network).");
    return failed ? 1 : 0;
}

static int RunDebugSmoke(ConsoleSettings settings, const std::string& sampleDirectory)
{
    settings.debug = true;
    const int samples = RunSmoke(settings, sampleDirectory);
    NeoCore::Error error;
    auto program = NeoCore::Program::Compile("int zero = 0;\nprint(1 / zero);\n",
        error, "debug-smoke.nc", true);
    if (!program) { PrintError(error); return 1; }
    const NeoCore::Result result = program->Run();
    if (result.ok() || result.error.line != 2 || result.error.sourceName != "debug-smoke.nc")
    { std::fprintf(stderr, "Debug source-location check FAILED\n"); PrintError(result.error); return 1; }
    std::puts("Debug source-location check passed.");
    std::puts("DAP breakpoint and stepping checks run in the protocol regression suite.");
    return samples;
}

static int Usage(int status = 2)
{
    std::puts("Usage: neocore [--debug]                         Interactive sample menu");
    std::puts("       neocore --list                          List samples");
    std::puts("       neocore --run <name-or-number>           Run one sample");
    std::puts("       neocore --smoke | --run-all              Offline functional samples");
    std::puts("       neocore --debug-smoke                    Debug-info functional checks");
    std::puts("       neocore --compiler-error-regression     Typed compiler diagnostics");
    std::puts("       neocore --bench | --bench-check          Timed / untimed benchmarks");
    std::puts("       neocore --pooltrim                      Memory-pool diagnostics");
    std::puts("       neocore --pooltrim-check                Untimed memory-pool checks");
    std::puts("       neocore --file|--check|--emit-backend <file.nc>");
    std::puts("Options: --debug, --asm, --stats (before or after the command)");
    std::puts("       neocore --dap | --lsp                    Typed editor protocols (stdio)");
    std::puts("Not yet ported: --v2smoke, untyped --globals.");
    return status;
}

static int RunMenu(const ConsoleSettings& settings, const std::string& sampleDirectory)
{
    PrintSamples();
    std::string choice;
    const SampleEntry* previous = nullptr;
    for (;;)
    {
        std::printf("\nNeoCore> ");
        std::fflush(stdout);
        if (!std::getline(std::cin, choice)) { std::puts(""); return std::cin.bad() ? 1 : 0; }
        const std::size_t first = choice.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        choice = choice.substr(first, choice.find_last_not_of(" \t\r\n") - first + 1);
        if (choice == "q" || choice == "quit" || choice == "exit" || choice == "\x1b") return 0;
        if (choice == "list" || choice == "help" || choice == "?") { PrintSamples(); continue; }
        if (choice == "all" || choice == "smoke") { RunSmoke(settings, sampleDirectory); continue; }
        if (choice == "debug-smoke") { RunDebugSmoke(settings, sampleDirectory); continue; }
        if (choice == "compiler-errors") { RunCompilerErrorRegression(settings.debug); continue; }
        if (choice == "bench-check") { RunBenchmarkChecks(settings.debug); continue; }
        if (choice == "pooltrim-check") { RunPoolTrimChecks(settings.debug); continue; }
        if (choice == "bench") { RunBenchmarks(settings.debug); continue; }
        if (choice == "pooltrim") { RunPoolTrim(settings.debug); continue; }
        const SampleEntry* sample = choice == "r" ? previous : FindSample(choice);
        if (!sample)
        { std::fprintf(stderr, "Unknown selection: %s. Enter a sample number/name, list, all or q.\n", choice.c_str()); continue; }
        previous = sample;
        std::printf("\n--- %s ---\n", sample->name);
        if (Execute("--run", sample->name, settings, sampleDirectory) != 0)
            std::printf("sample %s FAILED\n", sample->name);
    }
}

static int ConsoleMain(const std::vector<std::string>& arguments)
{
    ConsoleSettings settings;
    std::vector<std::string> positional;
    for (std::size_t i = 1; i < arguments.size(); ++i)
    {
        const std::string& argument = arguments[i];
        if (argument == "--debug") settings.debug = true;
        else if (argument == "--asm") settings.emitAsm = true;
        else if (argument == "--stats") settings.stats = true;
        else if (argument == "--globals")
        { std::fprintf(stderr, "NeoCore requires HostSchema/HostBindings for typed globals; --globals name-only declarations are unsupported.\n"); return 2; }
        else positional.push_back(argument);
    }
    const std::string sampleDirectory = FindSamples(arguments[0]);
    if (positional.empty()) return RunMenu(settings, sampleDirectory);
    const std::string& mode = positional[0];
    if (positional.size() == 1)
    {
        if (mode == "--help" || mode == "-h") return Usage(0);
        if (mode == "--list") { PrintSamples(); return 0; }
        if (mode == "--smoke" || mode == "--run-all") return RunSmoke(settings, sampleDirectory);
        if (mode == "--debug-smoke") return RunDebugSmoke(settings, sampleDirectory);
        if (mode == "--compiler-error-regression") return RunCompilerErrorRegression(settings.debug);
        if (mode == "--bench") return RunBenchmarks(settings.debug);
        if (mode == "--bench-check") return RunBenchmarkChecks(settings.debug);
        if (mode == "--pooltrim") return RunPoolTrim(settings.debug);
        if (mode == "--pooltrim-check") return RunPoolTrimChecks(settings.debug);
        if (mode == "--dap") return RunNeoCoreDebugAdapter();
        if (mode == "--lsp") return RunNeoCoreLanguageServer();
        if (mode == "--v2smoke")
        {
            std::fprintf(stderr, "%s is not yet ported to NeoCore; the legacy NeoScript VM/debugger/language-service checks do not validate .nc.\n", mode.c_str());
            return 2;
        }
    }
    if (positional.size() != 2 || !ValidMode(mode)) return Usage();
    std::string argument = positional[1];
    if (mode == "--run")
    {
        const SampleEntry* sample = FindSample(argument);
        if (sample) argument = sample->name;
    }
    return Execute(mode, argument, settings, sampleDirectory);
}

#ifdef _WIN32
static std::string Utf8(const wchar_t* input)
{
    int size = WideCharToMultiByte(CP_UTF8, 0, input, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return std::string();
    std::vector<char> buffer(static_cast<std::size_t>(size));
    WideCharToMultiByte(CP_UTF8, 0, input, -1, buffer.data(), size, nullptr, nullptr);
    return std::string(buffer.data());
}

// Windows는 유니코드 인자와 파일 경로를 그대로 사용한다.
int wmain(int argc, wchar_t** argv)
{
    SetConsoleOutputCP(CP_UTF8);
    std::vector<std::string> arguments;
    for (int i = 0; i < argc; ++i) arguments.push_back(Utf8(argv[i]));
    return ConsoleMain(arguments);
}
#else
int main(int argc, char** argv)
{
    std::vector<std::string> arguments;
    for (int i = 0; i < argc; ++i) arguments.push_back(argv[i]);
    return ConsoleMain(arguments);
}
#endif
