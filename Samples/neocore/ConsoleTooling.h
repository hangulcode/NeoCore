#pragma once

#include <string>
#include <cstddef>

std::string ConsoleModuleCacheKey(const std::string& name, const std::string& importer);
int RunNeoCoreDebugAdapter();
int RunNeoCoreLanguageServer(const std::string& hostSchema = "", const std::string& hostImplementation = "",
    const std::string& resourceRoot = "", std::size_t diagnosticLimit = 100);
