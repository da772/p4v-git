#pragma once

#include <array>
#include <chrono>
#include <filesystem>
#include <future>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "git/GitRepository.h"
#include "git/ShelfService.h"
#include "git/WorkspaceState.h"

namespace p4vgit
{
class StdoutLog;

struct ShelfPullRequestLink
{
    std::string shelf;
    std::string url;
};

struct ShelfPullRequestFiles
{
    std::string shelf;
    std::vector<GitStatusEntry> files;
};

struct RepositorySnapshot
{
    bool succeeded = false;
    bool discovered = false;
    std::string error;
    std::filesystem::path root;
    WorkspaceState workspaceState;
    std::vector<std::string> shelves;
    std::vector<GitStatusEntry> statusEntries;
    std::string currentBranch;
    std::vector<ShelfPullRequestLink> pullRequestLinks;
    std::vector<ShelfPullRequestFiles> pullRequestFiles;
};

struct ShelfJobResult
{
    std::string shelf;
    std::string pullRequestUrl;
    std::string file;
    bool succeeded = false;
    bool removeShelfState = false;
    bool removeFileState = false;
    bool selectShelf = false;
    bool refreshAfter = true;
    ShelfSubmitResult submit;
};

struct ShelfJob
{
    std::string shelf;
    std::string label;
    std::future<ShelfJobResult> future;
};

class AppUi
{
public:
    AppUi();

    void SetStdoutLog(const StdoutLog* stdoutLog);
    void Draw();

private:
    static RepositorySnapshot LoadRepositorySnapshot(const std::filesystem::path& selectedPath, bool discoverRepository, bool logCommands);
    static ShelfJobResult RunCreateShelfJob(const std::filesystem::path& repoRoot, std::string shelfName);
    static ShelfJobResult RunShelveShelfJob(const std::filesystem::path& repoRoot, std::string shelf, std::vector<std::string> files);
    static ShelfJobResult RunSubmitShelfJob(const std::filesystem::path& repoRoot, std::string shelf);
    static ShelfJobResult RunDeleteShelfJob(const std::filesystem::path& repoRoot, std::string shelf);
    static ShelfJobResult RunRemovePullRequestFileJob(const std::filesystem::path& repoRoot, std::string shelf, std::string file);

    void PollAsyncOperations();
    void ApplyRepositorySnapshot(const RepositorySnapshot& snapshot);
    void StartRepositoryLoad(const std::filesystem::path& selectedPath);
    void StartShelfJob(std::string shelf, std::string label, std::future<ShelfJobResult> future);
    bool IsShelfBusy(std::string_view shelf) const;
    std::string ShelfBusyLabel(std::string_view shelf) const;
    void DrawWorkspaceExplorer();
    void DrawFileChanges();
    void DrawLog();
    void UseSourcePath(const std::filesystem::path& path);
    void DrawDirectory(const std::filesystem::path& path, int depth);
    void DrawFileEntry(const std::filesystem::directory_entry& entry);
    void CheckOutFile(const std::filesystem::path& path);
    void RefreshRepositoryData(bool logCommands = true);
    void RefreshRepositoryDataIfNeeded();
    void PruneInvalidWorkspaceShelves();
    void DrawShelfList();
    void DrawShelfPanel(const std::string& shelf, bool isMainShelf);
    void DrawShelfFile(const std::string& shelf, const std::string& file);
    void DrawPullRequestFile(const std::string& shelf, const GitStatusEntry& file);
    void SelectShelf(std::string_view shelf);
    void ClearSelectedShelf();
    void SetPullRequestLink(std::string_view shelf, std::string url);
    std::string PullRequestLink(std::string_view shelf) const;
    std::vector<GitStatusEntry> PullRequestFiles(std::string_view shelf) const;
    std::vector<std::string> MainActiveFiles() const;
    bool IsFileActiveInShelf(std::string_view relativePath) const;
    bool IsFileActive(std::string_view relativePath) const;
    void MoveCheckedOutFile(std::string_view payload, std::string_view toShelf);
    void RemoveCheckedOutFile(const std::string& shelf, const std::string& file);
    void RemovePullRequestFile(const std::string& shelf, const std::string& file);
    void CreateShelfFromInput();
    void ShelveShelf(const std::string& shelf);
    void SubmitShelf(const std::string& shelf);
    void DeleteShelf(const std::string& shelf);
    void OpenPullRequestLink(std::string_view shelf);
    std::string RelativePath(const std::filesystem::path& path) const;
    std::string FileLabel(const std::filesystem::path& path) const;
    static std::string PathLabel(const std::filesystem::path& path);

    const StdoutLog* m_stdoutLog = nullptr;
    std::array<char, 1024> m_sourcePathInput = {};
    std::array<char, 128> m_newShelfNameInput = {};
    std::filesystem::path m_sourcePath;
    bool m_hasSourcePath = false;
    std::string m_sourcePathError;
    std::string m_selectedBranch;
    std::string m_currentGitBranch;
    std::vector<ShelfPullRequestLink> m_pullRequestLinks;
    std::vector<ShelfPullRequestFiles> m_pullRequestFiles;
    std::optional<GitRepository> m_repository;
    ShelfService m_shelfService;
    WorkspaceState m_workspaceState;
    std::vector<std::string> m_shelves;
    std::vector<GitStatusEntry> m_statusEntries;
    std::chrono::steady_clock::time_point m_lastRefreshTime = {};
    std::optional<std::future<RepositorySnapshot>> m_repositoryLoadFuture;
    std::optional<std::future<RepositorySnapshot>> m_refreshFuture;
    std::vector<ShelfJob> m_shelfJobs;
    bool m_logAutoScroll = true;
};
}
