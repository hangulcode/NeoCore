#pragma once

#include <string>

class ConsoleOutputRoot
{
public:
    explicit ConsoleOutputRoot(const std::string& directory);
    ~ConsoleOutputRoot();
    ConsoleOutputRoot(const ConsoleOutputRoot&) = delete;
    ConsoleOutputRoot& operator=(const ConsoleOutputRoot&) = delete;

    bool WriteText(const std::string& path, const std::string& text, std::string& error) const;
private:
#ifdef _WIN32
    void* directory_;
#else
    int directory_;
#endif
};
