#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace p4vgit
{
struct ShelfWorkspaceFiles
{
    std::string shelf;
    std::vector<std::string> files;
};

class WorkspaceState
{
public:
    bool Load(const std::filesystem::path& repoRoot);
    bool Save() const;

    const std::string& ActiveShelf() const { return m_activeShelf; }
    void SetActiveShelf(std::string shelf);
    const std::string& TargetBranch() const { return m_targetBranch; }
    void SetTargetBranch(std::string branch);

    const std::vector<ShelfWorkspaceFiles>& Shelves() const { return m_shelfFiles; }
    const std::vector<std::string>& CheckedOutFiles() const;
    const std::vector<std::string>& CheckedOutFiles(std::string_view shelf) const;
    bool IsCheckedOut(std::string_view relativePath) const;
    bool IsCheckedOut(std::string_view shelf, std::string_view relativePath) const;
    void CheckOut(std::string relativePath);
    void CheckOut(std::string_view shelf, std::string relativePath);
    void MoveCheckedOutFile(std::string_view fromShelf, std::string_view toShelf, std::string_view relativePath);
    void RevertCheckedOutFile(std::string_view shelf, std::string_view relativePath);
    void DeleteShelf(std::string_view shelf);
    void ClearCheckedOutFiles();

private:
    static std::string Escape(std::string_view text);
    static std::string Unescape(std::string_view text);
    static std::vector<std::string> ReadJsonStringArray(std::string_view json, std::string_view key);
    static std::string ReadJsonString(std::string_view json, std::string_view key);
    static std::string ReadObjectString(std::string_view jsonObject, std::string_view key);
    static std::vector<ShelfWorkspaceFiles> ReadShelfFiles(std::string_view json);

    std::vector<std::string>& MutableCheckedOutFiles(std::string_view shelf);

    std::filesystem::path m_statePath;
    std::string m_activeShelf;
    std::string m_targetBranch = "main";
    std::vector<ShelfWorkspaceFiles> m_shelfFiles;
};
}
