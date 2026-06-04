#pragma once

#include <array>
#include <chrono>
#include <filesystem>
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

class AppUi
{
public:
    AppUi();

    void SetStdoutLog(const StdoutLog* stdoutLog);
    void Draw();

private:
    void DrawWorkspaceExplorer();
    void DrawFileChanges();
    void DrawLog();
    void UseSourcePath(const std::filesystem::path& path);
    void DrawDirectory(const std::filesystem::path& path, int depth);
    void DrawFileEntry(const std::filesystem::directory_entry& entry);
    void CheckOutFile(const std::filesystem::path& path);
    void RefreshRepositoryData(bool logCommands = true);
    void RefreshRepositoryDataIfNeeded();
    void DrawShelfList();
    void DrawShelfPanel(const std::string& shelf);
    void DrawShelfFile(const std::string& shelf, const std::string& file);
    void SelectShelf(std::string_view shelf);
    void ClearSelectedShelf();
    void RefreshPullRequestLinks(bool logCommands);
    void SetPullRequestLink(std::string_view shelf, std::string url);
    std::string PullRequestLink(std::string_view shelf) const;
    void MoveCheckedOutFile(std::string_view payload, std::string_view toShelf);
    void RemoveCheckedOutFile(const std::string& shelf, const std::string& file);
    void CreateShelfFromInput();
    void ShelveShelf(const std::string& shelf);
    void SubmitShelf(const std::string& shelf);
    void DeleteShelf(const std::string& shelf);
    void OpenPullRequestLink(std::string_view shelf);
    bool HasWritableShelfSelected() const;
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
    std::vector<ShelfPullRequestLink> m_pullRequestLinks;
    std::optional<GitRepository> m_repository;
    ShelfService m_shelfService;
    WorkspaceState m_workspaceState;
    std::vector<std::string> m_shelves;
    std::vector<GitStatusEntry> m_statusEntries;
    std::chrono::steady_clock::time_point m_lastRefreshTime = {};
    bool m_logAutoScroll = true;
};
}
