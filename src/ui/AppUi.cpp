#include "ui/AppUi.h"

#include "log/StdoutLog.h"
#include "platform/UrlLauncher.h"
#include "ui/widgets/Widgets.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <system_error>
#include <vector>

namespace p4vgit
{
AppUi::AppUi()
{
    const std::filesystem::path currentPath = std::filesystem::current_path();
    const std::string currentPathText = currentPath.string();
    currentPathText.copy(m_sourcePathInput.data(), std::min(currentPathText.size(), m_sourcePathInput.size() - 1));
}

void AppUi::SetStdoutLog(const StdoutLog* stdoutLog)
{
    m_stdoutLog = stdoutLog;
}

void AppUi::Draw()
{
    RefreshRepositoryDataIfNeeded();

    constexpr std::array defaultLayout = {
        ui::widgets::DockspaceDefaultLayout{ "Log", ui::widgets::DockspaceSide::Down, 30.0f },
        ui::widgets::DockspaceDefaultLayout{ "Workspace Explorer", ui::widgets::DockspaceSide::Left, 24.0f },
        ui::widgets::DockspaceDefaultLayout{ "File Changes", ui::widgets::DockspaceSide::Center, 100.0f },
    };

    ui::widgets::DrawDockspace(defaultLayout);

    DrawWorkspaceExplorer();
    DrawFileChanges();
    DrawLog();
}

void AppUi::DrawWorkspaceExplorer()
{
    if (ui::widgets::BeginWindow("Workspace Explorer"))
    {
        ui::widgets::DrawWindowHeader("Workspace Explorer");
        ui::widgets::InputText("Source Folder", m_sourcePathInput.data(), m_sourcePathInput.size());

        if (ui::widgets::Button("Use Folder"))
            UseSourcePath(m_sourcePathInput.data());

        ui::widgets::SameLine();
        if (ui::widgets::Button("Use Current Directory"))
            UseSourcePath(std::filesystem::current_path());

        if (!m_sourcePathError.empty())
            ui::widgets::Text(m_sourcePathError);

        ui::widgets::Separator();

        if (m_hasSourcePath)
        {
            ui::widgets::Text("Repo: " + m_sourcePath.string());
            ui::widgets::BeginScrollRegion("WorkspaceExplorerScroll");
            DrawDirectory(m_sourcePath, 0);
            ui::widgets::EndScrollRegion();
        }
        else
        {
            ui::widgets::Text("Select a Git repository folder to populate the explorer.");
        }
    }
    ui::widgets::EndWindow();
}

void AppUi::DrawFileChanges()
{
    if (ui::widgets::BeginWindow("File Changes"))
    {
        ui::widgets::DrawWindowHeader("File Changes");

        if (!m_hasSourcePath || !m_repository.has_value())
        {
            ui::widgets::Text("Select a Git repository folder to inspect changes.");
            ui::widgets::EndWindow();
            return;
        }

        ui::widgets::InputText("New Shelf", m_newShelfNameInput.data(), m_newShelfNameInput.size());
        if (ui::widgets::Button("Create Shelf"))
            CreateShelfFromInput();

        ui::widgets::SameLine();
        if (ui::widgets::Button("Refresh"))
            RefreshRepositoryData();

        ui::widgets::Separator();

        DrawShelfList();
    }
    ui::widgets::EndWindow();
}

void AppUi::DrawLog()
{
    if (ui::widgets::BeginWindow("Log"))
    {
        ui::widgets::DrawWindowHeader("Log");
        ui::widgets::BeginScrollRegion("LogScroll");

        if (m_stdoutLog != nullptr)
        {
            const std::vector<std::string> lines = m_stdoutLog->Lines();
            for (const std::string& line : lines)
                ui::widgets::Text(line);
        }

        const bool userScrolledLog = ui::widgets::DidUserScrollCurrentRegion();
        const bool logIsAtBottom = ui::widgets::IsCurrentScrollRegionAtBottom();
        if (userScrolledLog && !logIsAtBottom)
            m_logAutoScroll = false;
        if (logIsAtBottom)
            m_logAutoScroll = true;
        if (m_logAutoScroll)
            ui::widgets::ScrollCurrentRegionToBottom();

        ui::widgets::EndScrollRegion();
    }
    ui::widgets::EndWindow();
}

void AppUi::UseSourcePath(const std::filesystem::path& path)
{
    std::error_code error;
    const std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(path, error);
    const std::filesystem::path selectedPath = error ? path : canonicalPath;

    if (!std::filesystem::exists(selectedPath) || !std::filesystem::is_directory(selectedPath))
    {
        m_sourcePathError = "Source folder does not exist or is not a directory.";
        m_hasSourcePath = false;
        std::cout << "Failed to select source folder: " << selectedPath.string() << '\n';
        return;
    }

    m_repository = GitRepository::Discover(selectedPath);
    if (!m_repository.has_value())
    {
        m_sourcePathError = "Source folder is not inside a Git repository.";
        m_hasSourcePath = false;
        m_shelfService.SetRepository(nullptr);
        std::cout << "Failed to discover Git repository from: " << selectedPath.string() << '\n';
        return;
    }

    m_sourcePath = m_repository->Root();
    m_hasSourcePath = true;
    m_sourcePathError.clear();
    m_shelfService.SetRepository(&m_repository.value());
    m_workspaceState.Load(m_sourcePath);

    const std::string pathText = m_sourcePath.string();
    std::fill(m_sourcePathInput.begin(), m_sourcePathInput.end(), '\0');
    pathText.copy(m_sourcePathInput.data(), std::min(pathText.size(), m_sourcePathInput.size() - 1));

    if (!m_workspaceState.ActiveShelf().empty())
        m_selectedBranch = m_workspaceState.ActiveShelf();
    else
        m_selectedBranch = "main";

    RefreshRepositoryData();

    std::cout << "Selected Git repository: " << m_sourcePath.string() << '\n';
}

void AppUi::DrawDirectory(const std::filesystem::path& path, int depth)
{
    constexpr int maxDepth = 5;
    constexpr size_t maxEntriesPerDirectory = 200;

    std::error_code error;
    std::vector<std::filesystem::directory_entry> entries;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(path, error))
    {
        if (error)
            break;
        if (entry.path().filename() == ".git")
            continue;

        entries.push_back(entry);
        if (entries.size() >= maxEntriesPerDirectory)
            break;
    }

