#pragma once

#include "NeoCoreTypes.h"
#include "NeoCoreIR.h"
#include <string>
#include <utility>
#include <vector>

namespace NeoCore
{

class HostSchema;

struct StaticFunctionSignature
{
    std::string name;
    TypeId returnType = InvalidType;
    std::vector<TypeId> parameterTypes;
    bool exported = false;
};

struct StaticDebugSymbol
{
    std::string backendName;
    std::string name;
    TypeId type = InvalidType;
    bool boxed = false;
};

struct StaticGlobalSignature
{
    std::string name;
    TypeId type = InvalidType;
    bool exported = false;
};

struct StaticDeclaration
{
    std::string name;
    std::string type;
    std::vector<std::string> parameters;
    std::size_t offset = 0;
    bool function = false;
};

void CollectStaticDeclarations(const std::string& source, std::vector<StaticDeclaration>& declarations);

struct StaticCompileResult
{
    bool success = false;
    std::string loweredSource;
    // 타입 전용 내부 호출은 이 접두사를 허용한 백엔드 컴파일에서만 명령으로 바뀐다.
    std::string backendIntrinsicPrefix;
    // 숫자 식은 문자열 대신 타입·심볼·원본 위치를 가진 IR로 백엔드에 전달한다.
    TypedIR typedIR;
    // 호스트 멤버는 컴파일 때 바인딩 번호로 확정하고 비공개 호출 객체로 내린다.
    std::string hostPrefix;
    // 생성된 프로퍼티 쓰기 함수의 줄을 원본 대입식 줄로 되돌리는 디버그 정보다.
    std::vector<std::pair<int, int>> hostWriteSourceLines;
    std::string errorMessage;
    int errorLine = 0;
    int errorColumn = 0;
    TypeRegistry types;
    std::vector<StaticFunctionSignature> functions;
    std::vector<StaticDebugSymbol> debugSymbols;
    std::vector<StaticGlobalSignature> globals;
};

// 타입 검사한 숫자 식은 IR로, 아직 이관하지 않은 문장과 식은 줄 수를 보존한 골격으로 내린다.
// IR은 원본 위치를 보존한다. 기존 경로의 변환 식 때문에 백엔드 열 위치는 달라질 수 있다.
struct CompileDefine;
struct StaticDefineReplacement
{
    std::size_t begin = 0, end = 0;
    std::string text;
};
bool ExpandStaticDefines(const std::string& source, const std::vector<CompileDefine>& defines,
    std::vector<StaticDefineReplacement>& replacements, std::string& error, int& line, int& column);
bool CompileStaticSource(const std::string& source, StaticCompileResult& result, const HostSchema* hostSchema = nullptr,
    const std::vector<CompileDefine>* defines = nullptr);

}
