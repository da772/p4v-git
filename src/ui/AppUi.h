#pragma once

#include <array>
#include <chrono>
#include <cstdint>
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
class Window;

struct ShelfLinkEntry
{
    std::string shelf;
    std::string url;
};

struct ShelfCommittedFileList
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
    std::vector<std::string> branches;
    std::vector<std::string> shelves;
    std::vector<GitStatusEntry> statusEntries;
    std::string currentBranch;
    std::string targetBranch;
    std::vector<ShelfLinkEntry> shelfLinks;
    std::vector<ShelfCommittedFileList> shelfFiles;
    bool mainRemoteAvailable = false;
    int mainBehindCount = 0;
    uint64_t refreshGeneration = 0;
};

struct ShelfJobResult
{
    std::string shelf;
    std::string shelfUrl;
    std::string file;
    std::vector<std::string> files;
    bool succeeded = false;
    bool addFileState = false;
    bool addFilesState = false;
    bool clearMainActiveFiles = false;
    bool deleteShelfState = false;
    bool revertActiveFileState = false;
    bool removeShelfFileState = false;
    bool selectShelf = false;
    bool selectTargetBranch = false;
    bool refreshAfter = true;
    ShelfSubmitResult submit;
};

struct ShelfJob
{
    std::string shelf;
    std::string label;
    std::future<ShelfJobResult> future;
};

enum class ConfirmationAction
{
    None,
    RevertActiveFile,
    RestoreShelfFile,
};

class AppUi
{
public:
    AppUi();

    void SetStdoutLog(const StdoutLog* stdoutLog);
    void SetWindow(Window* window);
    void OnWindowFocusGained();
    void Draw();

private:
    static RepositorySnapshot LoadRepositorySnapshot(const std::filesystem::path& selectedPath, bool discoverRepository, bool logCommands, uint64_t refreshGeneration);
    static ShelfJobResult RunCreateShelfJob(const std::filesystem::path& repoRoot, std::string targetBranch, std::string shelfName, std::vector<std::string> files);
    static ShelfJobResult RunCreateTargetBranchJob(const std::filesystem::path& repoRoot, std::string baseBranch, std::string newBranch);
    static ShelfJobResult RunSelectTargetBranchJob(const std::filesystem::path& repoRoot, std::string targetBranch);
    static ShelfJobResult RunShelveShelfJob(const std::filesystem::path& repoRoot, std::string targetBranch, std::string shelf, std::vector<std::string> files, std::string summary, std::string description);
    static ShelfJobResult RunShelveAndSubmitShelfJob(const std::filesystem::path& repoRoot, std::string targetBranch, std::string shelf, std::vector<std::string> files, std::string summary, std::string description);
    static ShelfJobResult RunSubmitShelfJob(const std::filesystem::path& repoRoot, std::string targetBranch, std::string shelf, std::vector<std::string> files);
    static ShelfJobResult RunSubmitMainJob(const std::filesystem::path& repoRoot, std::string targetBranch, std::vector<std::string> files, std::string summary, std::string description);
    static ShelfJobResult RunPullMainJob(const std::filesystem::path& repoRoot, std::string targetBranch);
    static ShelfJobResult RunDeleteShelfJob(const std::filesystem::path& repoRoot, std::string shelf);
    static ShelfJobResult RunRevertShelfFileJob(const std::filesystem::path& repoRoot, std::string targetBranch, std::string shelf, std::string file);
    static ShelfJobResult RunRevertShelfFilesJob(const std::filesystem::path& repoRoot, std::string targetBranch, std::string shelf, std::vector<std::string> files);
    static ShelfJobResult RunRevertActiveFileJob(const std::filesystem::path& repoRoot, std::string shelf, std::string file);
    static ShelfJobResult RunRevertActiveFilesJob(const std::filesystem::path& repoRoot, std::string shelf, std::vector<std::string> files);
    static ShelfJobResult RunRestoreShelfFileJob(const std::filesystem::path& repoRoot, std::string shelf, std::string file);
    static ShelfJobResult RunRestoreShelfFilesJob(const std::filesystem::path& repoRoot, std::string shelf, std::vector<std::string> files);
    static void RunOpenFileDiffJob(const std::filesystem::path& repoRoot, std::string targetBranch, std::string file);

