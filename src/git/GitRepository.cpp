#include "git/GitRepository.h"

#include "platform/Process.h"

#include <algorithm>
#include <iostream>
#include <sstream>

namespace p4vgit
{
namespace
{
struct GitHubRepositoryInfo
{
    std::string owner;
    std::string repository;
};

static std::string TrimText(std::string text)
{
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '\t'))
        text.pop_back();

    size_t start = 0;
    while (start < text.size() && (text[start] == ' ' || text[start] == '\t'))
        ++start;

    return text.substr(start);
}

static std::optional<GitHubRepositoryInfo> ParseGitHubRemote(std::string remoteUrl)
{
    remoteUrl = TrimText(std::move(remoteUrl));
    if (remoteUrl.ends_with(".git"))
        remoteUrl.resize(remoteUrl.size() - 4);

    const std::string httpsPrefix = "https://github.com/";
    const std::string sshPrefix = "git@github.com:";
    const std::string sshUrlPrefix = "ssh://git@github.com/";

    if (remoteUrl.rfind(httpsPrefix, 0) == 0)
        remoteUrl = remoteUrl.substr(httpsPrefix.size());
    else if (remoteUrl.rfind(sshPrefix, 0) == 0)
        remoteUrl = remoteUrl.substr(sshPrefix.size());
    else if (remoteUrl.rfind(sshUrlPrefix, 0) == 0)
        remoteUrl = remoteUrl.substr(sshUrlPrefix.size());
    else
        return std::nullopt;

    const size_t separator = remoteUrl.find('/');
    if (separator == std::string::npos || separator == 0 || separator + 1 >= remoteUrl.size())
        return std::nullopt;

    return GitHubRepositoryInfo{
        remoteUrl.substr(0, separator),
        remoteUrl.substr(separator + 1),
    };
}

static std::vector<std::string_view> SplitFields(std::string_view text, char separator)
{
    std::vector<std::string_view> fields;
    size_t cursor = 0;
    while (cursor <= text.size())
    {
        const size_t next = text.find(separator, cursor);
        if (next == std::string_view::npos)
        {
            fields.push_back(text.substr(cursor));
            break;
        }

        fields.push_back(text.substr(cursor, next - cursor));
        cursor = next + 1;
    }

    return fields;
}
}

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

std::vector<GitFileHistoryEntry> GitRepository::FileHistory(std::string_view file, bool logCommand) const
{
    std::vector<GitFileHistoryEntry> entries;
    if (file.empty())
        return entries;

    const std::string fileText(file);
    const std::string commitUrlPrefix = GitHubCommitUrl("", false);
    const std::string format = "%H%x1f%h%x1f%an%x1f%ad%x1f%s%x1e";
    const GitCommandResult result = Run("log --follow --date=short --pretty=format:" + Quote(std::string_view(format)) + " -- " + Quote(std::string_view(fileText)), logCommand);
    if (!result.Succeeded())
        return entries;

    size_t cursor = 0;
    while (cursor < result.output.size())
    {
        const size_t recordEnd = result.output.find('\x1e', cursor);
        if (recordEnd == std::string::npos)
            break;

        const std::string_view record(result.output.data() + cursor, recordEnd - cursor);
        cursor = recordEnd + 1;
        if (record.empty())
            continue;

        const std::vector<std::string_view> fields = SplitFields(record, '\x1f');
        if (fields.size() < 5)
            continue;

        GitFileHistoryEntry entry;
        entry.commit = std::string(fields[0]);
        entry.shortCommit = std::string(fields[1]);
        entry.author = std::string(fields[2]);
        entry.date = std::string(fields[3]);
        entry.summary = std::string(fields[4]);
        if (!commitUrlPrefix.empty())
            entry.url = commitUrlPrefix + entry.commit;
        entries.push_back(std::move(entry));
    }

    return entries;
}

std::string GitRepository::GitHubCommitUrl(std::string_view commit, bool logCommand) const
{
    const std::optional<GitHubRepositoryInfo> repositoryInfo = ParseGitHubRemote(RemoteUrl("origin", logCommand));
    if (!repositoryInfo.has_value())
        return {};

    std::string url = "https://github.com/" + repositoryInfo->owner + "/" + repositoryInfo->repository + "/commit/";
    url += commit;
    return url;
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

bool GitRepository::CreateAndCheckoutBranch(std::string_view branch, std::string_view startPoint)
{
    if (branch.empty() || BranchExists(branch, false))
        return false;

    std::string command = "checkout -b " + Quote(branch);
    if (!startPoint.empty())
        command += " " + Quote(startPoint);

    return Run(command).Succeeded();
}

bool GitRepository::CheckoutFileVersion(std::string_view commit, std::string_view file)
{
    if (commit.empty() || file.empty())
        return false;

    return Run("checkout " + Quote(commit) + " -- " + Quote(file)).Succeeded();
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
