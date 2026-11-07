#if defined(_WIN32)
#include <cstdlib>
#include <crtdbg.h>
#include <windows.h>

namespace
{
// 자동 회귀 시험은 실패를 로그와 종료 코드로 남기고 데스크톱 대화상자를 띄우지 않는다.
struct NoninteractiveTests
{
    NoninteractiveTests()
    {
#if defined(NEOCORE_TEST_CONSOLE)
        char enabled[2];
        if (!GetEnvironmentVariableA("NEOCORE_TEST_NONINTERACTIVE", enabled, sizeof(enabled))) return;
#endif
        _set_error_mode(_OUT_TO_STDERR);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#if defined(_DEBUG)
        _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
        _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
        _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    }
} noninteractiveTests;
}
#endif