    void PollAsyncOperations();
    void ApplyRepositorySnapshot(const RepositorySnapshot& snapshot);
    void StartRepositoryLoad(const std::filesystem::path& selectedPath);
    void StartShelfJob(std::string shelf, std::string label, std::future<ShelfJobResult> future);
    bool IsShelfBusy(std::string_view shelf) const;
    std::string ShelfBusyLabel(std::string_view shelf) const;
    void RequestConfirmation(ConfirmationAction action, std::string shelf, std::string file);
    void RequestConfirmation(ConfirmationAction action, std::string shelf, std::vector<std::string> files);
    void DrawConfirmationPopup();
    void ConfirmPendingAction();
    void ClearPendingConfirmation();
    void OpenMainSubmitPopup();
    void DrawMainSubmitPopup();
    void OpenShelvePopup(std::string shelf, bool submitAfterShelve = false);
    void DrawShelvePopup();
    void OpenCreateShelfPopup();
    void DrawCreateShelfPopup();
    void OpenCreateBranchPopup();
    void DrawCreateBranchPopup();
    void DrawAppTitleBar();
    void DrawWorkspaceExplorer();
    void DrawFileChanges();
    void DrawLog();
    void UseSourcePath(const std::filesystem::path& path);
    void DrawDirectory(const std::filesystem::path& path, int depth);
    void DrawFileEntry(const std::filesystem::directory_entry& entry);
    void CheckOutFile(const std::filesystem::path& path);
    void RefreshRepositoryData(bool logCommands = true);
    void RefreshRepositoryDataIfNeeded();
    void RequestRepositoryRefresh();
    void PruneInvalidWorkspaceShelves();
    void DrawShelfList();
    void DrawShelfPanel(const std::string& shelf, bool isMainShelf);
    void DrawShelfFile(const std::string& shelf, const std::vector<std::string>& files, size_t fileIndex);
    void DrawShelfCommittedFile(const std::string& shelf, const std::vector<GitStatusEntry>& files, size_t fileIndex);
    void SelectShelf(std::string_view shelf);
    void ClearSelectedShelf();
    void SetShelfLink(std::string_view shelf, std::string url);
    std::string ShelfLink(std::string_view shelf) const;
    std::vector<GitStatusEntry> ShelfFiles(std::string_view shelf) const;
    std::vector<std::string> MainActiveFiles() const;
    std::vector<std::string> MainSubmittableFiles() const;
    std::optional<std::string> ActiveShelfForFile(std::string_view relativePath) const;
    bool IsFileActiveInShelf(std::string_view relativePath) const;
    bool HasUnshelvedChanges() const;
    bool HasLocalChanges(std::string_view relativePath) const;
    bool IsFileActive(std::string_view relativePath) const;
    bool IsActiveFileSelected(std::string_view shelf, std::string_view file) const;
    std::vector<std::string> SelectedFilesForShelf(std::string_view shelf, std::string_view fallbackFile) const;
    std::string BuildDragPayload(std::string_view shelf, const std::vector<std::string>& files) const;
    std::vector<std::string> ActiveFilesForShelf(std::string_view shelf) const;
    void ClearActiveFileSelection();
    void PruneSelectedActiveFiles();
    void SelectActiveFile(const std::string& shelf, const std::vector<std::string>& files, size_t fileIndex);
    bool IsShelfFileSelected(std::string_view shelf, std::string_view file) const;
    std::vector<std::string> SelectedShelfFilesForShelf(std::string_view shelf, std::string_view fallbackFile) const;
    void SelectShelfFile(const std::string& shelf, const std::vector<GitStatusEntry>& files, size_t fileIndex);
    void MoveCheckedOutFile(std::string_view payload, std::string_view toShelf);
    void RevertCheckedOutFile(const std::string& shelf, const std::string& file);
    void RevertCheckedOutFiles(const std::string& shelf, const std::vector<std::string>& files);
    void RevertCheckedOutFilesConfirmed(const std::string& shelf, const std::vector<std::string>& files);
    void OpenFileDiff(const std::string& file);
    void RevertShelfFile(const std::string& shelf, const std::string& file);
    void RevertShelfFiles(const std::string& shelf, const std::vector<std::string>& files);
    void RestoreShelfFile(const std::string& shelf, const std::string& file);
    void RestoreShelfFiles(const std::string& shelf, const std::vector<std::string>& files);
    void RestoreShelfFiles(const std::string& shelf, const std::vector<GitStatusEntry>& files);
    void CreateShelfFromInput();
    void CreateTargetBranchFromInput();
    void SelectTargetBranch(std::string branch);
    void ShelveShelf(const std::string& shelf);
    void ShelveAndSubmitShelf(const std::string& shelf);
    void SubmitShelf(const std::string& shelf);
    void SubmitMainFromPopup();
    void PullMain();
    void DeleteShelf(const std::string& shelf);
    void OpenShelfLink(std::string_view shelf);
    std::string RelativePath(const std::filesystem::path& path) const;
    std::string FileLabel(const std::filesystem::path& path) const;
    static std::string PathLabel(const std::filesystem::path& path);

