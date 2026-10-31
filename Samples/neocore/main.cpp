#include "NeoCore.h"
#include "NeoCoreCompiler.h"
#include <cstdio>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

static void PrintError(const NeoCore::Error& error)
{
    std::fprintf(stderr, "%s:%d:%d: %s\n", error.sourceName.c_str(),
        error.line, error.column, error.message.c_str());
}

static int Execute(const std::string& mode, const std::string& path, FILE* input, bool debug)
{
    if (!input) { std::fprintf(stderr, "cannot open file: %s\n", path.c_str()); return 1; }
    std::string source;
    char buffer[4096];
    std::size_t count;
    while ((count = std::fread(buffer, 1, sizeof(buffer), input)) != 0) source.append(buffer, count);
    const bool failed = std::ferror(input) != 0;
    std::fclose(input);
    if (failed) { std::fprintf(stderr, "cannot read file: %s\n", path.c_str()); return 1; }
    NeoCore::Error error;
    if (mode == "--emit-backend")
    {
        NeoCore::StaticCompileResult result;
        if (!NeoCore::CompileStaticSource(source, result))
        {
            error.sourceName = path; error.message = result.errorMessage;
            error.line = result.errorLine; error.column = result.errorColumn;
            PrintError(error);
            return 1;
        }
        if (std::fwrite(result.loweredSource.data(), 1, result.loweredSource.size(), stdout) != result.loweredSource.size() ||
            std::fflush(stdout) != 0)
        {
            std::fprintf(stderr, "cannot write backend output\n");
            return 1;
        }
        return 0;
    }
    std::unique_ptr<NeoCore::Program> program = NeoCore::Program::Compile(source, error, path, debug);
    if (!program) { PrintError(error); return 1; }
    if (mode == "--check") { std::puts("Type check and backend compilation passed."); return 0; }
    NeoCore::Result result = program->Run();
    if (!result.ok()) { PrintError(result.error); return 1; }
    return 0;
}

static bool ValidMode(const std::string& mode)
{
    return mode == "--file" || mode == "--check" || mode == "--emit-backend";
}

static int Usage()
{
    std::fprintf(stderr, "Usage: neocore [--debug] --file|--check|--emit-backend <file.nc>\n");
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
    FILE* input = nullptr;
    _wfopen_s(&input, argv[first + 1], L"rb");
    return Execute(Utf8(argv[first]), Utf8(argv[first + 1]), input, debug);
}
#else
int main(int argc, char** argv)
{
    const bool debug = argc > 1 && std::string(argv[1]) == "--debug";
    const int first = debug ? 2 : 1;
    if (argc != first + 2 || !ValidMode(argv[first])) return Usage();
    return Execute(argv[first], argv[first + 1], std::fopen(argv[first + 1], "rb"), debug);
}
#endif
