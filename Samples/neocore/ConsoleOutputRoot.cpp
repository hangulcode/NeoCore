#include "ConsoleOutputRoot.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <winternl.h>
#ifdef _MSC_VER
// Windows SDK의 winternl.h는 FILE_INFORMATION_CLASS의 첫 항목만 공개한다.
extern "C" NTSYSAPI NTSTATUS NTAPI NtSetInformationFile(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, ULONG);
static const ULONG RenameInformation = 10; // FileRenameInformation
#else
static const FILE_INFORMATION_CLASS RenameInformation = FileRenameInformation;
#endif
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
const char tempPrefix[] = ".neocore-tmp-";

std::vector<std::string> OutputComponents(const std::string& path)
{
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (;;)
    {
        const std::size_t end = path.find_first_of("/\\", start);
        const std::string part = path.substr(start, end - start);
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ' ||
            part.find_first_of(":*?\"<>|") != std::string::npos ||
            std::any_of(part.begin(), part.end(), [](unsigned char c) { return c < 32; }))
            throw std::runtime_error("invalid relative output path");
        std::string folded = part;
        for (char& ch : folded) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
        if (folded.compare(0, sizeof(tempPrefix) - 1, tempPrefix) == 0)
            throw std::runtime_error("reserved temporary output name");
        parts.push_back(part);
        if (end == std::string::npos) return parts;
        start = end + 1;
    }
}

std::string TempName(unsigned long pid)
{
    static std::atomic<unsigned long> sequence(0);
    return tempPrefix + std::to_string(pid) + "-" + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
}

void FileError(const char* action, unsigned long code)
{
    throw std::runtime_error(std::string(action) + " (OS error " + std::to_string(code) + ")");
}

#ifdef _WIN32
using Handle = HANDLE;
void Close(Handle h) { CloseHandle(h); }

std::wstring Wide(const std::string& utf8)
{
    const int size = utf8.size() > INT_MAX ? 0 : MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (!size) throw std::runtime_error("invalid UTF-8 output path");
    std::wstring value(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), &value[0], size);
    return value;
}

// 구성요소 하나만 열린 부모 핸들에 상대적으로 연다. 중간 경로를 문자열로 다시 탐색하지 않는다.
DWORD OpenChild(HANDLE parent, const std::wstring& name, ACCESS_MASK access, ULONG share,
    ULONG disposition, ULONG options, HANDLE& handle)
{
    if (name.size() > USHRT_MAX / sizeof(wchar_t)) return ERROR_FILENAME_EXCED_RANGE;
    UNICODE_STRING child;
    child.Length = child.MaximumLength = static_cast<USHORT>(name.size() * sizeof(wchar_t));
    child.Buffer = const_cast<wchar_t*>(name.data());
    OBJECT_ATTRIBUTES attributes;
    InitializeObjectAttributes(&attributes, &child, OBJ_CASE_INSENSITIVE | OBJ_DONT_REPARSE, parent, nullptr);
    IO_STATUS_BLOCK io;
    const NTSTATUS status = NtCreateFile(&handle, access | SYNCHRONIZE, &attributes, &io, nullptr,
        FILE_ATTRIBUTE_NORMAL, share, disposition, options | FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT,
        nullptr, 0);
    return status < 0 ? RtlNtStatusToDosError(status) : ERROR_SUCCESS;
}

DWORD Attributes(HANDLE handle)
{
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(handle, &info)) FileError("cannot inspect output path", GetLastError());
    return info.dwFileAttributes;
}
#else
using Handle = int;
void Close(Handle fd) { close(fd); }
#endif

struct ScopedHandle
{
    Handle value;
    explicit ScopedHandle(Handle handle) : value(handle) {}
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;
    ScopedHandle(ScopedHandle&& other) noexcept : value(other.value) { other.value = Invalid(); }
    ~ScopedHandle() { if (value != Invalid()) Close(value); }
    static Handle Invalid()
    {
#ifdef _WIN32
        return INVALID_HANDLE_VALUE;
#else
        return -1;
#endif
    }
};

#ifdef _WIN32
Handle OpenDirectory(Handle parent, const std::string& name)
{
    HANDLE dir;
    DWORD err = OpenChild(parent, Wide(name), FILE_TRAVERSE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
        FILE_OPEN, FILE_DIRECTORY_FILE, dir);
    if (err) FileError("cannot open output directory", err);
    ScopedHandle guard(dir);
    if (Attributes(dir) & FILE_ATTRIBUTE_REPARSE_POINT) throw std::runtime_error("output directory is a reparse point");
    guard.value = INVALID_HANDLE_VALUE;
    return dir;
}

