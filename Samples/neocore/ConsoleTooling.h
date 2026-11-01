#pragma once

#include <string>
#include <cstddef>

int RunNeoCoreDebugAdapter();
int RunNeoCoreLanguageServer(const std::string& hostSchema = "", const std::string& hostImplementation = "",
    const std::string& resourceRoot = "", std::size_t diagnosticLimit = 100);