    std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs) {
        const bool lhsDirectory = lhs.is_directory();
        const bool rhsDirectory = rhs.is_directory();
        if (lhsDirectory != rhsDirectory)
            return lhsDirectory > rhsDirectory;

        return lhs.path().filename().string() < rhs.path().filename().string();
    });

    for (const std::filesystem::directory_entry& entry : entries)
    {
        const std::filesystem::path entryPath = entry.path();
        const bool isDirectory = entry.is_directory(error);
        if (isDirectory && depth < maxDepth)
        {
            if (ui::widgets::BeginTreeNode(PathLabel(entryPath)))
            {
                DrawDirectory(entryPath, depth + 1);
                ui::widgets::EndTreeNode();
            }
        }
        else if (isDirectory)
        {
            ui::widgets::TreeLeaf(PathLabel(entryPath));
        }
        else
        {
            DrawFileEntry(entry);
        }
    }
}

void AppUi::DrawFileEntry(const std::filesystem::directory_entry& entry)
{
    const std::string relativePath = RelativePath(entry.path());
    ui::widgets::TreeLeaf(FileLabel(entry.path()));

    if (ui::widgets::BeginContextMenuForLastItem())
    {
        for (const std::string& shelf : m_shelves)
        {
            if (ui::widgets::MenuItem("Check out to " + shelf, true))
            {
                SelectShelf(shelf);
                CheckOutFile(entry.path());
            }
        }
        if (m_shelves.empty())
            ui::widgets::Text("Create a shelf before checking out files.");
        ui::widgets::EndContextMenu();
    }
}

void AppUi::CheckOutFile(const std::filesystem::path& path)
{
    if (!HasWritableShelfSelected())
        return;

    const std::string relativePath = RelativePath(path);
    m_workspaceState.SetActiveShelf(m_selectedBranch);
    m_workspaceState.CheckOut(relativePath);
    m_workspaceState.Save();

    std::cout << "Checked out " << relativePath << " into " << m_selectedBranch << '\n';
}

void AppUi::RefreshRepositoryData(bool logCommands)
{
    if (!m_repository.has_value())
        return;

    m_workspaceState.Load(m_sourcePath);
    m_shelves = m_shelfService.Shelves(logCommands);
    m_statusEntries = m_repository->Status(logCommands);
    m_lastRefreshTime = std::chrono::steady_clock::now();

    if (m_selectedBranch != "main")
    {
        const bool branchListed = std::find(m_shelves.begin(), m_shelves.end(), m_selectedBranch) != m_shelves.end();
        if (!branchListed || !m_shelfService.IsShelfValid(m_selectedBranch, logCommands))
        {
            if (logCommands)
                std::cout << "Shelf is no longer valid: " << m_selectedBranch << '\n';
            ClearSelectedShelf();
            return;
        }
    }

    RefreshPullRequestLinks(logCommands);
}

