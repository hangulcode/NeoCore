#pragma once

#include <cstdio>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace NeoCore
{
namespace TextFile
{
inline FILE* Open(const std::string& path, bool write)
{
#ifdef _WIN32
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), static_cast<int>(path.size()), nullptr, 0);
    if (count <= 0) return nullptr;
    std::vector<wchar_t> wide(static_cast<std::size_t>(count) + 1, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), static_cast<int>(path.size()), wide.data(), count);
    FILE* file = nullptr;
    _wfopen_s(&file, wide.data(), write ? L"wb" : L"rb");
    return file;
#else
    return std::fopen(path.c_str(), write ? "wb" : "rb");
#endif
}
inline bool Read(const std::string& path, std::string& text, std::string& error)
{
    error.clear();
    FILE* file = Open(path, false);
    if (!file) { error = "cannot read text file: " + path; return false; }
    std::string result;
    char buffer[8192];
    try
    {
        std::size_t count;
        while ((count = std::fread(buffer, 1, sizeof(buffer), file)) != 0) result.append(buffer, count);
    }
    catch (...) { std::fclose(file); throw; }
    const bool ok = std::ferror(file) == 0;
    std::fclose(file);
    if (!ok) { error = "cannot finish reading text file: " + path; return false; }
    if (result.compare(0, 3, "\xef\xbb\xbf") == 0) result.erase(0, 3);
    text.swap(result);
    return true;
}
inline bool Write(const std::string& path, const std::string& text, std::string& error)
{
    error.clear();
    FILE* file = Open(path, true);
    if (!file) { error = "cannot write text file: " + path; return false; }
    const bool ok = std::fwrite(text.data(), 1, text.size(), file) == text.size();
    const bool closed = std::fclose(file) == 0;
    if (!ok || !closed) { error = "cannot finish writing text file: " + path; return false; }
    return true;
}
}
}