void WriteInDirectory(Handle dir, const std::string& leaf, const std::string& text)
{
    const std::wstring name = Wide(leaf);
    HANDLE existing;
    DWORD err = OpenChild(dir, name, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        FILE_OPEN, 0, existing);
    if (err == ERROR_SUCCESS)
    {
        ScopedHandle guard(existing);
        if (Attributes(existing) & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("output target must be a regular file");
    }
    else if (err != ERROR_FILE_NOT_FOUND) FileError("cannot inspect output target", err);

    const DWORD nameBytes = static_cast<DWORD>(name.size() * sizeof(wchar_t));
    std::vector<unsigned char> storage(sizeof(FILE_RENAME_INFO) + nameBytes);
    FILE_RENAME_INFO* renameInfo = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
    renameInfo->ReplaceIfExists = TRUE;
    renameInfo->RootDirectory = dir;
    renameInfo->FileNameLength = nameBytes;
    std::copy(name.begin(), name.end(), renameInfo->FileName);

    HANDLE temp;
    for (unsigned attempt = 0; ; ++attempt)
    {
        err = OpenChild(dir, Wide(TempName(GetCurrentProcessId())), GENERIC_WRITE | DELETE, 0,
            FILE_CREATE, FILE_NON_DIRECTORY_FILE, temp);
        if (!err) break;
        if ((err != ERROR_ALREADY_EXISTS && err != ERROR_FILE_EXISTS) || attempt >= 127)
            FileError("cannot create temporary output file", err);
    }
    ScopedHandle guard(temp);
    std::size_t offset = 0;
    while (offset < text.size())
    {
        const DWORD count = static_cast<DWORD>((std::min)(text.size() - offset, std::size_t(1024 * 1024)));
        DWORD written = 0;
        if (!WriteFile(temp, text.data() + offset, count, &written, nullptr)) { err = GetLastError(); break; }
        if (!written) { err = ERROR_WRITE_FAULT; break; }
        offset += written;
    }
    if (!err && !FlushFileBuffers(temp)) err = GetLastError();
    if (!err)
    {
        // Win32 래퍼의 경로 변환을 거치지 않고, 임시 파일과 같은 부모 핸들에서 이름만 교체한다.
        IO_STATUS_BLOCK io;
        const NTSTATUS status = NtSetInformationFile(temp, &io, renameInfo, static_cast<ULONG>(storage.size()), RenameInformation);
        if (status < 0) err = RtlNtStatusToDosError(status);
    }
    if (err)
    {
        FILE_DISPOSITION_INFO discard = {TRUE};
        SetFileInformationByHandle(temp, FileDispositionInfo, &discard, sizeof(discard));
        FileError("cannot save output file", err);
    }
}
#else
Handle OpenDirectory(Handle parent, const std::string& name)
{
    const int fd = openat(parent, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) FileError("cannot open output directory", errno);
    return fd;
}

void WriteInDirectory(Handle dir, const std::string& leaf, const std::string& text)
{
    struct stat info;
    if (fstatat(dir, leaf.c_str(), &info, AT_SYMLINK_NOFOLLOW) == 0)
    {
        if (!S_ISREG(info.st_mode)) throw std::runtime_error("output target must be a regular file");
    }
    else if (errno != ENOENT) FileError("cannot inspect output target", errno);

    std::string temp;
    int fd;
    for (unsigned attempt = 0; ; ++attempt)
    {
        temp = TempName(static_cast<unsigned long>(getpid()));
        fd = openat(dir, temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd >= 0) break;
        if (errno != EEXIST || attempt >= 127) FileError("cannot create temporary output file", errno);
    }
    int err = 0;
    std::size_t offset = 0;
    while (offset < text.size())
    {
        const ssize_t written = write(fd, text.data() + offset, std::min(text.size() - offset, std::size_t(1024 * 1024)));
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) { err = written ? errno : EIO; break; }
        offset += static_cast<std::size_t>(written);
    }
    if (!err && fsync(fd) != 0) err = errno;
    if (close(fd) != 0 && !err) err = errno;
    if (!err && renameat(dir, temp.c_str(), dir, leaf.c_str()) != 0) err = errno;
    if (err)
    {
        unlinkat(dir, temp.c_str(), 0);
        FileError("cannot save output file", err);
    }
}
#endif
}

ConsoleOutputRoot::ConsoleOutputRoot(const std::string& directory)
{
#ifdef _WIN32
    directory_ = CreateFileW(Wide(directory).c_str(), FILE_TRAVERSE | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (directory_ == INVALID_HANDLE_VALUE) FileError("cannot open --output-root", GetLastError());
    ScopedHandle guard(directory_);
    if (GetFileType(directory_) != FILE_TYPE_DISK || !(Attributes(directory_) & FILE_ATTRIBUTE_DIRECTORY))
        throw std::runtime_error("--output-root must be a directory");
    guard.value = INVALID_HANDLE_VALUE;
#else
    directory_ = open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory_ < 0) FileError("cannot open --output-root", errno);
#endif
}

ConsoleOutputRoot::~ConsoleOutputRoot() { Close(directory_); }

bool ConsoleOutputRoot::WriteText(const std::string& path, const std::string& text, std::string& error) const
{
    try
    {
        const std::vector<std::string> parts = OutputComponents(path);
        Handle parent = directory_;
        std::vector<ScopedHandle> directories;
        directories.reserve(parts.size() - 1);
        for (std::size_t i = 0; i + 1 < parts.size(); ++i)
        {
            parent = OpenDirectory(parent, parts[i]);
            directories.emplace_back(parent);
        }
        WriteInDirectory(parent, parts.back(), text);
        error.clear();
        return true;
    }
    catch (const std::runtime_error& failure)
    {
        error = failure.what();
        return false;
    }
}
