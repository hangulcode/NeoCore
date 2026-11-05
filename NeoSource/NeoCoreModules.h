#pragma once

#include "NeoCore.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace NeoCore
{

struct ModuleSourceColumn
{
    int generatedColumn = 1;
    int sourceColumn = 1;
    bool fixed = false;
};

struct ModuleSourceLine
{
    std::string sourceName;
    int line = 1;
    std::vector<ModuleSourceColumn> columns;
};

struct ModuleBundle
{
    std::string source;
    std::vector<ModuleSourceLine> lines;
    std::map<std::string, std::string> symbolNames;
    // 이름 치환으로 길이가 달라진 토큰도 원래 파일·줄·열로 되돌린다.
    void Translate(Error& error) const;
};

// 합친 결과도 반드시 정적 프런트엔드를 통과해야 한다. 동적 백엔드 소스는 생성하지 않는다.
// externalNames에는 모듈에서 사용할 호스트 타입·전역 이름을 명시한다.
// privateEntrySource는 콘솔 내부 진입점만 전달한다. 일반 임베딩 컴파일은 항상 nullptr을 사용한다.
bool BundleModules(const std::string& source, const std::string& sourceName,
    const ModuleLoader& loader, ModuleBundle& result, Error& error,
    const std::vector<std::string>& externalNames = std::vector<std::string>(),
    const std::vector<CompileDefine>* defines = nullptr, std::size_t diagnosticLimit = 100,
    const std::string* privateEntrySource = nullptr);

}
