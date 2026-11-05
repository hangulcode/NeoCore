#pragma once

#include "NeoCore.h"

namespace NeoCore
{
// 콘솔 구현 전용 진입점이다. 공개 CompileOptions와 일반 Runtime::Compile에는 예외를 노출하지 않는다.
struct ConsoleCompileAccess
{
    static std::unique_ptr<Program> Compile(const std::string& source, const HostBindings* bindings,
        Error& error, const std::string& sourceName, bool debug, const CompileOptions& options,
        const std::string& entrySourceName);
    // 경고는 오류와 분리해서 반환한다. privateEntrySource가 없으면 일반 가시성을 유지한다.
    static std::unique_ptr<Program> Check(const std::string& source, const HostBindings* bindings,
        Error& error, const std::string& sourceName, bool debug, const CompileOptions& options,
        const std::string* privateEntrySource, std::vector<CompileDiagnostic>& unusedWarnings);
};
}
