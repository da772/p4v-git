#pragma once

#include "git/GitRepository.h"

#include <string>
#include <vector>

namespace p4vgit
{
class ShelfService
{
public:
    void SetRepository(GitRepository* repository);

    std::string UserPrefix() const;
    std::string MakeShelfBranch(std::string_view shelfName) const;
    std::vector<std::string> Shelves() const;
    bool CreateShelf(std::string_view shelfName);
    bool ShelveFiles(std::string_view shelfBranch, const std::vector<std::string>& files);
    std::string EnsurePullRequest(std::string_view shelfBranch);
    std::string ShelveFilesAndOpenPullRequest(std::string_view shelfBranch, const std::vector<std::string>& files);
    std::string SubmitShelfAsPullRequest(std::string_view shelfBranch);
    bool DeleteShelf(std::string_view shelfBranch, bool deleteRemote);

private:
    static std::string SanitizeBranchPart(std::string text);
    static std::string Quote(std::string_view text);

    GitRepository* m_repository = nullptr;
};
}
