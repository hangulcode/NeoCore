#pragma once

#include <string>

namespace NeoCore
{
struct CompileDiagnostic
{
    std::string message;
    std::string sourceName;
    int line = 0;
    int column = 0;
};
}
