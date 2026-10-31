#include "NeoCoreC.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void require(int condition, const char* message)
{
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}

static void status(NcResult* result, int expected)
{
    if (!result || NcResultStatus(result) != expected)
    {
        uint32_t length = 0;
        const char* error = NcResultError(result, &length);
        fprintf(stderr, "Unexpected execution status: %.*s\n", (int)length, error);
        exit(1);
    }
}

static NcResult* call(NcInstance* instance, const char* name, const NcArguments* arguments)
{
    return NcCall(instance, name, (uint32_t)strlen(name), arguments);
}

static double read_float(NcInstance* instance, const char* name)
{
    NcResult* result = call(instance, name, NULL);
    double value;
    status(result, NC_COMPLETED);
    require(NcResultKind(result) == NC_FLOAT, "float return kind");
    value = NcResultFloat(result);
    NcResultDestroy(result);
    return value;
}

static char* read_file(const char* path, uint32_t* length)
{
    FILE* file = fopen(path, "rb");
    long size;
    char* source;
    require(file != NULL, "open demo.nc");
    require(fseek(file, 0, SEEK_END) == 0, "seek sample");
    size = ftell(file);
    require(size >= 0 && (unsigned long)size < UINT32_MAX, "sample length");
    rewind(file);
    source = (char*)malloc((size_t)size + 1);
    require(source != NULL, "allocate sample");
    require(fread(source, 1, (size_t)size, file) == (size_t)size, "read sample");
    fclose(file);
    source[size] = 0;
    *length = (uint32_t)size;
    return source;
}

static void demo(const char* path)
{
    uint32_t length;
    char* source = read_file(path, &length);
    NcResult* result = NULL;
    NcProgram* program = NcCompile(source, length, "demo.nc", 7, 1, &result);
    NcInstance* first;
    NcInstance* second;
    int i;
    free(source);
    status(result, NC_COMPLETED);
    require(program != NULL, "typed demo compiles");
    NcResultDestroy(result);
    first = NcInstanceCreate(program, &result);
    status(result, NC_COMPLETED);
    require(first != NULL, "first instance");
    NcResultDestroy(result);
    second = NcInstanceCreate(program, &result);
    status(result, NC_COMPLETED);
    require(second != NULL, "second instance");
    NcResultDestroy(result);
    NcProgramDestroy(program);
    result = NcRun(first);
    status(result, NC_COMPLETED);
    NcResultDestroy(result);
    for (i = 0; i < 16; ++i)
    {
        double time = i * 0.125;
        double dt = 0.125;
        NcArguments* arguments = NcArgumentsCreate();
        require(arguments != NULL, "arguments");
        require(NcArgumentsFloat(arguments, dt) && NcArgumentsFloat(arguments, time), "float arguments");
        result = call(first, "Update", arguments);
        NcArgumentsDestroy(arguments);
        status(result, NC_COMPLETED);
        require(NcResultKind(result) == NC_BOOL && NcResultBool(result) == (i == 0), "first tick result");
        NcResultDestroy(result);
        require(fabs(read_float(first, "PositionX") - (double)(float)(sin(time) * 2)) < 0.000001, "wobble x");
        require(read_float(first, "PositionY") == 1.125, "component update y");
        require(fabs(read_float(first, "PositionZ") - (double)(float)(cos(time) * 2)) < 0.000001, "wobble z");
    }
    result = call(first, "Ticks", NULL);
    status(result, NC_COMPLETED);
    require(NcResultInt(result) == 16, "first instance tick count");
    NcResultDestroy(result);
    result = call(second, "Ticks", NULL);
    status(result, NC_COMPLETED);
    require(NcResultInt(result) == 0, "independent instance state");
    NcResultDestroy(result);
    NcInstanceDestroy(second);
    NcInstanceDestroy(first);
}

static void boundaries(void)
{
    const char* source =
        "export int EchoInt(int x) { return x; }\n"
        "export string EchoString(string x) { return x; }\n"
        "export bool EchoBool(bool x) { return x; }\n"
        "export string Nothing() { return null; }\n"
        "export int Wait(int x) { sleep(0); return x + 1; }\n"
        "export int Bad() { int zero = 0; return 1 / zero; }\n";
    const char* text = "UTF-8 \xe2\x98\x83";
    const int64_t integer = INT64_C(9007199254740993);
    NcResult* result = NULL;
    NcProgram* program = NcCompile(source, (uint32_t)strlen(source), "boundary.nc", 11, 1, &result);
    NcInstance* instance;
    NcArguments* arguments;
    uint32_t length;
    const char* output;
    status(result, NC_COMPLETED);
    NcResultDestroy(result);
    instance = NcInstanceCreate(program, &result);
    status(result, NC_COMPLETED);
    NcResultDestroy(result);
    arguments = NcArgumentsCreate();
    require(NcArgumentsInt(arguments, integer), "int64 argument");
    result = call(instance, "EchoInt", arguments);
    status(result, NC_COMPLETED);
    require(NcResultInt(result) == integer, "exact int64 boundary");
    NcResultDestroy(result);
    NcArgumentsDestroy(arguments);
    arguments = NcArgumentsCreate();
    require(NcArgumentsString(arguments, text, (uint32_t)strlen(text)), "string argument");
    result = call(instance, "EchoString", arguments);
    status(result, NC_COMPLETED);
    output = NcResultString(result, &length);
    require(length == strlen(text) && memcmp(output, text, length) == 0, "owned UTF-8 result");
    NcResultDestroy(result);
    result = call(instance, "EchoInt", arguments);
    status(result, NC_FAILED);
    require(NcResultError(result, &length) != NULL && length != 0, "typed argument rejection");
    NcResultDestroy(result);
    NcArgumentsDestroy(arguments);
    arguments = NcArgumentsCreate();
    require(NcArgumentsBool(arguments, 1), "bool argument");
    result = call(instance, "EchoBool", arguments);
    status(result, NC_COMPLETED);
    require(NcResultBool(result), "bool result");
    NcResultDestroy(result);
    NcArgumentsDestroy(arguments);
    result = call(instance, "Nothing", NULL);
    status(result, NC_COMPLETED);
    require(NcResultKind(result) == NC_NULL, "nullable return");
    NcResultDestroy(result);
    arguments = NcArgumentsCreate();
    require(NcArgumentsInt(arguments, 41), "suspended argument");
    result = call(instance, "Wait", arguments);
    status(result, NC_SUSPENDED);
    NcResultDestroy(result);
    NcArgumentsDestroy(arguments);
    result = NcResume(instance, 0);
    status(result, NC_COMPLETED);
    require(NcResultInt(result) == 42, "suspended return retained");
    NcResultDestroy(result);
    result = call(instance, "Bad", NULL);
    status(result, NC_FAILED);
    require(NcResultLine(result) == 6, "runtime source location");
    NcResultDestroy(result);
    NcInstanceDestroy(instance);
    NcProgramDestroy(program);
    source = "var untyped = 1;";
    program = NcCompile(source, (uint32_t)strlen(source), "reject.nc", 9, 1, &result);
    require(program == NULL, "legacy source rejected");
    status(result, NC_FAILED);
    require(NcResultLine(result) == 1, "compile source location");
    NcResultDestroy(result);
    result = NcRun(NULL);
    status(result, NC_FAILED);
    NcResultDestroy(result);
}

int main(int argc, char** argv)
{
    require(argc == 2, "usage: neocore_unity_c_smoke demo.nc");
    demo(argv[1]);
    boundaries();
    puts("NeoCore Unity C ABI smoke passed: demo, scalar boundaries, instances, rejection, suspension");
    return 0;
}
