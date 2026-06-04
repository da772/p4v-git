#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace p4vgit
{
struct GitCommandResult
{
    int exitCode = -1;
    std::string output;

    bool Succeeded() const { return exitCode == 0; }
};

struct GitStatusEntry
{
    std::string path;
    std::string status;
};

class GitRepository
{
public:
    GitRepository() = default;
    explicit GitRepository(std::filesystem::path root);

    bool IsOpen() const { return !m_root.empty(); }
    const std::filesystem::path& Root() const { return m_root; }

    static std::optional<GitRepository> Discover(const std::filesystem::path& selectedPath);

    GitCommandResult Run(std::string_view arguments) const;
    std::string CurrentBranch() const;
    std::string UserName() const;
    std::string RemoteUrl(std::string_view remote) const;
    std::filesystem::path GitDir() const;
    std::vector<std::string> LocalBranches() const;
    std::vector<GitStatusEntry> Status() const;
    bool BranchExists(std::string_view branch) const;

private:
    static std::string Trim(std::string text);
    static std::string Quote(const std::filesystem::path& path);
    static std::string Quote(std::string_view text);

    std::filesystem::path m_root;
};
}
