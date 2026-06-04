#include "git/GitRepository.h"

#include <array>
#include <cstdio>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#define P4VGIT_POPEN _popen
#define P4VGIT_PCLOSE _pclose
#else
#define P4VGIT_POPEN popen
#define P4VGIT_PCLOSE pclose
#endif

namespace p4vgit
{
GitRepository::GitRepository(std::filesystem::path root)
    : m_root(std::move(root))
{
}

std::optional<GitRepository> GitRepository::Discover(const std::filesystem::path& selectedPath)
{
    const std::filesystem::path probePath = std::filesystem::is_directory(selectedPath) ? selectedPath : selectedPath.parent_path();
    const std::string command = "git -C " + Quote(probePath) + " rev-parse --show-toplevel 2>&1";

    std::array<char, 256> buffer = {};
    std::string output;
    FILE* pipe = P4VGIT_POPEN(command.c_str(), "r");
    if (pipe == nullptr)
        return std::nullopt;

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
        output += buffer.data();

    const int exitCode = P4VGIT_PCLOSE(pipe);
    if (exitCode != 0)
        return std::nullopt;

    return GitRepository(Trim(output));
}

GitCommandResult GitRepository::Run(std::string_view arguments) const
{
    return Run(arguments, true);
}

GitCommandResult GitRepository::Run(std::string_view arguments, bool logCommand) const
{
    const std::string command = "git -C " + Quote(m_root) + " " + std::string(arguments) + " 2>&1";
    if (logCommand)
        std::cout << "$ " << command << '\n';

    GitCommandResult result;
    std::array<char, 512> buffer = {};
    FILE* pipe = P4VGIT_POPEN(command.c_str(), "r");
    if (pipe == nullptr)
    {
        result.output = "Failed to start git process";
        if (logCommand)
            std::cout << result.output << '\n';
        return result;
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
        result.output += buffer.data();

    result.exitCode = P4VGIT_PCLOSE(pipe);
    if (logCommand && !result.output.empty())
        std::cout << result.output;
    if (logCommand && !result.Succeeded())
        std::cout << "git command failed with exit code " << result.exitCode << '\n';

    return result;
}

std::string GitRepository::CurrentBranch(bool logCommand) const
{
    return Trim(Run("branch --show-current", logCommand).output);
}

std::string GitRepository::UserName(bool logCommand) const
{
    std::string userName = Trim(Run("config user.name", logCommand).output);
    if (userName.empty())
        userName = "user";

    return userName;
}

std::string GitRepository::RemoteUrl(std::string_view remote, bool logCommand) const
{
    return Trim(Run("remote get-url " + std::string(remote), logCommand).output);
}

std::filesystem::path GitRepository::GitDir(bool logCommand) const
{
    std::filesystem::path gitDir = Trim(Run("rev-parse --git-dir", logCommand).output);
    if (gitDir.is_relative())
        gitDir = m_root / gitDir;

    return gitDir;
}

std::vector<std::string> GitRepository::LocalBranches(bool logCommand) const
{
    std::vector<std::string> branches;
    std::istringstream stream(Run("for-each-ref --format=%(refname:short) refs/heads", logCommand).output);
    std::string line;
    while (std::getline(stream, line))
    {
        line = Trim(line);
        if (!line.empty())
            branches.push_back(line);
    }

    return branches;
}

std::vector<GitStatusEntry> GitRepository::Status(bool logCommand) const
{
    std::vector<GitStatusEntry> entries;
    std::istringstream stream(Run("status --porcelain=v1", logCommand).output);
    std::string line;
    while (std::getline(stream, line))
    {
        if (line.size() < 4)
            continue;

        entries.push_back({
            line.substr(3),
            Trim(line.substr(0, 2)),
        });
    }

    return entries;
}

bool GitRepository::BranchExists(std::string_view branch, bool logCommand) const
{
    const std::string ref = "refs/heads/" + std::string(branch);
    return Run("show-ref --verify --quiet " + Quote(std::string_view(ref)), logCommand).Succeeded();
}

std::string GitRepository::Trim(std::string text)
{
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '\t'))
        text.pop_back();

    size_t start = 0;
    while (start < text.size() && (text[start] == ' ' || text[start] == '\t'))
        ++start;

    return text.substr(start);
}

std::string GitRepository::Quote(const std::filesystem::path& path)
{
    const std::string pathText = path.string();
    return Quote(std::string_view(pathText));
}

std::string GitRepository::Quote(std::string_view text)
{
    std::string quoted = "\"";
    for (char ch : text)
    {
        if (ch == '"')
            quoted += "\\\"";
        else
            quoted += ch;
    }
    quoted += '"';
    return quoted;
}
}
