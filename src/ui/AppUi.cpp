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

        DrawShelfSelector();

        ui::widgets::InputText("New Shelf", m_newShelfNameInput.data(), m_newShelfNameInput.size());
        if (ui::widgets::Button("Create Shelf"))
            CreateShelfFromInput();

        ui::widgets::SameLine();
        if (ui::widgets::Button("Refresh"))
            RefreshRepositoryData();

        ui::widgets::Separator();

        ui::widgets::Text("Checked out files");
        if (m_workspaceState.CheckedOutFiles().empty())
            ui::widgets::Text("No files checked out in this workspace.");
        for (const std::string& file : m_workspaceState.CheckedOutFiles())
            ui::widgets::Text(file);

        ui::widgets::Separator();

        const bool shelfSelected = HasWritableShelfSelected();
        if (ui::widgets::Button("Shelve") && shelfSelected)
            ShelveSelectedFiles();

        ui::widgets::SameLine();
        if (ui::widgets::Button("Open PR") && shelfSelected)
            OpenSelectedPullRequest();

        ui::widgets::SameLine();
        if (ui::widgets::Button("Submit") && shelfSelected)
            SubmitSelectedShelf();

        ui::widgets::SameLine();
        if (ui::widgets::Button("Delete Shelf") && shelfSelected)
            DeleteSelectedShelf();

        if (!shelfSelected)
            ui::widgets::Text("Select or create a shelf branch to enable shelf actions.");

        if (!m_pullRequestLink.empty())
            ui::widgets::Text("PR: " + m_pullRequestLink);

        ui::widgets::Separator();

        ui::widgets::Text("Workspace status");
        if (m_statusEntries.empty())
            ui::widgets::Text("No Git status entries.");
        for (const GitStatusEntry& entry : m_statusEntries)
            ui::widgets::Text(entry.status + "  " + entry.path);
    }
    ui::widgets::EndWindow();
}

void AppUi::DrawLog()
{
    if (ui::widgets::BeginWindow("Log"))
    {
        ui::widgets::DrawWindowHeader("Log");
        ui::widgets::BeginScrollRegion("LogScroll", true);

        if (m_stdoutLog != nullptr)
        {
            const std::vector<std::string> lines = m_stdoutLog->Lines();
            for (const std::string& line : lines)
                ui::widgets::Text(line);
        }

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

    RefreshRepositoryData();

    if (!m_workspaceState.ActiveShelf().empty())
        m_selectedBranch = m_workspaceState.ActiveShelf();

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
        if (ui::widgets::MenuItem("Check out", HasWritableShelfSelected()))
            CheckOutFile(entry.path());
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

void AppUi::RefreshRepositoryData()
{
    if (!m_repository.has_value())
        return;

    m_workspaceState.Load(m_sourcePath);
    m_shelves = m_shelfService.Shelves();
    m_statusEntries = m_repository->Status();
    m_lastRefreshTime = std::chrono::steady_clock::now();

    if (m_selectedBranch != "main" && std::find(m_shelves.begin(), m_shelves.end(), m_selectedBranch) == m_shelves.end())
        m_selectedBranch = "main";
}

void AppUi::RefreshRepositoryDataIfNeeded()
{
    if (!m_repository.has_value())
        return;

    const auto now = std::chrono::steady_clock::now();
    if (m_lastRefreshTime.time_since_epoch().count() == 0 || now - m_lastRefreshTime >= std::chrono::seconds(3))
        RefreshRepositoryData();
}

void AppUi::DrawShelfSelector()
{
    if (ui::widgets::BeginCombo("Branch / Shelf", m_selectedBranch))
    {
        if (ui::widgets::Selectable("main", m_selectedBranch == "main"))
            m_selectedBranch = "main";

        for (const std::string& shelf : m_shelves)
        {
            if (ui::widgets::Selectable(shelf, shelf == m_selectedBranch))
            {
                m_selectedBranch = shelf;
                m_workspaceState.SetActiveShelf(shelf);
                m_workspaceState.Save();
            }
        }

        ui::widgets::EndCombo();
    }
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
        m_selectedBranch = branch;
        m_workspaceState.SetActiveShelf(branch);
        m_workspaceState.Save();
        std::fill(m_newShelfNameInput.begin(), m_newShelfNameInput.end(), '\0');
        RefreshRepositoryData();
        std::cout << "Created shelf: " << branch << '\n';
    }
}

void AppUi::ShelveSelectedFiles()
{
    if (!HasWritableShelfSelected())
        return;

    m_pullRequestLink = m_shelfService.ShelveFilesAndOpenPullRequest(m_selectedBranch, m_workspaceState.CheckedOutFiles());
    if (!m_pullRequestLink.empty())
    {
        std::cout << "Shelved files and opened PR link: " << m_pullRequestLink << '\n';
        RefreshRepositoryData();
    }
}

void AppUi::OpenSelectedPullRequest()
{
    if (!HasWritableShelfSelected())
        return;

    m_pullRequestLink = m_shelfService.EnsurePullRequest(m_selectedBranch);
    if (!m_pullRequestLink.empty())
    {
        std::cout << "Open PR: " << m_pullRequestLink << '\n';
        if (!OpenUrl(m_pullRequestLink))
            std::cout << "Failed to open PR link in browser\n";
    }
}

void AppUi::SubmitSelectedShelf()
{
    if (!HasWritableShelfSelected())
        return;

    m_pullRequestLink = m_shelfService.SubmitShelfAsPullRequest(m_selectedBranch);
    if (!m_pullRequestLink.empty())
    {
        std::cout << "Submit shelf through PR: " << m_pullRequestLink << '\n';
        if (!OpenUrl(m_pullRequestLink))
            std::cout << "Failed to open PR link in browser\n";
        RefreshRepositoryData();
    }
}

void AppUi::DeleteSelectedShelf()
{
    if (!HasWritableShelfSelected())
        return;

    const std::string deletedShelf = m_selectedBranch;
    if (m_shelfService.DeleteShelf(deletedShelf, true))
    {
        std::cout << "Deleted shelf: " << deletedShelf << '\n';
        m_selectedBranch = "main";
        m_pullRequestLink.clear();
        m_workspaceState.SetActiveShelf({});
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
