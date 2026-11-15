#pragma once

#include <string>

namespace NeoCore
{
struct RuntimeStackFrame
{
    std::string functionName;
    std::string sourceName;
    int line = 0;
    int column = 0;
    int functionId = -1;
    int instruction = -1;
};

struct CompileDiagnostic
{
    std::string message;
    std::string sourceName;
    int line = 0;
    int column = 0;
};
}
