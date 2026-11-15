#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace NeoCore {
enum class ProfileKind { Script, Host };
struct ProfileEntry {
    ProfileKind kind = ProfileKind::Script;
    std::string name;
    std::string sourceName;
    int line = 0;
    int functionId = -1;
    std::uint64_t calls = 0;
    std::uint64_t selfNanoseconds = 0;
    std::uint64_t inclusiveNanoseconds = 0;
};
struct ProfileSnapshot {
    bool enabled = false;
    bool complete = true;
    std::uint64_t activeNanoseconds = 0;
    std::vector<ProfileEntry> entries;
};
}
