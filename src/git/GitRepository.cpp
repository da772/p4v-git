#include "git/GitRepository.h"

#include "platform/Process.h"

#include <algorithm>
#include <iostream>
#include <sstream>

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

    const ProcessResult result = RunHiddenCommand(command);
    if (!result.Succeeded())
        return std::nullopt;

    return GitRepository(Trim(result.output));
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

    const ProcessResult processResult = RunHiddenCommand(command);
    GitCommandResult result;
    result.exitCode = processResult.exitCode;
    result.output = processResult.output;
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
    const auto addBranch = [&branches](std::string branch) {
        if (branch.empty() || branch == "origin")
            return;

        if (std::find(branches.begin(), branches.end(), branch) != branches.end())
            return;

        branches.push_back(std::move(branch));
    };

    if (Run("remote get-url origin", false).Succeeded())
        Run("fetch origin --prune", logCommand);

    std::istringstream stream(Run("show-ref", logCommand).output);
    std::string line;
    while (std::getline(stream, line))
    {
        line = Trim(std::move(line));
        const size_t refPosition = line.find(' ');
        if (refPosition == std::string::npos)
            continue;

        const std::string ref = line.substr(refPosition + 1);
        constexpr std::string_view localPrefix = "refs/heads/";
        constexpr std::string_view originPrefix = "refs/remotes/origin/";

        if (ref.rfind(localPrefix, 0) == 0)
            addBranch(ref.substr(localPrefix.size()));
        else if (ref.rfind(originPrefix, 0) == 0)
        {
            const std::string branch = ref.substr(originPrefix.size());
            if (branch != "HEAD")
                addBranch(branch);
        }
    }

    std::sort(branches.begin(), branches.end());
    return branches;
}

std::vector<GitStatusEntry> GitRepository::Status(bool logCommand) const
{
    std::vector<GitStatusEntry> entries;
    if (logCommand)
        std::cout << "$ git -C " << Quote(m_root) << " status --porcelain=v1 -z -uall\n";

    const std::string output = Run("status --porcelain=v1 -z -uall", false).output;
    size_t cursor = 0;
    while (cursor < output.size())
    {
        const size_t pathEnd = output.find('\0', cursor);
        if (pathEnd == std::string::npos)
            break;

        const std::string_view entry(output.data() + cursor, pathEnd - cursor);
        cursor = pathEnd + 1;
        if (entry.size() < 4)
            continue;

        const std::string status = Trim(std::string(entry.substr(0, 2)));
        const std::string path(entry.substr(3));
        if (path.empty())
            continue;

        entries.push_back({
            path,
            status,
        });

        if (status.find('R') != std::string::npos || status.find('C') != std::string::npos)
        {
            const size_t renameSourceEnd = output.find('\0', cursor);
            if (renameSourceEnd == std::string::npos)
                break;
            cursor = renameSourceEnd + 1;
        }
    }

    return entries;
}

bool GitRepository::BranchExists(std::string_view branch, bool logCommand) const
{
    const std::string ref = "refs/heads/" + std::string(branch);
    return Run("show-ref --verify --quiet " + Quote(std::string_view(ref)), logCommand).Succeeded();
}

bool GitRepository::CheckoutBranch(std::string_view branch)
{
    if (BranchExists(branch, false))
        return Run("checkout " + Quote(branch)).Succeeded();

    const std::string branchText(branch);
    const std::string remoteRef = "refs/remotes/origin/" + branchText;
    if (Run("show-ref --verify --quiet " + Quote(std::string_view(remoteRef)), false).Succeeded())
    {
        const std::string remoteBranch = "origin/" + branchText;
        return Run("checkout --track -b " + Quote(std::string_view(branchText)) + " " + Quote(std::string_view(remoteBranch))).Succeeded();
    }

    return Run("checkout " + Quote(branch)).Succeeded();
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
