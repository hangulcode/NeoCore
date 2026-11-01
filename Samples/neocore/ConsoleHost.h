#pragma once

#include "NeoCoreHost.h"
#include "NeoCoreHostSchemaIO.h"
#include "NeoCoreScriptHost.h"
#include <map>

class ConsoleHost
{
public:
    ConsoleHost(const NeoCore::HostSchemaDocument& document, const std::string& implementationSource,
        const std::string& resourceRoot);
    const NeoCore::HostBindings& Bindings() const { return *bindings_; }
    const NeoCore::HostSchema& Schema() const { return schema_; }
    std::shared_ptr<const NeoCore::ScriptHostCompileConfig> Config() const { return config_; }
private:
    struct Object { NeoCore::TypeId type; };
    NeoCore::HostSchema schema_;
    std::unique_ptr<NeoCore::HostBindings> bindings_;
    std::shared_ptr<NeoCore::ScriptHostCompileConfig> config_;
    std::map<std::uint64_t, Object> objects_;
    std::uint64_t nextId_ = 1;
    std::string resourceRoot_, fileError_;
    NeoCore::ObjectHandle NewObject(NeoCore::TypeId type);
    static void* Resolve(void* context, NeoCore::TypeId type, NeoCore::ObjectHandle object);
};
