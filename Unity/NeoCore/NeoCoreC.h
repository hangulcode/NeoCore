#ifndef NEOCORE_C_H
#define NEOCORE_C_H

#include <stdint.h>

#if defined(NEOCORE_C_STATIC)
# define NC_API
#elif defined(_WIN32)
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
typedef struct NcValue NcValue;
typedef struct NcBytes NcBytes;
typedef struct NcOptions NcOptions;
typedef struct NcHostCall NcHostCall;
typedef struct NcModuleOutput NcModuleOutput;
typedef struct NcHttpRequest NcHttpRequest;
typedef struct NcHttpResponse NcHttpResponse;

typedef void (*NcRelease)(void* userData);
typedef void (*NcLogCallback)(void* userData, int32_t error, const char* text, uint32_t length);
NC_API int32_t NcSetLogHandler(NcLogCallback callback, void* userData, NcRelease release);
typedef struct NcField { const char* name; uint32_t length; uint32_t type; } NcField;
typedef void* (*NcObjectResolver)(void* userData, uint32_t type, uint64_t object);
typedef int32_t (*NcHostCallback)(void* userData, NcHostCall* call);
typedef int32_t (*NcModuleLoader)(void* userData, const char* module, uint32_t moduleLength,
    const char* importer, uint32_t importerLength, NcModuleOutput* output);
typedef void (*NcHttpTransport)(void* userData, const NcHttpRequest* request, NcHttpResponse* response);

enum NcStatus { NC_COMPLETED = 0, NC_SUSPENDED = 1, NC_FAILED = 2 };
enum NcDefineKind { NC_DEFINE_IDENTIFIER=0, NC_DEFINE_INT=1, NC_DEFINE_FLOAT=2, NC_DEFINE_STRING=3, NC_DEFINE_BOOL=4, NC_DEFINE_NULL=5 };
#define NEOCORE_C_ABI_VERSION 1
NC_API uint32_t NcGetAbiVersion(void);
enum NcKind { NC_VOID = 0, NC_INT = 1, NC_FLOAT = 2, NC_BOOL = 3, NC_STRING = 4, NC_NULL = 5,
    NC_VECTOR2 = 6, NC_VECTOR3 = 7, NC_VECTOR4 = 8, NC_QUATERNION = 9, NC_OBJECT = 10,
    NC_LIST = 11, NC_MAP = 12, NC_SET = 13, NC_ARRAY = 14, NC_FUNCTION = 15, NC_STRUCT = 16, NC_ARRAY32 = 17 };

NC_API NcProgram* NcCompile(const char* source, uint32_t sourceLength,
    const char* sourceName, uint32_t nameLength, int32_t debug, NcResult** result);
NC_API NcProgram* NcCompileWithOptions(const char* source, uint32_t sourceLength,
    const char* sourceName, uint32_t nameLength, int32_t debug, const NcOptions* options, NcResult** result);
NC_API void NcProgramDestroy(NcProgram* program);
NC_API int64_t NcProgramTrimMemory(NcProgram* program, int32_t force);
NC_API int64_t NcProgramCollectCycles(NcProgram* program, int32_t force);
NC_API int64_t NcAllocationStat(int32_t index);
NC_API void NcProgramSetEmptyPageHoldSeconds(NcProgram* program, float seconds);
NC_API void NcProgramSetTrimPagesPerCall(NcProgram* program, int32_t pages);
NC_API NcInstance* NcInstanceCreate(NcProgram* program, NcResult** result);
NC_API void NcInstanceDestroy(NcInstance* instance);
NC_API int32_t NcInstanceTryDestroy(NcInstance* instance);
NC_API NcResult* NcRun(NcInstance* instance);
NC_API NcResult* NcCall(NcInstance* instance, const char* name, uint32_t nameLength,
    const NcArguments* arguments);
NC_API NcResult* NcResume(NcInstance* instance, double deltaTime);
NC_API NcResult* NcCallLimited(NcInstance* instance, const char* name, uint32_t nameLength,
    const NcArguments* arguments, int32_t timeoutMs, uint32_t instructionBudget);
