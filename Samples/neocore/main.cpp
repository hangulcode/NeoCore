#include "NeoCore.h"
#include "NeoCoreCompiler.h"
#include "NeoCoreModules.h"
#include "SampleHost.h"
#include <chrono>
#include <thread>
#include <cstdlib>
#ifdef NEOCORE_CONSOLE_HTTP
#include "../console/HttpTransport.h"
#endif
#include <cstdio>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#endif

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

static int Execute(const std::string& mode, const std::string& argument, bool debug)
{
    const bool sample = mode == "--run";
    std::string path = argument;
    if (sample)
    {
        if (argument.find_first_of("/\\.") != std::string::npos)
        { std::fprintf(stderr, "sample name must be a simple name\n"); return 1; }
        path = "Samples/neocore/scripts/" + argument + ".nc";
    }
    path = Canonical(path);
    std::string source;
    if (!Read(path, source)) { std::fprintf(stderr, "cannot read file: %s\n", path.c_str()); return 1; }
    NeoCore::Error error;
    NeoCore::CompileOptions options;
    options.moduleLoader = LoadModuleSource;
#ifdef NEOCORE_CONSOLE_HTTP
    options.httpTransport = CreateConsoleHttpTransport();
#endif
    SampleHost host;
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
    std::unique_ptr<NeoCore::Program> program = sample
        ? NeoCore::Program::Compile(source, host.Bindings(), error, path, debug, options)
        : NeoCore::Program::Compile(source, error, path, debug, options);
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
        std::printf("sample %s passed\n", argument.c_str());
    }
    return 0;
}

static bool ValidMode(const std::string& mode)
{
    return mode == "--file" || mode == "--check" || mode == "--emit-backend" || mode == "--run";
}

static int Usage()
{
    std::fprintf(stderr, "Usage: neocore [--debug] --file|--check|--emit-backend <file.nc>\n       neocore [--debug] --run <sample> (repository working directory)\n");
    return 2;
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
    const bool debug = argc > 1 && Utf8(argv[1]) == "--debug";
    const int first = debug ? 2 : 1;
    if (argc != first + 2 || !ValidMode(Utf8(argv[first]))) return Usage();
    return Execute(Utf8(argv[first]), Utf8(argv[first + 1]), debug);
}
#else
int main(int argc, char** argv)
{
    const bool debug = argc > 1 && std::string(argv[1]) == "--debug";
    const int first = debug ? 2 : 1;
    if (argc != first + 2 || !ValidMode(argv[first])) return Usage();
    return Execute(argv[first], argv[first + 1], debug);
}
#endif