void AppUi::RefreshRepositoryDataIfNeeded()
{
    if (!m_repository.has_value())
        return;

    const auto now = std::chrono::steady_clock::now();
    if (m_lastRefreshTime.time_since_epoch().count() == 0 || now - m_lastRefreshTime >= std::chrono::seconds(3))
        RefreshRepositoryData(false);
}

void AppUi::DrawShelfList()
{
    ui::widgets::Text("Shelves");
    if (m_shelves.empty())
    {
        ui::widgets::Text("No shelves yet.");
        return;
    }

    for (const std::string& shelf : m_shelves)
        DrawShelfPanel(shelf);
}

void AppUi::DrawShelfPanel(const std::string& shelf)
{
    const bool open = ui::widgets::BeginTreeNode(shelf);
    if (const std::optional<std::string> payload = ui::widgets::AcceptDragDropPayload("p4v-git-file"))
        MoveCheckedOutFile(*payload, shelf);

    if (!open)
        return;

    if (ui::widgets::Button("Shelve"))
        ShelveShelf(shelf);

    ui::widgets::SameLine();
    if (ui::widgets::Button("Submit"))
        SubmitShelf(shelf);

    ui::widgets::SameLine();
    if (ui::widgets::Button("Delete Shelf"))
        DeleteShelf(shelf);

    const std::string link = PullRequestLink(shelf);
    if (!link.empty())
    {
        ui::widgets::Text("PR");
        if (ui::widgets::Link(link))
            OpenPullRequestLink(shelf);
    }

    ui::widgets::Separator();
    ui::widgets::Text("Active changes");

    const std::vector<std::string>& files = m_workspaceState.CheckedOutFiles(shelf);
    if (files.empty())
    {
        ui::widgets::Text("No active changes assigned to this shelf.");
    }
    else
    {
        const std::vector<std::string> filesCopy = files;
        for (const std::string& file : filesCopy)
            DrawShelfFile(shelf, file);
    }

    ui::widgets::EndTreeNode();
}

void AppUi::DrawShelfFile(const std::string& shelf, const std::string& file)
{
    ui::widgets::Text(file);
    ui::widgets::DragDropSource("p4v-git-file", shelf + "\n" + file, file);

    ui::widgets::SameLine();
    if (ui::widgets::Button("Remove##" + shelf + "/" + file))
        RemoveCheckedOutFile(shelf, file);
}

void AppUi::SelectShelf(std::string_view shelf)
{
    if (shelf.empty() || shelf == "main")
    {
        ClearSelectedShelf();
        return;
    }

    if (!m_shelfService.IsShelfValid(shelf, true))
    {
        std::cout << "Cannot open shelf because it no longer exists: " << shelf << '\n';
        ClearSelectedShelf();
        RefreshRepositoryData();
        return;
    }

    m_selectedBranch = std::string(shelf);
    m_workspaceState.SetActiveShelf(m_selectedBranch);
    m_workspaceState.Save();
    RefreshPullRequestLinks(true);
}

void AppUi::ClearSelectedShelf()
{
    m_selectedBranch = "main";
    m_pullRequestLinks.clear();
    m_workspaceState.SetActiveShelf({});
    m_workspaceState.Save();
}

void AppUi::RefreshPullRequestLinks(bool logCommands)
{
    if (m_shelves.empty())
    {
        m_pullRequestLinks.clear();
        return;
    }

    std::vector<ShelfPullRequestLink> refreshedLinks;
    for (const std::string& shelf : m_shelves)
    {
        const std::string link = m_shelfService.FindPullRequest(shelf, logCommands);
        if (!link.empty())
            refreshedLinks.push_back({ shelf, link });
    }

    m_pullRequestLinks = std::move(refreshedLinks);
}

void AppUi::SetPullRequestLink(std::string_view shelf, std::string url)
{
    for (ShelfPullRequestLink& link : m_pullRequestLinks)
    {
        if (link.shelf == shelf)
        {
            link.url = std::move(url);
            return;
        }
    }

    if (!url.empty())
        m_pullRequestLinks.push_back({ std::string(shelf), std::move(url) });
}

std::string AppUi::PullRequestLink(std::string_view shelf) const
{
    for (const ShelfPullRequestLink& link : m_pullRequestLinks)
    {
        if (link.shelf == shelf)
            return link.url;
    }

    return {};
}

