#pragma once

#include <array>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "git/GitRepository.h"
#include "git/ShelfService.h"
#include "git/WorkspaceState.h"

namespace p4vgit
{
class StdoutLog;

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
    void RefreshRepositoryData();
    void RefreshRepositoryDataIfNeeded();
    void DrawShelfSelector();
    void CreateShelfFromInput();
    void ShelveSelectedFiles();
    void OpenSelectedPullRequest();
    void SubmitSelectedShelf();
    void DeleteSelectedShelf();
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
    std::string m_selectedBranch = "main";
    std::string m_pullRequestLink;
    std::optional<GitRepository> m_repository;
    ShelfService m_shelfService;
    WorkspaceState m_workspaceState;
    std::vector<std::string> m_shelves;
    std::vector<GitStatusEntry> m_statusEntries;
    std::chrono::steady_clock::time_point m_lastRefreshTime = {};
};
}