NC_API NcResult* NcResumeBudget(NcInstance* instance, double deltaTime, uint32_t instructionBudget);
NC_API NcResult* NcCancel(NcInstance* instance);
NC_API NcResult* NcReset(NcInstance* instance);
NC_API int32_t NcIsSuspended(const NcInstance* instance);
NC_API int32_t NcIsRunning(const NcInstance* instance);
NC_API uint64_t NcInstanceId(const NcInstance* instance);
NC_API NcResult* NcInstanceSetHostObject(NcInstance* instance, const char* name, uint32_t length, uint64_t id);

NC_API NcArguments* NcArgumentsCreate(void);
NC_API void NcArgumentsDestroy(NcArguments* arguments);
NC_API int32_t NcArgumentsInt(NcArguments* arguments, int64_t value);
NC_API int32_t NcArgumentsFloat(NcArguments* arguments, double value);
NC_API int32_t NcArgumentsBool(NcArguments* arguments, int32_t value);
NC_API int32_t NcArgumentsString(NcArguments* arguments, const char* value, uint32_t length);
NC_API int32_t NcArgumentsNull(NcArguments* arguments);
NC_API int32_t NcArgumentsVector(NcArguments* arguments, int32_t kind, const float* components);
NC_API int32_t NcArgumentsObject(NcArguments* arguments, const char* typeName, uint32_t length, uint64_t id);
NC_API int32_t NcArgumentsValue(NcArguments* arguments, const NcValue* value);

NC_API void NcResultDestroy(NcResult* result);
NC_API int32_t NcResultStatus(const NcResult* result);
NC_API int32_t NcResultKind(const NcResult* result);
NC_API int64_t NcResultInt(const NcResult* result);
NC_API double NcResultFloat(const NcResult* result);
NC_API int32_t NcResultBool(const NcResult* result);
NC_API int32_t NcResultVector(const NcResult* result, float* components, uint32_t capacity);
NC_API uint64_t NcResultObject(const NcResult* result);
NC_API const char* NcResultObjectType(const NcResult* result, uint32_t* length);
NC_API const char* NcResultString(const NcResult* result, uint32_t* length);
NC_API const char* NcResultError(const NcResult* result, uint32_t* length);
NC_API const char* NcResultSource(const NcResult* result, uint32_t* length);
NC_API int32_t NcResultLine(const NcResult* result);
NC_API int32_t NcResultColumn(const NcResult* result);
NC_API NcValue* NcResultValue(const NcResult* result);
NC_API NcValue* NcArgumentsValueAt(const NcArguments* arguments, uint32_t index);
NC_API void NcValueDestroy(NcValue* value);
NC_API NcValue* NcValueClone(const NcValue* value);
NC_API int32_t NcValueKind(const NcValue* value);
NC_API const char* NcValueType(const NcValue* value, uint32_t* length);
NC_API int64_t NcValueInt(const NcValue* value);
NC_API double NcValueFloat(const NcValue* value);
NC_API int32_t NcValueBool(const NcValue* value);
NC_API const char* NcValueString(const NcValue* value, uint32_t* length);
NC_API int32_t NcValueVector(const NcValue* value, float* components, uint32_t capacity);
NC_API uint64_t NcValueObject(const NcValue* value);
NC_API uint64_t NcValueCount(const NcValue* value);
NC_API int32_t NcValueSameStorage(const NcValue* first, const NcValue* second);
NC_API NcResult* NcValueCreate(NcProgram* program, const char* typeName, uint32_t length);
NC_API NcResult* NcValueCreateFields(NcProgram* program, const char* typeName, uint32_t length, const NcArguments* fields);
NC_API NcResult* NcFindFunction(NcProgram* program, const char* name, uint32_t length);
NC_API NcResult* NcParseJson(NcProgram* program, const char* typeName, uint32_t typeLength, const char* json, uint32_t jsonLength, int32_t strictIntegers);
NC_API NcResult* NcToJson(NcProgram* program, const NcValue* value, int32_t sortKeys);
NC_API NcResult* NcValueAt(const NcValue* value, uint64_t index);
NC_API NcResult* NcValueKeyAt(const NcValue* value, uint64_t index);
NC_API NcResult* NcValueGet(const NcValue* value, const NcValue* key);
NC_API NcResult* NcValueContains(const NcValue* value, const NcValue* key);
NC_API NcResult* NcValueSet(const NcValue* value, uint64_t index, const NcValue* element);
NC_API NcResult* NcValuePut(const NcValue* value, const NcValue* key, const NcValue* element);
NC_API NcResult* NcValueAppend(const NcValue* value, const NcValue* element);
NC_API NcResult* NcValueRemove(const NcValue* value, const NcValue* key);
NC_API NcResult* NcValueResize(const NcValue* value, uint64_t count);
NC_API NcResult* NcValueReserve(const NcValue* value, uint64_t count);
NC_API NcResult* NcValueRemoveAt(const NcValue* value, uint64_t index);
NC_API NcResult* NcArrayReadInt64(const NcValue* value, uint64_t offset, int64_t* output, uint64_t count);
NC_API NcResult* NcArrayReadFloat64(const NcValue* value, uint64_t offset, double* output, uint64_t count);
NC_API NcResult* NcArrayReadInt32(const NcValue* value, uint64_t offset, int32_t* output, uint64_t count);
NC_API NcResult* NcArrayReadFloat32(const NcValue* value, uint64_t offset, float* output, uint64_t count);
NC_API NcResult* NcArrayWriteInt64(const NcValue* value, uint64_t offset, const int64_t* input, uint64_t count);
NC_API NcResult* NcArrayWriteFloat64(const NcValue* value, uint64_t offset, const double* input, uint64_t count);
NC_API NcResult* NcArrayWriteInt32(const NcValue* value, uint64_t offset, const int32_t* input, uint64_t count);
NC_API NcResult* NcArrayWriteFloat32(const NcValue* value, uint64_t offset, const float* input, uint64_t count);
NC_API NcResult* NcValueClear(const NcValue* value);
NC_API NcResult* NcCallFunction(NcInstance* instance, const NcValue* function, const NcArguments* arguments,
    int32_t timeoutMs, uint32_t instructionBudget);
