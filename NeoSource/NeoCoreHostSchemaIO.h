#pragma once

#include "NeoCoreHostSchema.h"
#include "NeoCoreOptions.h"

namespace NeoCore
{

struct HostSchemaDocument
{
    HostSchema schema;
    std::vector<CompileDefine> defines;

    bool ToJson(std::string& json, std::string& error) const;
    static bool FromJson(const std::string& json, HostSchemaDocument& output, std::string& error);
    bool SaveFile(const std::string& path, std::string& error) const;
    static bool LoadFile(const std::string& path, HostSchemaDocument& output, std::string& error);
};

}
