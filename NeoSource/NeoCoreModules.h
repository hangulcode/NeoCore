#pragma once

#include "NeoCore.h"
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
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

// 줄 표의 원본 파일 이름. 같은 파일의 줄들이 문자열 하나를 나눠 가진다(줄마다 복사하지 않는다).
// 값처럼 복사할 수 있고 const std::string& 로 읽는다.
class ModuleSourceName
{
public:
    ModuleSourceName() {}
    ModuleSourceName(const std::string& text) : text_(std::make_shared<const std::string>(text)) {}
    operator const std::string&() const { return str(); }
    const std::string& str() const { return text_ ? *text_ : Empty(); }
    bool empty() const { return str().empty(); }

private:
    std::shared_ptr<const std::string> text_;
    static const std::string& Empty() { static const std::string empty; return empty; }
};
inline bool operator==(const ModuleSourceName& a, const std::string& b) { return a.str() == b; }
inline bool operator==(const std::string& a, const ModuleSourceName& b) { return a == b.str(); }
inline bool operator!=(const ModuleSourceName& a, const std::string& b) { return a.str() != b; }
inline bool operator!=(const std::string& a, const ModuleSourceName& b) { return a != b.str(); }

struct ModuleSourceLine
{
    ModuleSourceName sourceName;
    int line = 1;
    // 이 줄의 열 대응은 ModuleBundle::columns 의 [firstColumn, firstColumn + columnCount) 구간이다.
    std::uint32_t firstColumn = 0, columnCount = 0;
};

struct ModuleBundle
{
    std::string source;
    std::vector<ModuleSourceLine> lines;
    // 모든 줄의 열 대응을 줄 순서대로 이어 둔다. 줄마다 따로 할당하지 않는다.
    std::vector<ModuleSourceColumn> columns;
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
    const std::string* privateEntrySource = nullptr, const ModuleCacheKey& cacheKey = ModuleCacheKey());

}
