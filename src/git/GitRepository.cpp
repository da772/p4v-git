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
    const std::string command = "git -C " + Quote(m_root) + " " + std::string(arguments) + " 2>&1";
    std::cout << "$ " << command << '\n';

    GitCommandResult result;
    std::array<char, 512> buffer = {};
    FILE* pipe = P4VGIT_POPEN(command.c_str(), "r");
    if (pipe == nullptr)
    {
        result.output = "Failed to start git process";
        std::cout << result.output << '\n';
        return result;
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
        result.output += buffer.data();

    result.exitCode = P4VGIT_PCLOSE(pipe);
    if (!result.output.empty())
        std::cout << result.output;
    if (!result.Succeeded())
        std::cout << "git command failed with exit code " << result.exitCode << '\n';

    return result;
}

std::string GitRepository::CurrentBranch() const
{
    return Trim(Run("branch --show-current").output);
}

std::string GitRepository::UserName() const
{
    std::string userName = Trim(Run("config user.name").output);
    if (userName.empty())
        userName = "user";

    return userName;
}

std::string GitRepository::RemoteUrl(std::string_view remote) const
{
    return Trim(Run("remote get-url " + std::string(remote)).output);
}

std::filesystem::path GitRepository::GitDir() const
{
    std::filesystem::path gitDir = Trim(Run("rev-parse --git-dir").output);
    if (gitDir.is_relative())
        gitDir = m_root / gitDir;

    return gitDir;
}

std::vector<std::string> GitRepository::LocalBranches() const
{
    std::vector<std::string> branches;
    std::istringstream stream(Run("for-each-ref --format=%(refname:short) refs/heads").output);
    std::string line;
    while (std::getline(stream, line))
    {
        line = Trim(line);
        if (!line.empty())
            branches.push_back(line);
    }

    return branches;
}

std::vector<GitStatusEntry> GitRepository::Status() const
{
    std::vector<GitStatusEntry> entries;
    std::istringstream stream(Run("status --porcelain=v1").output);
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

bool GitRepository::BranchExists(std::string_view branch) const
{
    const std::string ref = "refs/heads/" + std::string(branch);
    return Run("show-ref --verify --quiet " + Quote(std::string_view(ref))).Succeeded();
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
