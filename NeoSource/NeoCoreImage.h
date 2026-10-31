#pragma once

#include "NeoCoreCompiler.h"
#include "NeoCoreModules.h"
#include <cstdint>
#include <vector>

namespace NeoCore
{
namespace Image
{
// 실행 주소나 객체 ID 대신 컴파일된 코드와 정적 계약만 보관한다.
struct Data
{
    StaticCompileResult checked;
    ModuleBundle bundle;
    std::string sourceName;
    bool debug = false;
    std::vector<std::uint8_t> bytecode;
};

bool Write(const Data& data, const HostSchema* schema, std::vector<std::uint8_t>& image, Error& error);
bool Read(const std::vector<std::uint8_t>& image, const HostSchema* schema, Data& data, Error& error);
}
}