void AppUi::MoveCheckedOutFile(std::string_view payload, std::string_view toShelf)
{
    const size_t separator = payload.find('\n');
    if (separator == std::string_view::npos)
        return;

    const std::string_view fromShelf = payload.substr(0, separator);
    const std::string_view file = payload.substr(separator + 1);
    m_workspaceState.MoveCheckedOutFile(fromShelf, toShelf, file);
    m_workspaceState.Save();
    std::cout << "Moved " << file << " from " << fromShelf << " to " << toShelf << '\n';
}

void AppUi::RemoveCheckedOutFile(const std::string& shelf, const std::string& file)
{
    m_shelfService.RemoveFileFromShelf(shelf, file);
    m_workspaceState.RemoveCheckedOutFile(shelf, file);
    m_workspaceState.Save();
    std::cout << "Removed " << file << " from " << shelf << '\n';
}

void AppUi::CreateShelfFromInput()
{
    if (!m_repository.has_value())
        return;

    const std::string shelfName = m_newShelfNameInput.data();
    if (shelfName.empty())
        return;

    const std::string branch = m_shelfService.MakeShelfBranch(shelfName);
    if (m_shelfService.CreateShelf(shelfName))
    {
        SelectShelf(branch);
        std::fill(m_newShelfNameInput.begin(), m_newShelfNameInput.end(), '\0');
        RefreshRepositoryData();
        std::cout << "Created shelf: " << branch << '\n';
    }
}

void AppUi::ShelveShelf(const std::string& shelf)
{
    if (!m_shelfService.IsShelfValid(shelf, true))
        return;

    const std::string pullRequestLink = m_shelfService.ShelveFilesAndOpenPullRequest(shelf, m_workspaceState.CheckedOutFiles(shelf));
    if (!pullRequestLink.empty())
    {
        SetPullRequestLink(shelf, pullRequestLink);
        std::cout << "Shelved files and updated PR: " << pullRequestLink << '\n';
        RefreshRepositoryData();
    }
}

void AppUi::SubmitShelf(const std::string& shelf)
{
    if (!m_shelfService.IsShelfValid(shelf, true))
        return;

    const ShelfSubmitResult submitResult = m_shelfService.SubmitShelfAsPullRequest(shelf);
    if (!submitResult.pullRequestUrl.empty())
    {
        SetPullRequestLink(shelf, submitResult.pullRequestUrl);
        std::cout << "Submit requested for PR: " << submitResult.pullRequestUrl << '\n';
        if (submitResult.merged)
        {
            if (submitResult.branchDeleted)
                std::cout << "Submitted shelf and deleted branch: " << shelf << '\n';
            else
                std::cout << "Submitted shelf, but branch deletion did not fully complete: " << shelf << '\n';
            if (m_selectedBranch == shelf)
                ClearSelectedShelf();
            m_workspaceState.RemoveShelf(shelf);
            m_workspaceState.Save();
        }
        RefreshRepositoryData();
    }
}

void AppUi::OpenPullRequestLink(std::string_view shelf)
{
    const std::string pullRequestLink = PullRequestLink(shelf);
    if (pullRequestLink.empty())
        return;

    std::cout << "Open PR: " << pullRequestLink << '\n';
    if (!OpenUrl(pullRequestLink))
        std::cout << "Failed to open PR link in browser\n";
}

void AppUi::DeleteShelf(const std::string& shelf)
{
    if (!m_shelfService.IsShelfValid(shelf, true))
        return;

    if (m_shelfService.DeleteShelf(shelf, true))
    {
        std::cout << "Deleted shelf: " << shelf << '\n';
        if (m_selectedBranch == shelf)
            ClearSelectedShelf();
        m_workspaceState.RemoveShelf(shelf);
        m_workspaceState.Save();
        RefreshRepositoryData();
    }
}

bool AppUi::HasWritableShelfSelected() const
{
    return m_hasSourcePath && m_selectedBranch != "main" && !m_selectedBranch.empty();
}

std::string AppUi::RelativePath(const std::filesystem::path& path) const
{
    std::error_code error;
    const std::filesystem::path relative = std::filesystem::relative(path, m_sourcePath, error);
    if (error)
        return path.string();

    return relative.generic_string();
}

std::string AppUi::FileLabel(const std::filesystem::path& path) const
{
    const std::string relativePath = RelativePath(path);
    std::string label = PathLabel(path);
    if (m_workspaceState.IsCheckedOut(relativePath))
        label += " [checked out]";

    return label;
}

std::string AppUi::PathLabel(const std::filesystem::path& path)
{
    const std::string filename = path.filename().string();
    if (!filename.empty())
        return filename;

    return path.string();
}
}
