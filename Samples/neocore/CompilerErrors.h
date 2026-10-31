#ifndef NEOCORE_SAMPLE_COMPILER_ERRORS_H
#define NEOCORE_SAMPLE_COMPILER_ERRORS_H

#include "NeoCore.h"
#include <cstdio>
#include <memory>
#include <string>

inline int RunCompilerErrorRegression(bool debug)
{
    struct TestCase
    {
        const char* name;
        const char* source;
        bool accepts;
    };
    // 거부할 코드와 가까운 정상 코드를 함께 검사하고, 컴파일 중에는 실행하지 않는다.
    const TestCase cases[] = {
        {"inferred_declaration", "var x=1;", false},
        {"dynamic_function", "fun F(){return 1;}", false},
        {"compact_scalar", "int32 x=1;", false},
        {"implicit_narrowing", "int x=1.5;", false},
        {"bool_as_integer", "int x=true;", false},
        {"nonnull_value", "int x=null;", false},
        {"nullable_arithmetic", "int? x=1;int y=x+1;", false},
        {"reference_declaration", "int x=1;int& r=x;", false},
        {"missing_return", "int F(bool b){if(b)return 1;}", false},
        {"argument_narrowing", "int F(int x){return x;}int y=F(1.2);", false},
        {"numeric_condition", "if(1){}", false},
        {"declaration_order", "int x=y;int y=1;", false},
        {"const_assignment", "const int X=1;X=2;", false},
        {"array_element_type", "array<int> a=array<int>(1);a[0]=1.5;", false},
        {"vector_dimensions", "Vector3 v=Vector2(1,2);", false},
        {"function_signature", "int F(int x){return x;}fn(string)->int f=F;", false},
        {"explicit_numeric_values", "int count;float value=1;int whole=toint(1.5);bool ready=true;if(ready)count+=whole;", true},
        {"nullable_values", "int? value=null;string? text=null;bool absent=value==null&&text==null;", true},
        {"typed_functions", "void Empty(){return;}int F(bool b){if(b)return 1;else return 2;}int value=F(true);Empty();", true},
        {"ordered_const_use", "const int X=1;int y=X;int Read(){return y;}int result=Read();", true},
        {"typed_array", "array<int> a=array<int>(1);a[0]=2;a.append(3);int value=a[0];", true},
        {"typed_vector", "Vector3 v=Vector3(1,2,3);v.x=4.0;float value=v.x;", true},
        {"typed_function_value", "int F(int x){return x;}fn(int)->int f=F;int value=f(2);", true},
        {"runtime_error_is_not_compile_error", "int zero=0;int value=1/zero;", true}
    };
    int checks = 0;
    int failures = 0;
    for (const TestCase& test : cases)
    {
        NeoCore::Error error;
        const std::string sourceName = std::string("compiler_errors/") + test.name + ".nc";
        const std::unique_ptr<NeoCore::Program> program =
            NeoCore::Program::Compile(test.source, error, sourceName, debug);
        const bool passed = test.accepts ? program != nullptr && error.ok()
            : program == nullptr && !error.ok() && error.line > 0;
        ++checks;
        if (!passed)
        {
            ++failures;
            std::fprintf(stderr, "FAIL|compiler-error|%s|expected %s; %s:%d:%d: %s\n",
                test.name, test.accepts ? "acceptance" : "rejection with a source line",
                error.sourceName.c_str(), error.line, error.column, error.message.c_str());
        }
    }
    std::printf("NeoCore compiler-error regression: %d/%d passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}

#endif
