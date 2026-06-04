#include "ui/AppUi.h"

#include "log/StdoutLog.h"
#include "platform/UrlLauncher.h"
#include "ui/widgets/Widgets.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <future>
#include <iostream>
#include <system_error>
#include <utility>
#include <vector>

namespace p4vgit
{
RepositorySnapshot AppUi::LoadRepositorySnapshot(const std::filesystem::path& selectedPath, bool discoverRepository, bool logCommands, uint64_t refreshGeneration)
{
    RepositorySnapshot snapshot;
    snapshot.refreshGeneration = refreshGeneration;
    std::optional<GitRepository> discoveredRepository;

    if (discoverRepository)
        discoveredRepository = GitRepository::Discover(selectedPath);
    else
        discoveredRepository = GitRepository(selectedPath);

    if (!discoveredRepository.has_value())
    {
        snapshot.error = "Source folder is not inside a Git repository.";
        return snapshot;
    }

    GitRepository repository = discoveredRepository.value();
    ShelfService shelfService;
    shelfService.SetRepository(&repository);

    snapshot.discovered = true;
    snapshot.root = repository.Root();
    snapshot.workspaceState.Load(snapshot.root);
    snapshot.shelves = shelfService.Shelves(logCommands);
    snapshot.statusEntries = repository.Status(logCommands);
    snapshot.currentBranch = repository.CurrentBranch(logCommands);
    const MainSyncStatus mainSyncStatus = shelfService.RefreshMain(logCommands);
    snapshot.mainRemoteAvailable = mainSyncStatus.remoteAvailable;
    snapshot.mainBehindCount = mainSyncStatus.behindCount;

    bool stateChanged = false;
    const std::vector<ShelfWorkspaceFiles> workspaceShelves = snapshot.workspaceState.Shelves();
    for (const ShelfWorkspaceFiles& shelf : workspaceShelves)
    {
        if (shelf.shelf == "main")
            continue;

        if (std::find(snapshot.shelves.begin(), snapshot.shelves.end(), shelf.shelf) == snapshot.shelves.end())
        {
            snapshot.workspaceState.DeleteShelf(shelf.shelf);
            stateChanged = true;
        }
    }
    if (stateChanged)
        snapshot.workspaceState.Save();

    for (const std::string& shelf : snapshot.shelves)
    {
        const std::string link = shelfService.FindShelfLink(shelf, logCommands);
        if (!link.empty())
            snapshot.shelfLinks.push_back({ shelf, link });

        snapshot.shelfFiles.push_back({ shelf, shelfService.ShelfFiles(shelf, logCommands) });
    }

    snapshot.succeeded = true;
    return snapshot;
}

ShelfJobResult AppUi::RunCreateShelfJob(const std::filesystem::path& repoRoot, std::string shelfName)
{
    GitRepository repository(repoRoot);
    ShelfService shelfService;
    shelfService.SetRepository(&repository);

    ShelfJobResult result;
    result.shelf = shelfService.MakeShelfBranch(shelfName);
    result.succeeded = shelfService.CreateShelf(shelfName);
    result.selectShelf = result.succeeded;
    return result;
}

ShelfJobResult AppUi::RunShelveShelfJob(const std::filesystem::path& repoRoot, std::string shelf, std::vector<std::string> files)
{
    GitRepository repository(repoRoot);
    ShelfService shelfService;
    shelfService.SetRepository(&repository);

    ShelfJobResult result;
    result.shelf = std::move(shelf);
    result.shelfUrl = shelfService.ShelveFilesAndEnsureShelfLink(result.shelf, files);
    result.succeeded = !result.shelfUrl.empty();
    return result;
}

ShelfJobResult AppUi::RunSubmitShelfJob(const std::filesystem::path& repoRoot, std::string shelf, std::vector<std::string> files)
{
    GitRepository repository(repoRoot);
    ShelfService shelfService;
    shelfService.SetRepository(&repository);

    ShelfJobResult result;
    result.shelf = std::move(shelf);
    result.files = std::move(files);
    result.submit = shelfService.SubmitShelf(result.shelf, result.files);
    result.shelfUrl = result.submit.shelfUrl;
    result.succeeded = result.submit.merged;
    result.deleteShelfState = result.submit.merged;
    return result;
}

ShelfJobResult AppUi::RunSubmitMainJob(const std::filesystem::path& repoRoot, std::vector<std::string> files, std::string summary, std::string description)
{
    GitRepository repository(repoRoot);
    ShelfService shelfService;
    shelfService.SetRepository(&repository);

    ShelfJobResult result;
    result.shelf = "main";
    result.files = std::move(files);
    result.succeeded = shelfService.SubmitMain(result.files, summary, description);
    result.clearMainActiveFiles = result.succeeded;
    return result;
}

ShelfJobResult AppUi::RunPullMainJob(const std::filesystem::path& repoRoot)
{
    GitRepository repository(repoRoot);
    ShelfService shelfService;
    shelfService.SetRepository(&repository);

    ShelfJobResult result;
    result.shelf = "main";
    result.succeeded = shelfService.PullMain();
    return result;
}

ShelfJobResult AppUi::RunDeleteShelfJob(const std::filesystem::path& repoRoot, std::string shelf)
{
    GitRepository repository(repoRoot);
    ShelfService shelfService;
    shelfService.SetRepository(&repository);

    ShelfJobResult result;
    result.shelf = std::move(shelf);
    result.succeeded = shelfService.DeleteShelf(result.shelf, true);
    result.deleteShelfState = result.succeeded;
    return result;
}

ShelfJobResult AppUi::RunRevertShelfFileJob(const std::filesystem::path& repoRoot, std::string shelf, std::string file)
{
    GitRepository repository(repoRoot);
    ShelfService shelfService;
    shelfService.SetRepository(&repository);

    ShelfJobResult result;
    result.shelf = std::move(shelf);
    result.file = std::move(file);
    result.succeeded = shelfService.RevertFileFromShelf(result.shelf, result.file);
    result.removeShelfFileState = result.succeeded;
    return result;
}

ShelfJobResult AppUi::RunRevertActiveFileJob(const std::filesystem::path& repoRoot, std::string shelf, std::string file)
{
    GitRepository repository(repoRoot);
    ShelfService shelfService;
    shelfService.SetRepository(&repository);

    ShelfJobResult result;
    result.shelf = std::move(shelf);
    result.file = std::move(file);
    result.succeeded = shelfService.UndoLocalFileChanges(result.file);
    result.revertActiveFileState = result.succeeded;
    return result;
}

ShelfJobResult AppUi::RunRestoreShelfFileJob(const std::filesystem::path& repoRoot, std::string shelf, std::string file)
{
    GitRepository repository(repoRoot);
    ShelfService shelfService;
    shelfService.SetRepository(&repository);

    ShelfJobResult result;
    result.shelf = std::move(shelf);
    result.file = std::move(file);
    result.succeeded = shelfService.RestoreFileFromShelfToWorkingTree(result.shelf, result.file);
    result.addFileState = result.succeeded;
    return result;
}

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

void AppUi::OnWindowFocusGained()
{
    RequestRepositoryRefresh();
}

void AppUi::PollAsyncOperations()
{
    using namespace std::chrono_literals;

    if (m_repositoryLoadFuture.has_value() && m_repositoryLoadFuture->wait_for(0ms) == std::future_status::ready)
    {
        RepositorySnapshot snapshot = m_repositoryLoadFuture->get();
        m_repositoryLoadFuture.reset();

        if (snapshot.succeeded)
            ApplyRepositorySnapshot(snapshot);
        else
        {
            m_sourcePathError = snapshot.error.empty() ? "Failed to load repository." : snapshot.error;
            m_hasSourcePath = false;
            m_repository.reset();
            m_shelfService.SetRepository(nullptr);
            m_mainRemoteAvailable = false;
            m_mainBehindCount = 0;
            std::cout << "Failed to select source folder: " << m_sourcePathError << '\n';
        }
    }

    for (auto job = m_shelfJobs.begin(); job != m_shelfJobs.end();)
    {
        if (job->future.wait_for(0ms) != std::future_status::ready)
        {
            ++job;
            continue;
        }

        ShelfJobResult result = job->future.get();
        const std::string label = job->label;
        job = m_shelfJobs.erase(job);

        if (!result.shelfUrl.empty())
            SetShelfLink(result.shelf, result.shelfUrl);

        if (result.selectShelf)
        {
            m_selectedBranch = result.shelf;
            m_workspaceState.SetActiveShelf(result.shelf);
            m_workspaceState.Save();
        }

        if (result.addFileState)
        {
            m_workspaceState.CheckOut(result.shelf, result.file);
            m_workspaceState.SetActiveShelf(result.shelf);
            m_workspaceState.Save();
        }

        if (result.clearMainActiveFiles)
        {
            for (const std::string& file : result.files)
                m_workspaceState.RevertCheckedOutFile("main", file);
            m_workspaceState.Save();
        }

        if (result.revertActiveFileState)
        {
            m_workspaceState.RevertCheckedOutFile(result.shelf, result.file);
            m_workspaceState.Save();
        }

        if (result.removeShelfFileState)
        {
            for (ShelfCommittedFileList& shelfFiles : m_shelfFiles)
            {
                if (shelfFiles.shelf != result.shelf)
                    continue;

                shelfFiles.files.erase(std::remove_if(shelfFiles.files.begin(), shelfFiles.files.end(), [&result](const GitStatusEntry& file) {
                    return file.path == result.file;
                }), shelfFiles.files.end());
                break;
            }
        }

        if (result.deleteShelfState)
        {
            if (m_selectedBranch == result.shelf)
                ClearSelectedShelf();
            m_workspaceState.DeleteShelf(result.shelf);
            m_workspaceState.Save();
        }

        std::cout << label << (result.succeeded ? " complete" : " failed") << ": " << result.shelf << '\n';
        if (result.refreshAfter)
            RequestRepositoryRefresh();
    }

    if (m_refreshFuture.has_value() && m_refreshFuture->wait_for(0ms) == std::future_status::ready)
    {
        RepositorySnapshot snapshot = m_refreshFuture->get();
        m_refreshFuture.reset();
        if (snapshot.succeeded)
        {
            if (snapshot.refreshGeneration < m_gitMutationGeneration || !m_shelfJobs.empty())
                m_refreshAfterJobs = true;
            else
                ApplyRepositorySnapshot(snapshot);
        }
    }

    if (m_refreshAfterJobs && m_shelfJobs.empty() && !m_refreshFuture.has_value())
    {
        m_refreshAfterJobs = false;
        RefreshRepositoryData(false);
    }
}

void AppUi::ApplyRepositorySnapshot(const RepositorySnapshot& snapshot)
{
    m_sourcePath = snapshot.root;
    m_hasSourcePath = true;
    m_sourcePathError.clear();
    m_repository = GitRepository(snapshot.root);
    m_shelfService.SetRepository(&m_repository.value());
    m_workspaceState = snapshot.workspaceState;
    m_shelves = snapshot.shelves;
    m_statusEntries = snapshot.statusEntries;
    m_currentGitBranch = snapshot.currentBranch;
    m_mainRemoteAvailable = snapshot.mainRemoteAvailable;
    m_mainBehindCount = snapshot.mainBehindCount;
    m_shelfLinks = snapshot.shelfLinks;
    m_shelfFiles = snapshot.shelfFiles;
    m_lastRefreshTime = std::chrono::steady_clock::now();

    if (!m_workspaceState.ActiveShelf().empty())
        m_selectedBranch = m_workspaceState.ActiveShelf();
    else
        m_selectedBranch = "main";

    if (m_selectedBranch != "main" && std::find(m_shelves.begin(), m_shelves.end(), m_selectedBranch) == m_shelves.end())
        ClearSelectedShelf();
}

void AppUi::StartRepositoryLoad(const std::filesystem::path& selectedPath)
{
    if (m_repositoryLoadFuture.has_value())
        return;

    m_sourcePathError.clear();
    m_hasSourcePath = false;
    m_repository.reset();
    m_shelfService.SetRepository(nullptr);
    m_mainRemoteAvailable = false;
    m_mainBehindCount = 0;
    m_repositoryLoadFuture = std::async(std::launch::async, [selectedPath]() {
        return LoadRepositorySnapshot(selectedPath, true, true, 0);
    });
}

void AppUi::StartShelfJob(std::string shelf, std::string label, std::future<ShelfJobResult> future)
{
    if (IsShelfBusy(shelf))
        return;

    ++m_gitMutationGeneration;
    if (m_refreshFuture.has_value())
        m_refreshAfterJobs = true;

    m_shelfJobs.push_back({ std::move(shelf), std::move(label), std::move(future) });
}

bool AppUi::IsShelfBusy(std::string_view shelf) const
{
    return std::any_of(m_shelfJobs.begin(), m_shelfJobs.end(), [shelf](const ShelfJob& job) {
        return job.shelf == shelf;
    });
}

std::string AppUi::ShelfBusyLabel(std::string_view shelf) const
{
    for (const ShelfJob& job : m_shelfJobs)
    {
        if (job.shelf == shelf)
            return job.label;
    }

    return {};
}

void AppUi::RequestConfirmation(ConfirmationAction action, std::string shelf, std::string file)
{
    m_confirmationAction = action;
    m_confirmationShelf = std::move(shelf);
    m_confirmationFile = std::move(file);
    m_openConfirmationPopup = true;
}

void AppUi::DrawConfirmationPopup()
{
    constexpr std::string_view popupId = "Confirm File Operation";
    if (m_openConfirmationPopup)
    {
        ui::widgets::OpenPopup(popupId);
        m_openConfirmationPopup = false;
    }

    if (!ui::widgets::BeginModal(popupId))
        return;

    if (m_confirmationAction == ConfirmationAction::RevertActiveFile)
    {
        ui::widgets::Text("Undo local changes on disk for:");
        ui::widgets::Text(m_confirmationFile);
    }
    else if (m_confirmationAction == ConfirmationAction::RestoreShelfFile)
    {
        ui::widgets::Text("Replace the current disk version with the shelf branch version for:");
        ui::widgets::Text(m_confirmationFile);
    }
    else
    {
        ui::widgets::Text("No operation is pending.");
    }

    ui::widgets::Separator();
    if (ui::widgets::Button("Yes"))
    {
        ConfirmPendingAction();
        ui::widgets::CloseCurrentPopup();
    }

    ui::widgets::SameLine();
    if (ui::widgets::Button("No"))
    {
        ClearPendingConfirmation();
        ui::widgets::CloseCurrentPopup();
    }

    ui::widgets::EndModal();
}

void AppUi::ConfirmPendingAction()
{
    const ConfirmationAction action = m_confirmationAction;
    const std::string shelf = m_confirmationShelf;
    const std::string file = m_confirmationFile;
    ClearPendingConfirmation();

    if (action == ConfirmationAction::RevertActiveFile)
        RevertCheckedOutFile(shelf, file);
    else if (action == ConfirmationAction::RestoreShelfFile)
        RestoreShelfFile(shelf, file);
}

void AppUi::ClearPendingConfirmation()
{
    m_confirmationAction = ConfirmationAction::None;
    m_confirmationShelf.clear();
    m_confirmationFile.clear();
    m_openConfirmationPopup = false;
}

void AppUi::OpenMainSubmitPopup()
{
    std::fill(m_mainSubmitSummaryInput.begin(), m_mainSubmitSummaryInput.end(), '\0');
    std::fill(m_mainSubmitDescriptionInput.begin(), m_mainSubmitDescriptionInput.end(), '\0');
    m_mainSubmitError.clear();
    m_openMainSubmitPopup = true;
}

void AppUi::DrawMainSubmitPopup()
{
    constexpr std::string_view popupId = "Submit Main";
    if (m_openMainSubmitPopup)
    {
        ui::widgets::OpenPopup(popupId);
        m_openMainSubmitPopup = false;
    }

    if (!ui::widgets::BeginModal(popupId))
        return;

    const std::vector<std::string> files = MainSubmittableFiles();
    ui::widgets::Text("Submit local Main changes.");
    ui::widgets::Text("Main will be pulled before submitting.");
    ui::widgets::Text(std::to_string(files.size()) + " file(s)");
    if (m_currentGitBranch != "main")
        ui::widgets::Text("Checkout main branch to submit Main.");
    if (!m_mainSubmitError.empty())
        ui::widgets::Text(m_mainSubmitError);

    ui::widgets::Separator();
    ui::widgets::InputText("Summary", m_mainSubmitSummaryInput.data(), m_mainSubmitSummaryInput.size());
    ui::widgets::InputTextMultiline("Description", m_mainSubmitDescriptionInput.data(), m_mainSubmitDescriptionInput.size());
    ui::widgets::Separator();

    const bool canSubmit = !files.empty() &&
        m_currentGitBranch == "main" &&
        !IsShelfBusy("main") &&
        m_mainSubmitSummaryInput[0] != '\0';
    ui::widgets::BeginDisabled(!canSubmit);
    if (ui::widgets::Button("Submit"))
    {
        SubmitMainFromPopup();
        ui::widgets::CloseCurrentPopup();
    }
    ui::widgets::EndDisabled();

    ui::widgets::SameLine();
    if (ui::widgets::Button("Cancel"))
    {
        m_mainSubmitError.clear();
        ui::widgets::CloseCurrentPopup();
    }

    ui::widgets::EndModal();
}

void AppUi::Draw()
{
    PollAsyncOperations();
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
    DrawConfirmationPopup();
    DrawMainSubmitPopup();
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

        if (m_repositoryLoadFuture.has_value())
            ui::widgets::Spinner("Loading repository");

        ui::widgets::Separator();

        if (m_hasSourcePath)
        {
            ui::widgets::Text("Repo: " + m_sourcePath.string());
            if (m_mainBehindCount > 0)
            {
                ui::widgets::Text("Main has " + std::to_string(m_mainBehindCount) + " update(s).");
                ui::widgets::SameLine();
                const bool canPull = m_currentGitBranch == "main" && !IsShelfBusy("main");
                ui::widgets::BeginDisabled(!canPull);
                if (ui::widgets::Button("Pull"))
                    PullMain();
                ui::widgets::EndDisabled();
            }
            else if (m_mainRemoteAvailable)
            {
                ui::widgets::Text("Main is up to date.");
            }

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
            if (m_repositoryLoadFuture.has_value())
                ui::widgets::Spinner("Loading repository");
            else
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

        if (m_refreshFuture.has_value())
        {
            ui::widgets::SameLine();
            ui::widgets::Spinner("Refreshing");
        }

        ui::widgets::Separator();

        DrawShelfList();
    }
    ui::widgets::EndWindow();
}

void AppUi::DrawLog()
{
    const int fps = static_cast<int>(ui::widgets::FrameRate() + 0.5f);
    const std::string logTitle = "Log (" + std::to_string(fps) + " fps)";
    if (ui::widgets::BeginWindow(logTitle + "###Log"))
    {
        ui::widgets::DrawWindowHeader(logTitle);
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

    const std::string pathText = selectedPath.string();
    std::fill(m_sourcePathInput.begin(), m_sourcePathInput.end(), '\0');
    pathText.copy(m_sourcePathInput.data(), std::min(pathText.size(), m_sourcePathInput.size() - 1));

    StartRepositoryLoad(selectedPath);
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
        const std::optional<std::string> activeShelf = ActiveShelfForFile(relativePath);
        if (activeShelf.has_value())
        {
            if (ui::widgets::MenuItem("Revert", !IsShelfBusy(*activeShelf)))
            {
                if (HasLocalChanges(relativePath))
                    RequestConfirmation(ConfirmationAction::RevertActiveFile, *activeShelf, relativePath);
                else
                    RevertCheckedOutFile(*activeShelf, relativePath);
            }
        }
        else
        {
            if (ui::widgets::MenuItem("Check out", true))
            {
                SelectShelf("main");
                CheckOutFile(entry.path());
            }
        }

        ui::widgets::EndContextMenu();
    }
}

void AppUi::CheckOutFile(const std::filesystem::path& path)
{
    if (m_selectedBranch.empty())
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

    if (!m_shelfJobs.empty())
    {
        m_refreshAfterJobs = true;
        return;
    }

    if (m_refreshFuture.has_value() || m_repositoryLoadFuture.has_value())
        return;

    const std::filesystem::path repoRoot = m_sourcePath;
    const uint64_t refreshGeneration = ++m_refreshGeneration;
    m_refreshAfterJobs = false;
    m_refreshFuture = std::async(std::launch::async, [repoRoot, logCommands, refreshGeneration]() {
        return LoadRepositorySnapshot(repoRoot, false, logCommands, refreshGeneration);
    });
}

void AppUi::RefreshRepositoryDataIfNeeded()
{
    if (!m_repository.has_value() || m_refreshFuture.has_value() || m_repositoryLoadFuture.has_value())
        return;

    const auto now = std::chrono::steady_clock::now();
    if (m_lastRefreshTime.time_since_epoch().count() == 0 || now - m_lastRefreshTime >= std::chrono::seconds(30))
        RefreshRepositoryData(false);
}

void AppUi::RequestRepositoryRefresh()
{
    if (!m_repository.has_value())
        return;

    if (!m_shelfJobs.empty())
    {
        m_refreshAfterJobs = true;
        return;
    }

    RefreshRepositoryData(false);
}

void AppUi::PruneInvalidWorkspaceShelves()
{
    bool changed = false;
    const std::vector<ShelfWorkspaceFiles> workspaceShelves = m_workspaceState.Shelves();
    for (const ShelfWorkspaceFiles& shelf : workspaceShelves)
    {
        if (shelf.shelf == "main")
            continue;

        if (std::find(m_shelves.begin(), m_shelves.end(), shelf.shelf) == m_shelves.end())
        {
            m_workspaceState.DeleteShelf(shelf.shelf);
            changed = true;
        }
    }

    if (changed)
        m_workspaceState.Save();
}

void AppUi::DrawShelfList()
{
    DrawShelfPanel("main", true);

    ui::widgets::Separator();
    ui::widgets::Text("Shelves");
    if (m_shelves.empty())
    {
        ui::widgets::Text("No shelves yet.");
        return;
    }

    const std::vector<std::string> shelvesCopy = m_shelves;
    for (const std::string& shelf : shelvesCopy)
        DrawShelfPanel(shelf, false);
}

void AppUi::DrawShelfPanel(const std::string& shelf, bool isMainShelf)
{
    const std::string title = isMainShelf ? "Main" : shelf;
    const bool shelfBusy = IsShelfBusy(shelf);
    if (shelfBusy)
        ui::widgets::BeginDisabled();

    const bool open = ui::widgets::BeginTreeNode(title, isMainShelf);

    if (ui::widgets::BeginContextMenuForLastItem())
    {
        if (isMainShelf)
        {
            const bool canOpenSubmit = !shelfBusy && !MainSubmittableFiles().empty();
            if (ui::widgets::MenuItem("Submit", canOpenSubmit))
                OpenMainSubmitPopup();
        }
        else
        {
            if (ui::widgets::MenuItem("Shelve", !shelfBusy))
                ShelveShelf(shelf);

            if (ui::widgets::MenuItem("Submit", !shelfBusy))
                SubmitShelf(shelf);

            const std::string link = ShelfLink(shelf);
            if (ui::widgets::MenuItem("Open", !link.empty() && !shelfBusy))
                OpenShelfLink(shelf);

            if (ui::widgets::MenuItem("Delete", !shelfBusy))
                DeleteShelf(shelf);
        }

        ui::widgets::EndContextMenu();
    }

    if (!shelfBusy)
    {
        if (const std::optional<std::string> payload = ui::widgets::AcceptDragDropPayload("p4v-git-file"))
            MoveCheckedOutFile(*payload, shelf);
    }

    if (shelfBusy)
        ui::widgets::EndDisabled();

    if (shelfBusy)
        ui::widgets::Spinner(ShelfBusyLabel(shelf));

    if (!open)
        return;

    if (shelfBusy)
        ui::widgets::BeginDisabled();

    const std::vector<std::string> mainFiles = isMainShelf ? MainActiveFiles() : std::vector<std::string>{};
    const std::vector<std::string>& files = isMainShelf ? mainFiles : m_workspaceState.CheckedOutFiles(shelf);
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

    if (!isMainShelf)
    {
        ui::widgets::Separator();
        if (ui::widgets::BeginTreeNode("Files in Shelf##" + shelf))
        {
            const std::vector<GitStatusEntry> shelfFiles = ShelfFiles(shelf);
            if (shelfFiles.empty())
            {
                ui::widgets::Text("No committed files in this shelf yet.");
            }
            else
            {
                for (const GitStatusEntry& file : shelfFiles)
                    DrawShelfCommittedFile(shelf, file);
            }

            ui::widgets::EndTreeNode();
        }
    }

    ui::widgets::EndTreeNode();

    if (shelfBusy)
        ui::widgets::EndDisabled();
}

void AppUi::DrawShelfFile(const std::string& shelf, const std::string& file)
{
    ui::widgets::Selectable(file + "##" + shelf + "/" + file, false);
    ui::widgets::DragDropSource("p4v-git-file", shelf + "\n" + file, file);

    if (ui::widgets::BeginContextMenuForLastItem())
    {
        if (ui::widgets::MenuItem("Revert", true))
        {
            if (HasLocalChanges(file))
                RequestConfirmation(ConfirmationAction::RevertActiveFile, shelf, file);
            else
                RevertCheckedOutFile(shelf, file);
        }
        ui::widgets::EndContextMenu();
    }
}

void AppUi::DrawShelfCommittedFile(const std::string& shelf, const GitStatusEntry& file)
{
    const std::string label = file.status + "  " + file.path;
    ui::widgets::Selectable(label + "##shelf-file/" + shelf + "/" + file.path, false);

    if (ui::widgets::BeginContextMenuForLastItem())
    {
        if (ui::widgets::MenuItem("Restore", !IsShelfBusy(shelf)))
        {
            if (HasLocalChanges(file.path))
                RequestConfirmation(ConfirmationAction::RestoreShelfFile, shelf, file.path);
            else
                RestoreShelfFile(shelf, file.path);
        }

        if (ui::widgets::MenuItem("Revert", !IsShelfBusy(shelf)))
            RevertShelfFile(shelf, file.path);
        ui::widgets::EndContextMenu();
    }
}

void AppUi::SelectShelf(std::string_view shelf)
{
    if (shelf.empty() || shelf == "main")
    {
        m_selectedBranch = "main";
        m_workspaceState.SetActiveShelf(m_selectedBranch);
        m_workspaceState.Save();
        return;
    }

    if (std::find(m_shelves.begin(), m_shelves.end(), shelf) == m_shelves.end())
    {
        std::cout << "Cannot open shelf because it no longer exists: " << shelf << '\n';
        ClearSelectedShelf();
        return;
    }

    m_selectedBranch = std::string(shelf);
    m_workspaceState.SetActiveShelf(m_selectedBranch);
    m_workspaceState.Save();
}

void AppUi::ClearSelectedShelf()
{
    m_selectedBranch = "main";
    m_workspaceState.SetActiveShelf(m_selectedBranch);
    m_workspaceState.Save();
}

void AppUi::SetShelfLink(std::string_view shelf, std::string url)
{
    for (ShelfLinkEntry& link : m_shelfLinks)
    {
        if (link.shelf == shelf)
        {
            link.url = std::move(url);
            return;
        }
    }

    if (!url.empty())
        m_shelfLinks.push_back({ std::string(shelf), std::move(url) });
}

std::string AppUi::ShelfLink(std::string_view shelf) const
{
    for (const ShelfLinkEntry& link : m_shelfLinks)
    {
        if (link.shelf == shelf)
            return link.url;
    }

    return {};
}

std::vector<GitStatusEntry> AppUi::ShelfFiles(std::string_view shelf) const
{
    for (const ShelfCommittedFileList& files : m_shelfFiles)
    {
        if (files.shelf == shelf)
            return files.files;
    }

    return {};
}

std::vector<std::string> AppUi::MainActiveFiles() const
{
    std::vector<std::string> files;
    for (const std::string& file : m_workspaceState.CheckedOutFiles("main"))
    {
        if (!IsFileActiveInShelf(file))
            files.push_back(file);
    }

    if (m_currentGitBranch == "main")
    {
        for (const GitStatusEntry& entry : m_statusEntries)
        {
            if (!IsFileActiveInShelf(entry.path) && std::find(files.begin(), files.end(), entry.path) == files.end())
                files.push_back(entry.path);
        }
    }

    return files;
}

std::vector<std::string> AppUi::MainSubmittableFiles() const
{
    std::vector<std::string> files;
    for (const std::string& file : MainActiveFiles())
    {
        if (HasLocalChanges(file))
            files.push_back(file);
    }

    return files;
}

std::optional<std::string> AppUi::ActiveShelfForFile(std::string_view relativePath) const
{
    for (const ShelfWorkspaceFiles& shelf : m_workspaceState.Shelves())
    {
        if (std::find(shelf.files.begin(), shelf.files.end(), relativePath) != shelf.files.end())
            return shelf.shelf;
    }

    if (m_currentGitBranch == "main" && HasLocalChanges(relativePath))
        return "main";

    return std::nullopt;
}

bool AppUi::IsFileActiveInShelf(std::string_view relativePath) const
{
    for (const ShelfWorkspaceFiles& shelf : m_workspaceState.Shelves())
    {
        if (shelf.shelf == "main")
            continue;

        if (std::find(shelf.files.begin(), shelf.files.end(), relativePath) != shelf.files.end())
            return true;
    }

    return false;
}

bool AppUi::HasLocalChanges(std::string_view relativePath) const
{
    return std::any_of(m_statusEntries.begin(), m_statusEntries.end(), [relativePath](const GitStatusEntry& entry) {
        return entry.path == relativePath;
    });
}

bool AppUi::IsFileActive(std::string_view relativePath) const
{
    if (m_workspaceState.IsCheckedOut(relativePath))
        return true;

    if (m_currentGitBranch != "main")
        return false;

    return std::any_of(m_statusEntries.begin(), m_statusEntries.end(), [relativePath](const GitStatusEntry& entry) {
        return entry.path == relativePath;
    });
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

void AppUi::RevertCheckedOutFile(const std::string& shelf, const std::string& file)
{
    if (!HasLocalChanges(file))
    {
        m_workspaceState.RevertCheckedOutFile(shelf, file);
        m_workspaceState.Save();
        std::cout << "Reverted active change " << file << " from " << shelf << '\n';
        return;
    }

    if (IsShelfBusy(shelf))
        return;

    const std::filesystem::path repoRoot = m_sourcePath;
    StartShelfJob(shelf, "Reverting", std::async(std::launch::async, [repoRoot, shelf, file]() {
        return RunRevertActiveFileJob(repoRoot, shelf, file);
    }));
}

void AppUi::RevertShelfFile(const std::string& shelf, const std::string& file)
{
    if (IsShelfBusy(shelf))
        return;

    const std::filesystem::path repoRoot = m_sourcePath;
    StartShelfJob(shelf, "Reverting", std::async(std::launch::async, [repoRoot, shelf, file]() {
        return RunRevertShelfFileJob(repoRoot, shelf, file);
    }));
}

void AppUi::RestoreShelfFile(const std::string& shelf, const std::string& file)
{
    if (IsShelfBusy(shelf))
        return;

    const std::filesystem::path repoRoot = m_sourcePath;
    StartShelfJob(shelf, "Restoring", std::async(std::launch::async, [repoRoot, shelf, file]() {
        return RunRestoreShelfFileJob(repoRoot, shelf, file);
    }));
}

void AppUi::CreateShelfFromInput()
{
    if (!m_repository.has_value())
        return;

    const std::string shelfName = m_newShelfNameInput.data();
    if (shelfName.empty())
        return;

    const std::filesystem::path repoRoot = m_sourcePath;
    StartShelfJob("main", "Creating shelf", std::async(std::launch::async, [repoRoot, shelfName]() {
        return RunCreateShelfJob(repoRoot, shelfName);
    }));
    std::fill(m_newShelfNameInput.begin(), m_newShelfNameInput.end(), '\0');
}

void AppUi::ShelveShelf(const std::string& shelf)
{
    if (IsShelfBusy(shelf))
        return;

    const std::filesystem::path repoRoot = m_sourcePath;
    const std::vector<std::string> files = m_workspaceState.CheckedOutFiles(shelf);
    StartShelfJob(shelf, "Shelving", std::async(std::launch::async, [repoRoot, shelf, files]() {
        return RunShelveShelfJob(repoRoot, shelf, files);
    }));
}

void AppUi::SubmitShelf(const std::string& shelf)
{
    if (IsShelfBusy(shelf))
        return;

    const std::filesystem::path repoRoot = m_sourcePath;
    const std::vector<std::string> files = m_workspaceState.CheckedOutFiles(shelf);
    StartShelfJob(shelf, "Submitting", std::async(std::launch::async, [repoRoot, shelf, files]() {
        return RunSubmitShelfJob(repoRoot, shelf, files);
    }));
}

void AppUi::SubmitMainFromPopup()
{
    if (IsShelfBusy("main"))
        return;

    std::vector<std::string> files = MainSubmittableFiles();
    const std::string summary = m_mainSubmitSummaryInput.data();
    const std::string description = m_mainSubmitDescriptionInput.data();
    if (files.empty())
    {
        m_mainSubmitError = "No Main changes to submit.";
        return;
    }

    if (summary.empty())
    {
        m_mainSubmitError = "Summary is required.";
        return;
    }

    const std::filesystem::path repoRoot = m_sourcePath;
    StartShelfJob("main", "Submitting", std::async(std::launch::async, [repoRoot, files = std::move(files), summary, description]() mutable {
        return RunSubmitMainJob(repoRoot, std::move(files), summary, description);
    }));
}

void AppUi::PullMain()
{
    if (IsShelfBusy("main"))
        return;

    const std::filesystem::path repoRoot = m_sourcePath;
    StartShelfJob("main", "Pulling", std::async(std::launch::async, [repoRoot]() {
        return RunPullMainJob(repoRoot);
    }));
}

void AppUi::OpenShelfLink(std::string_view shelf)
{
    const std::string shelfLink = ShelfLink(shelf);
    if (shelfLink.empty())
        return;

    std::cout << "Open shelf: " << shelfLink << '\n';
    if (!OpenUrl(shelfLink))
        std::cout << "Failed to open shelf link in browser\n";
}

void AppUi::DeleteShelf(const std::string& shelf)
{
    if (IsShelfBusy(shelf))
        return;

    const std::filesystem::path repoRoot = m_sourcePath;
    StartShelfJob(shelf, "Deleting shelf", std::async(std::launch::async, [repoRoot, shelf]() {
        return RunDeleteShelfJob(repoRoot, shelf);
    }));
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
    if (IsFileActive(relativePath))
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
