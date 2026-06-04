#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace p4vgit
{
class WorkspaceState
{
public:
    bool Load(const std::filesystem::path& repoRoot);
    bool Save() const;

    const std::string& ActiveShelf() const { return m_activeShelf; }
    void SetActiveShelf(std::string shelf);

    const std::vector<std::string>& CheckedOutFiles() const { return m_checkedOutFiles; }
    bool IsCheckedOut(std::string_view relativePath) const;
    void CheckOut(std::string relativePath);
    void ClearCheckedOutFiles();

private:
    static std::string Escape(std::string_view text);
    static std::string Unescape(std::string_view text);
    static std::vector<std::string> ReadJsonStringArray(std::string_view json, std::string_view key);
    static std::string ReadJsonString(std::string_view json, std::string_view key);

    std::filesystem::path m_statePath;
    std::string m_activeShelf;
    std::vector<std::string> m_checkedOutFiles;
};
}
