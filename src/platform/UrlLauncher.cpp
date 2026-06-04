#include "platform/UrlLauncher.h"

#include <cstdlib>
#include <string>

namespace p4vgit
{
bool OpenUrl(std::string_view url)
{
    if (url.empty())
        return false;

    std::string command;
#ifdef _WIN32
    command = "start \"\" \"" + std::string(url) + "\"";
#elif defined(__APPLE__)
    command = "open \"" + std::string(url) + "\"";
#else
    command = "xdg-open \"" + std::string(url) + "\"";
#endif

    return std::system(command.c_str()) == 0;
}
}
