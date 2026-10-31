#pragma once

#include "NeoCoreCompiler.h"
#include "NeoCoreModules.h"
#include "NeoScript.h"

namespace NeoCore
{

struct DebugAccess
{
    static bool Get(Program& owner, NeoScript::IRuntime*& runtime,
        NeoScript::ProgramHandle& program, NeoScript::InstanceHandle& instance,
        const StaticCompileResult*& checked, const ModuleBundle*& bundle);
};

}