NC_API NcResult* NcReadGlobal(NcInstance* instance, const char* name, uint32_t length);
NC_API NcResult* NcWriteGlobal(NcInstance* instance, const char* name, uint32_t length, const NcValue* value);
NC_API NcBytes* NcSaveImage(NcProgram* program, NcResult** result);
NC_API NcProgram* NcLoadImage(const uint8_t* bytes, uint64_t length, const NcOptions* options, NcResult** result);
NC_API void NcBytesDestroy(NcBytes* bytes);
NC_API const uint8_t* NcBytesData(const NcBytes* bytes, uint64_t* length);

NC_API NcOptions* NcOptionsCreate(void);
NC_API void NcOptionsDestroy(NcOptions* options);
NC_API int32_t NcOptionsDefine(NcOptions* options, const char* name, uint32_t nameLength, int32_t kind, const char* text, uint32_t textLength);
NC_API int32_t NcOptionsSetResolver(NcOptions* options, NcObjectResolver resolver, void* userData, NcRelease release);
NC_API int32_t NcOptionsSetModuleLoader(NcOptions* options, NcModuleLoader loader, void* userData, NcRelease release);
NC_API int32_t NcOptionsSetHttpTransport(NcOptions* options, NcHttpTransport transport, void* userData, NcRelease release);
NC_API uint32_t NcTypeBuiltin(int32_t kind);
NC_API uint32_t NcTypeNullable(NcOptions* options, uint32_t element);
NC_API uint32_t NcTypeCollection(NcOptions* options, int32_t kind, uint32_t element, uint32_t key, uint32_t value);
NC_API uint32_t NcTypeFunction(NcOptions* options, uint32_t resultType, const uint32_t* parameters, uint32_t count);
NC_API uint32_t NcSchemaObject(NcOptions* options, const char* name, uint32_t length);
NC_API uint32_t NcSchemaStruct(NcOptions* options, const char* name, uint32_t length);
NC_API int32_t NcSchemaStructFields(NcOptions* options, uint32_t type, const NcField* fields, uint32_t count);
NC_API uint32_t NcSchemaMethod(NcOptions* options, uint32_t owner, const char* name, uint32_t length,
    uint32_t resultType, const uint32_t* parameters, uint32_t count,
    NcHostCallback callback, void* userData, NcRelease release);
