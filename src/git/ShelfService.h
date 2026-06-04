#pragma once

#include "git/GitRepository.h"

#include <string>
#include <vector>

namespace p4vgit
{
struct ShelfSubmitResult
{
    std::string pullRequestUrl;
    bool merged = false;
    bool branchDeleted = false;
};

class ShelfService
{
public:
    void SetRepository(GitRepository* repository);

    std::string UserPrefix() const;
    std::string MakeShelfBranch(std::string_view shelfName) const;
    std::vector<std::string> Shelves(bool logCommand = true) const;
    bool IsShelfValid(std::string_view shelfBranch, bool logCommand = true) const;
    std::string FindPullRequest(std::string_view shelfBranch, bool logCommand = true) const;
    bool CreateShelf(std::string_view shelfName);
    bool ShelveFiles(std::string_view shelfBranch, const std::vector<std::string>& files);
    bool RemoveFileFromShelf(std::string_view shelfBranch, std::string_view file);
    std::string EnsurePullRequest(std::string_view shelfBranch);
    std::string ShelveFilesAndOpenPullRequest(std::string_view shelfBranch, const std::vector<std::string>& files);
    ShelfSubmitResult SubmitShelfAsPullRequest(std::string_view shelfBranch);
    bool DeleteShelf(std::string_view shelfBranch, bool deleteRemote);

private:
    std::string BuildUserPrefix(bool logCommand) const;
    static std::string SanitizeBranchPart(std::string text);
    static std::string Quote(std::string_view text);

    GitRepository* m_repository = nullptr;
};
}
