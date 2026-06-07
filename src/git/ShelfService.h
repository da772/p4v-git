#pragma once

#include "git/GitRepository.h"

#include <string>
#include <vector>

namespace p4vgit
{
struct ShelfSubmitResult
{
    std::string shelfUrl;
    bool merged = false;
    bool branchDeleted = false;
};

struct MainSyncStatus
{
    bool fetched = false;
    bool remoteAvailable = false;
    int behindCount = 0;
};

class ShelfService
{
public:
    void SetRepository(GitRepository* repository);
    void SetTargetBranch(std::string_view branch);

    std::string UserPrefix() const;
    std::string MakeShelfBranch(std::string_view shelfName) const;
    std::vector<std::string> Shelves(bool logCommand = true) const;
    bool IsShelfValid(std::string_view shelfBranch, bool logCommand = true) const;
    std::string FindShelfLink(std::string_view shelfBranch, bool logCommand = true) const;
    std::vector<GitStatusEntry> ShelfFiles(std::string_view shelfBranch, bool logCommand = true) const;
    bool CreateShelf(std::string_view shelfName);
    bool ShelveFiles(std::string_view shelfBranch, const std::vector<std::string>& files, std::string_view summary, std::string_view description);
    bool RevertFileFromShelf(std::string_view shelfBranch, std::string_view file);
    bool RestoreFileFromShelfToWorkingTree(std::string_view shelfBranch, std::string_view file);
    bool UndoLocalFileChanges(std::string_view file);
    bool OpenFileDiff(std::string_view file) const;
    bool OpenFileVersionDiff(std::string_view leftFile, std::string_view leftCommit, std::string_view rightFile, std::string_view rightCommit) const;
    bool OpenFileVersionToWorkingDiff(std::string_view historyFile, std::string_view workingFile, std::string_view commit) const;
    bool SubmitMain(const std::vector<std::string>& files, std::string_view summary, std::string_view description);
    MainSyncStatus RefreshMain(bool logCommand = true) const;
    bool PullMain();
    std::string EnsureShelfLink(std::string_view shelfBranch);
    std::string ShelveFilesAndEnsureShelfLink(std::string_view shelfBranch, const std::vector<std::string>& files, std::string_view summary, std::string_view description);
    ShelfSubmitResult SubmitShelf(std::string_view shelfBranch, const std::vector<std::string>& files);
    bool DeleteShelf(std::string_view shelfBranch, bool deleteRemote);

private:
    const std::string& TargetBranch() const { return m_targetBranch; }
    bool PullMainForSubmit();
    bool OpenDefaultMergeTool() const;
    bool RestoreFilesFromMain(const std::vector<std::string>& files);
    std::string BuildUserPrefix(bool logCommand) const;
    static std::string SanitizeBranchPart(std::string text);
    static std::string Quote(std::string_view text);

    GitRepository* m_repository = nullptr;
    std::string m_targetBranch = "main";
};
}