    const StdoutLog* m_stdoutLog = nullptr;
    Window* m_window = nullptr;
    std::array<char, 1024> m_sourcePathInput = {};
    std::array<char, 128> m_newShelfNameInput = {};
    std::array<char, 128> m_newBranchNameInput = {};
    std::array<char, 256> m_mainSubmitSummaryInput = {};
    std::array<char, 2048> m_mainSubmitDescriptionInput = {};
    std::array<char, 256> m_shelveSummaryInput = {};
    std::array<char, 2048> m_shelveDescriptionInput = {};
    std::filesystem::path m_sourcePath;
    bool m_hasSourcePath = false;
    std::string m_sourcePathError;
    std::string m_selectedBranch;
    std::string m_currentGitBranch;
    std::string m_targetBranch = "main";
    bool m_mainRemoteAvailable = false;
    int m_mainBehindCount = 0;
    std::vector<ShelfLinkEntry> m_shelfLinks;
    std::vector<ShelfCommittedFileList> m_shelfFiles;
    std::optional<GitRepository> m_repository;
    ShelfService m_shelfService;
    WorkspaceState m_workspaceState;
    std::vector<std::string> m_shelves;
    std::vector<std::string> m_branches;
    std::vector<GitStatusEntry> m_statusEntries;
    std::chrono::steady_clock::time_point m_lastRefreshTime = {};
    std::optional<std::future<RepositorySnapshot>> m_repositoryLoadFuture;
    std::optional<std::future<RepositorySnapshot>> m_refreshFuture;
    std::vector<ShelfJob> m_shelfJobs;
    uint64_t m_refreshGeneration = 0;
    uint64_t m_gitMutationGeneration = 0;
    bool m_refreshAfterJobs = false;
    ConfirmationAction m_confirmationAction = ConfirmationAction::None;
    std::string m_confirmationShelf;
    std::string m_confirmationFile;
    std::vector<std::string> m_confirmationFiles;
    bool m_openConfirmationPopup = false;
    bool m_openMainSubmitPopup = false;
    bool m_openShelvePopup = false;
    bool m_shelveSubmitAfter = false;
    bool m_openCreateShelfPopup = false;
    bool m_openCreateBranchPopup = false;
    std::string m_mainSubmitError;
    std::string m_shelveError;
    std::string m_shelveShelf;
    std::string m_createShelfError;
    std::string m_createBranchError;
    std::string m_selectedActiveShelf;
    std::vector<std::string> m_selectedActiveFiles;
    size_t m_lastSelectedActiveFileIndex = 0;
    bool m_hasLastSelectedActiveFileIndex = false;
    std::string m_selectedShelfFileShelf;
    std::vector<std::string> m_selectedShelfFiles;
    size_t m_lastSelectedShelfFileIndex = 0;
    bool m_hasLastSelectedShelfFileIndex = false;
    bool m_logAutoScroll = true;
};
}