NC_API uint32_t NcSchemaEvent(NcOptions* options, uint32_t owner, const char* eventName, uint32_t eventLength,
    uint32_t callbackType, const char* registrationMethod, uint32_t methodLength,
    NcHostCallback callback, void* userData, NcRelease release);
NC_API int32_t NcSchemaProperty(NcOptions* options, uint32_t owner, const char* name, uint32_t length,
    uint32_t type, NcHostCallback getter, NcHostCallback setter, void* userData, NcRelease release);
NC_API int32_t NcSchemaGlobal(NcOptions* options, const char* name, uint32_t length, uint32_t type, uint64_t id);
NC_API const char* NcOptionsError(const NcOptions* options, uint32_t* length);
NC_API uint32_t NcHostArgumentCount(const NcHostCall* call);
NC_API int32_t NcHostIsNull(const NcHostCall* call, uint32_t index);
NC_API NcValue* NcHostArgument(NcHostCall* call, uint32_t index);
NC_API void NcHostReturnValue(NcHostCall* call, const NcValue* value);
NC_API NcResult* NcHostCallFunction(NcHostCall* call, const NcValue* function, const NcArguments* arguments);
NC_API int64_t NcHostInt(NcHostCall* call, uint32_t index);
NC_API double NcHostFloat(NcHostCall* call, uint32_t index);
NC_API int32_t NcHostBool(NcHostCall* call, uint32_t index);
NC_API const char* NcHostString(NcHostCall* call, uint32_t index, uint32_t* length);
NC_API int32_t NcHostVector(NcHostCall* call, uint32_t index, float* output, uint32_t capacity);
NC_API uint64_t NcHostObject(NcHostCall* call, uint32_t index);
NC_API uint64_t NcHostReceiverId(const NcHostCall* call);
NC_API uint64_t NcHostCallerInstanceId(const NcHostCall* call);
NC_API void* NcHostResolveReceiver(NcHostCall* call);
NC_API void* NcHostResolveObject(NcHostCall* call, uint32_t index);
NC_API void NcHostReturnInt(NcHostCall* call, int64_t value);
NC_API void NcHostReturnFloat(NcHostCall* call, double value);
NC_API void NcHostReturnBool(NcHostCall* call, int32_t value);
NC_API void NcHostReturnString(NcHostCall* call, const char* value, uint32_t length);
NC_API void NcHostReturnVector(NcHostCall* call, int32_t kind, const float* components);
NC_API void NcHostReturnObject(NcHostCall* call, uint64_t id);
NC_API void NcHostReturnNull(NcHostCall* call);
NC_API void NcHostFail(NcHostCall* call, const char* error, uint32_t length);
NC_API NcResult* NcHostCallScript(NcHostCall* call, const char* name, uint32_t length, const NcArguments* arguments);
NC_API void NcModuleSource(NcModuleOutput* output, const char* source, uint32_t length, const char* name, uint32_t nameLength);
NC_API void NcModuleError(NcModuleOutput* output, const char* error, uint32_t length);
NC_API const char* NcHttpMethod(const NcHttpRequest* request, uint32_t* length);
NC_API const char* NcHttpUrl(const NcHttpRequest* request, uint32_t* length);
NC_API const char* NcHttpBody(const NcHttpRequest* request, uint32_t* length);
NC_API uint32_t NcHttpHeaderCount(const NcHttpRequest* request);
NC_API const char* NcHttpHeaderName(const NcHttpRequest* request, uint32_t index, uint32_t* length);
NC_API const char* NcHttpHeaderValue(const NcHttpRequest* request, uint32_t index, uint32_t* length);
NC_API int32_t NcHttpTimeout(const NcHttpRequest* request);
NC_API int32_t NcHttpIsCancelled(const NcHttpRequest* request);
NC_API void NcHttpComplete(NcHttpResponse* response, int32_t success, const char* body, uint32_t length,
    const char* error, uint32_t errorLength);

#ifdef __cplusplus
}
#endif
#endif
