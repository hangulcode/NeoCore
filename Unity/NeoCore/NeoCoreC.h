#ifndef NEOCORE_C_H
#define NEOCORE_C_H

#include <stdint.h>

#if defined(_WIN32)
# if defined(NEOCORE_C_BUILD)
#  define NC_API __declspec(dllexport)
# else
#  define NC_API __declspec(dllimport)
# endif
#else
# define NC_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NcProgram NcProgram;
typedef struct NcInstance NcInstance;
typedef struct NcArguments NcArguments;
typedef struct NcResult NcResult;

enum NcStatus { NC_COMPLETED = 0, NC_SUSPENDED = 1, NC_FAILED = 2 };
enum NcKind { NC_VOID = 0, NC_INT = 1, NC_FLOAT = 2, NC_BOOL = 3, NC_STRING = 4, NC_NULL = 5 };

NC_API NcProgram* NcCompile(const char* source, uint32_t sourceLength,
    const char* sourceName, uint32_t nameLength, int32_t debug, NcResult** result);
NC_API void NcProgramDestroy(NcProgram* program);
NC_API NcInstance* NcInstanceCreate(NcProgram* program, NcResult** result);
NC_API void NcInstanceDestroy(NcInstance* instance);
NC_API NcResult* NcRun(NcInstance* instance);
NC_API NcResult* NcCall(NcInstance* instance, const char* name, uint32_t nameLength,
    const NcArguments* arguments);
NC_API NcResult* NcResume(NcInstance* instance, double deltaTime);

NC_API NcArguments* NcArgumentsCreate(void);
NC_API void NcArgumentsDestroy(NcArguments* arguments);
NC_API int32_t NcArgumentsInt(NcArguments* arguments, int64_t value);
NC_API int32_t NcArgumentsFloat(NcArguments* arguments, double value);
NC_API int32_t NcArgumentsBool(NcArguments* arguments, int32_t value);
NC_API int32_t NcArgumentsString(NcArguments* arguments, const char* value, uint32_t length);
NC_API int32_t NcArgumentsNull(NcArguments* arguments);

NC_API void NcResultDestroy(NcResult* result);
NC_API int32_t NcResultStatus(const NcResult* result);
NC_API int32_t NcResultKind(const NcResult* result);
NC_API int64_t NcResultInt(const NcResult* result);
NC_API double NcResultFloat(const NcResult* result);
NC_API int32_t NcResultBool(const NcResult* result);
NC_API const char* NcResultString(const NcResult* result, uint32_t* length);
NC_API const char* NcResultError(const NcResult* result, uint32_t* length);
NC_API const char* NcResultSource(const NcResult* result, uint32_t* length);
NC_API int32_t NcResultLine(const NcResult* result);
NC_API int32_t NcResultColumn(const NcResult* result);

#ifdef __cplusplus
}
#endif
#endif
