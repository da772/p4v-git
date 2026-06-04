#pragma once

#include <string>
#include <string_view>

namespace p4vgit
{
struct ProcessResult
{
    int exitCode = -1;
    std::string output;

    bool Succeeded() const { return exitCode == 0; }
};

extern ProcessResult RunHiddenCommand(std::string_view command);
extern bool StartHiddenCommand(std::string_view command);
}
