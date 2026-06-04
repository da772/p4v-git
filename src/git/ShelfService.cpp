#include "git/ShelfService.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <system_error>

namespace p4vgit
{
void ShelfService::SetRepository(GitRepository* repository)
{
    m_repository = repository;
}

std::string ShelfService::UserPrefix() const
{
    if (m_repository == nullptr)
        return "shelves/user/";

    return "shelves/" + SanitizeBranchPart(m_repository->UserName()) + "/";
}

std::string ShelfService::MakeShelfBranch(std::string_view shelfName) const
{
    return UserPrefix() + SanitizeBranchPart(std::string(shelfName));
}

std::vector<std::string> ShelfService::Shelves() const
{
    std::vector<std::string> shelves;
    if (m_repository == nullptr)
        return shelves;

    const std::string prefix = UserPrefix();
    for (const std::string& branch : m_repository->LocalBranches())
    {
        if (branch.rfind(prefix, 0) == 0)
            shelves.push_back(branch);
    }

    return shelves;
}

bool ShelfService::CreateShelf(std::string_view shelfName)
{
    if (m_repository == nullptr)
        return false;

    const std::string branch = MakeShelfBranch(shelfName);
    if (m_repository->BranchExists(branch))
        return true;

    return m_repository->Run("branch " + Quote(branch)).Succeeded();
}

bool ShelfService::ShelveFiles(std::string_view shelfBranch, const std::vector<std::string>& files)
{
    if (m_repository == nullptr || shelfBranch.empty() || files.empty())
        return false;

    if (!m_repository->BranchExists(shelfBranch))
    {
        if (!m_repository->Run("branch " + Quote(shelfBranch)).Succeeded())
            return false;
    }

    const std::filesystem::path worktreeRoot = m_repository->GitDir() / "p4v-git" / "worktrees" / SanitizeBranchPart(std::string(shelfBranch));
    std::error_code error;
    std::filesystem::remove_all(worktreeRoot, error);
    std::filesystem::create_directories(worktreeRoot.parent_path(), error);

    if (!m_repository->Run("worktree add --force " + Quote(worktreeRoot.string()) + " " + Quote(shelfBranch)).Succeeded())
        return false;

    bool addSucceeded = true;
    for (const std::string& file : files)
    {
        const std::filesystem::path sourcePath = m_repository->Root() / file;
        const std::filesystem::path targetPath = worktreeRoot / file;
        std::filesystem::create_directories(targetPath.parent_path(), error);

        if (std::filesystem::exists(sourcePath))
            std::filesystem::copy_file(sourcePath, targetPath, std::filesystem::copy_options::overwrite_existing, error);
        else
            std::filesystem::remove(targetPath, error);

        GitRepository shelfWorktree(worktreeRoot);
        addSucceeded = shelfWorktree.Run("add -- " + Quote(file)).Succeeded() && addSucceeded;
    }

    bool committed = false;
    if (addSucceeded)
    {
        GitRepository shelfWorktree(worktreeRoot);
        committed = shelfWorktree.Run("commit -m " + Quote("Shelve files")).Succeeded();
    }

    m_repository->Run("worktree remove --force " + Quote(worktreeRoot.string()));

    return addSucceeded && committed;
}

std::string ShelfService::ShareShelf(std::string_view shelfBranch)
{
    if (m_repository == nullptr || shelfBranch.empty())
        return {};

    if (!m_repository->Run("push -u origin " + Quote(shelfBranch)).Succeeded())
        return {};

    return ToGitHubBranchUrl(m_repository->RemoteUrl("origin"), shelfBranch);
}

bool ShelfService::SubmitShelf(std::string_view shelfBranch)
{
    if (m_repository == nullptr || shelfBranch.empty())
        return false;

    const std::string currentBranch = m_repository->CurrentBranch();
    bool succeeded = m_repository->Run("switch main").Succeeded();
    if (succeeded)
        succeeded = m_repository->Run("pull --ff-only origin main").Succeeded();
    if (succeeded)
        succeeded = m_repository->Run("merge --no-ff " + Quote(shelfBranch) + " -m " + Quote("Submit " + std::string(shelfBranch))).Succeeded();
    if (succeeded)
        succeeded = m_repository->Run("push origin main").Succeeded();

    if (!currentBranch.empty() && currentBranch != "main")
        m_repository->Run("switch " + Quote(currentBranch));

    return succeeded;
}

std::string ShelfService::SanitizeBranchPart(std::string text)
{
    std::replace_if(text.begin(), text.end(), [](char ch) {
        return !(ch >= 'a' && ch <= 'z') &&
               !(ch >= 'A' && ch <= 'Z') &&
               !(ch >= '0' && ch <= '9') &&
               ch != '-' &&
               ch != '_' &&
               ch != '.';
    }, '-');

    while (!text.empty() && (text.front() == '.' || text.front() == '-'))
        text.erase(text.begin());
    while (!text.empty() && (text.back() == '.' || text.back() == '-'))
        text.pop_back();

    if (text.empty())
        return "shelf";

    return text;
}

std::string ShelfService::ToGitHubBranchUrl(std::string remoteUrl, std::string_view branch)
{
    if (remoteUrl.ends_with(".git"))
        remoteUrl.resize(remoteUrl.size() - 4);

    const std::string gitHubSshPrefix = "git@github.com:";
    if (remoteUrl.rfind(gitHubSshPrefix, 0) == 0)
        remoteUrl = "https://github.com/" + remoteUrl.substr(gitHubSshPrefix.size());

    return remoteUrl + "/tree/" + std::string(branch);
}

std::string ShelfService::Quote(std::string_view text)
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
