#include "ui/AppUi.h"

#include "git/GitRepository.h"
#include "log/StdoutLog.h"
#include "P4vGitVersion.h"
#include "platform/UrlLauncher.h"
#include "platform/Window.h"
#include "ui/widgets/Widgets.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <future>
#include <iostream>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace p4vgit
{
static std::optional<std::string> WorkspaceShelfForFile(const WorkspaceState& workspaceState, std::string_view file)
{
    for (const ShelfWorkspaceFiles& shelf : workspaceState.Shelves())
    {
        if (std::find(shelf.files.begin(), shelf.files.end(), file) != shelf.files.end())
            return shelf.shelf;
    }

    return std::nullopt;
}

static std::optional<std::string> CommittedShelfForFile(const std::vector<ShelfCommittedFileList>& shelfFiles, std::string_view file)
{
    std::optional<std::string> matchingShelf;
    for (const ShelfCommittedFileList& shelf : shelfFiles)
    {
        const bool fileInShelf = std::any_of(shelf.files.begin(), shelf.files.end(), [file](const GitStatusEntry& entry) {
            return entry.path == file;
        });
        if (!fileInShelf)
            continue;

        if (matchingShelf.has_value())
            return std::nullopt;

        matchingShelf = shelf.shelf;
    }

    return matchingShelf;
}

static size_t AssignStatusFilesToWorkspaceState(RepositorySnapshot& snapshot)
{
    size_t assignedCount = 0;
    for (const GitStatusEntry& entry : snapshot.statusEntries)
    {
        if (entry.path.empty())
            continue;

        const std::optional<std::string> currentShelf = WorkspaceShelfForFile(snapshot.workspaceState, entry.path);
        const std::optional<std::string> committedShelf = CommittedShelfForFile(snapshot.shelfFiles, entry.path);
        std::string targetShelf = snapshot.targetBranch;
        if (currentShelf.has_value() && *currentShelf != snapshot.targetBranch)
            targetShelf = *currentShelf;
        else if (committedShelf.has_value())
            targetShelf = *committedShelf;

        if (currentShelf.has_value() && *currentShelf == targetShelf)
            continue;

        snapshot.workspaceState.CheckOut(targetShelf, entry.path);
        ++assignedCount;
    }

    return assignedCount;
}

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
    snapshot.targetBranch = snapshot.workspaceState.TargetBranch().empty() ? "main" : snapshot.workspaceState.TargetBranch();
    for (const std::string& branch : repository.LocalBranches(logCommands))
    {
        if (branch.rfind("shelves/", 0) != 0)
            snapshot.branches.push_back(branch);
    }

    if (!snapshot.branches.empty() && std::find(snapshot.branches.begin(), snapshot.branches.end(), snapshot.targetBranch) == snapshot.branches.end())
    {
        const std::string currentBranch = repository.CurrentBranch(logCommands);
        if (!currentBranch.empty() && currentBranch.rfind("shelves/", 0) != 0)
            snapshot.targetBranch = currentBranch;
        else if (std::find(snapshot.branches.begin(), snapshot.branches.end(), "main") != snapshot.branches.end())
            snapshot.targetBranch = "main";
        else
            snapshot.targetBranch = snapshot.branches.front();

        snapshot.workspaceState.SetTargetBranch(snapshot.targetBranch);
        snapshot.workspaceState.SetActiveShelf(snapshot.targetBranch);
        snapshot.workspaceState.Save();
    }

    shelfService.SetTargetBranch(snapshot.targetBranch);
    snapshot.shelves = shelfService.Shelves(logCommands);
    repository.Run("add -A .", logCommands);
    snapshot.statusEntries = repository.Status(logCommands);
    snapshot.currentBranch = repository.CurrentBranch(logCommands);
    const MainSyncStatus mainSyncStatus = shelfService.RefreshMain(logCommands);
    snapshot.mainRemoteAvailable = mainSyncStatus.remoteAvailable;
    snapshot.mainBehindCount = mainSyncStatus.behindCount;

    bool stateChanged = false;
    const std::vector<ShelfWorkspaceFiles> workspaceShelves = snapshot.workspaceState.Shelves();
    for (const ShelfWorkspaceFiles& shelf : workspaceShelves)
    {
        if (shelf.shelf == snapshot.targetBranch)
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

    const size_t assignedStatusFiles = AssignStatusFilesToWorkspaceState(snapshot);
    if (assignedStatusFiles > 0 || stateChanged)
        snapshot.workspaceState.Save();
    if (logCommands)
    {
        std::cout << "Refresh status found " << snapshot.statusEntries.size() << " changed file(s)";
        if (assignedStatusFiles > 0)
            std::cout << " and assigned " << assignedStatusFiles << " to active change lists";
        std::cout << ".\n";
    }

    snapshot.succeeded = true;
    return snapshot;
}

ShelfJobResult AppUi::RunCreateShelfJob(const std::filesystem::path& repoRoot, const std::string& targetBranch, const std::string& shelfName, const std::vector<std::string>& files)
{
    GitRepository repository(repoRoot);
    ShelfService shelfService;
    shelfService.SetRepository(&repository);
    shelfService.SetTargetBranch(targetBranch);

    ShelfJobResult result;
    result.shelf = shelfService.MakeShelfBranch(shelfName);
    result.files = files;
    result.succeeded = shelfService.CreateShelf(shelfName);
    result.selectShelf = result.succeeded;
    result.addFilesState = result.succeeded && !result.files.empty();
    return result;
}

ShelfJobResult AppUi::RunCreateTargetBranchJob(const std::filesystem::path& repoRoot, const std::string& baseBranch, const std::string& newBranch)
{
    GitRepository repository(repoRoot);

    ShelfJobResult result;
    result.shelf = newBranch;
    result.succeeded = repository.CreateAndCheckoutBranch(result.shelf, baseBranch);
    result.selectTargetBranch = result.succeeded;
    return result;
}

ShelfJobResult AppUi::RunSelectTargetBranchJob(const std::filesystem::path& repoRoot, const std::string& targetBranch)
{
    GitRepository repository(repoRoot);
    ShelfService shelfService;
    shelfService.SetRepository(&repository);
    shelfService.SetTargetBranch(targetBranch);

    ShelfJobResult result;
    result.shelf = targetBranch;
    result.succeeded = repository.CheckoutBranch(result.shelf);
    if (result.succeeded)
        result.succeeded = shelfService.PullMain();
    result.selectTargetBranch = result.succeeded;
    return result;
}

ShelfJobResult AppUi::RunShelveShelfJob(const std::filesystem::path& repoRoot, const std::string& targetBranch, const std::string& shelf, const std::vector<std::string>& files, const std::string& summary, const std::string& description)
{
    GitRepository repository(repoRoot);
    ShelfService shelfService;
    shelfService.SetRepository(&repository);
    shelfService.SetTargetBranch(targetBranch);

    ShelfJobResult result;
    result.shelf = shelf;
    result.shelfUrl = shelfService.ShelveFilesAndEnsureShelfLink(result.shelf, files, summary, description);
    result.succeeded = !result.shelfUrl.empty();
    return result;
}

ShelfJobResult AppUi::RunShelveAndSubmitShelfJob(const std::filesystem::path& repoRoot, const std::string& targetBranch, const std::string& shelf, const std::vector<std::string>& files, const std::string& summary, const std::string& description)
{
    GitRepository repository(repoRoot);
    ShelfService shelfService;
    shelfService.SetRepository(&repository);
    shelfService.SetTargetBranch(targetBranch);

    ShelfJobResult result;
    result.shelf = shelf;
    result.files = files;
    result.shelfUrl = shelfService.ShelveFilesAndEnsureShelfLink(result.shelf, files, summary, description);
    if (result.shelfUrl.empty())
        return result;

    result.submit = shelfService.SubmitShelf(result.shelf, result.files);
    if (!result.submit.shelfUrl.empty())
        result.shelfUrl = result.submit.shelfUrl;
    result.succeeded = result.submit.merged;
    result.deleteShelfState = result.submit.merged;
    return result;
}

ShelfJobResult AppUi::RunSubmitShelfJob(const std::filesystem::path& repoRoot, const std::string& targetBranch, const std::string& shelf, const std::vector<std::string>& files)
{
    GitRepository repository(repoRoot);
    ShelfService shelfService;
    shelfService.SetRepository(&repository);
    shelfService.SetTargetBranch(targetBranch);

    ShelfJobResult result;
    result.shelf = shelf;
    result.files = files;
    result.submit = shelfService.SubmitShelf(result.shelf, result.files);
    result.shelfUrl = result.submit.shelfUrl;
    result.succeeded = result.submit.merged;
    result.deleteShelfState = result.submit.merged;
    return result;
}

ShelfJobResult AppUi::RunSubmitMainJob(const std::filesystem::path& repoRoot, const std::string& targetBranch, const std::vector<std::string>& files, const std::string& summary, const std::string& description)
{
    GitRepository repository(repoRoot);
    ShelfService shelfService;
    shelfService.SetRepository(&repository);
    shelfService.SetTargetBranch(targetBranch);

    ShelfJobResult result;
    result.shelf = targetBranch;
    result.files = files;
    result.succeeded = shelfService.SubmitMain(result.files, summary, description);
    result.clearMainActiveFiles = result.succeeded;
    return result;
}

ShelfJobResult AppUi::RunPullMainJob(const std::filesystem::path& repoRoot, const std::string& targetBranch)
{
    GitRepository repository(repoRoot);
    ShelfService shelfService;
    shelfService.SetRepository(&repository);
    shelfService.SetTargetBranch(targetBranch);

    ShelfJobResult result;
    result.shelf = targetBranch;
    result.succeeded = shelfService.PullMain();
    return result;
}

ShelfJobResult AppUi::RunDeleteShelfJob(const std::filesystem::path& repoRoot, const std::string& shelf)
{
    GitRepository repository(repoRoot);
    ShelfService shelfService;
    shelfService.SetRepository(&repository);

    ShelfJobResult result;
    result.shelf = shelf;
    result.succeeded = shelfService.DeleteShelf(result.shelf, true);
    result.deleteShelfState = result.succeeded;
    return result;
}

ShelfJobResult AppUi::RunRevertShelfFileJob(const std::filesystem::path& repoRoot, const std::string& targetBranch, const std::string& shelf, const std::string& file)
{
    GitRepository repository(repoRoot);
    ShelfService shelfService;
    shelfService.SetRepository(&repository);
    shelfService.SetTargetBranch(targetBranch);

    ShelfJobResult result;
    result.shelf = shelf;
    result.file = file;
    result.succeeded = shelfService.RevertFileFromShelf(result.shelf, result.file);
    result.removeShelfFileState = result.succeeded;
    return result;
}

ShelfJobResult AppUi::RunRevertShelfFilesJob(const std::filesystem::path& repoRoot, const std::string& targetBranch, const std::string& shelf, const std::vector<std::string>& files)
{
    GitRepository repository(repoRoot);
    ShelfService shelfService;
    shelfService.SetRepository(&repository);
    shelfService.SetTargetBranch(targetBranch);

    ShelfJobResult result;
    result.shelf = shelf;
    result.files = files;
    result.succeeded = true;
    for (const std::string& file : result.files)
        result.succeeded = shelfService.RevertFileFromShelf(result.shelf, file) && result.succeeded;
    result.removeShelfFileState = result.succeeded;
    return result;
}

ShelfJobResult AppUi::RunRevertActiveFileJob(const std::filesystem::path& repoRoot, const std::string& shelf, const std::string& file)
{
    GitRepository repository(repoRoot);
    ShelfService shelfService;
    shelfService.SetRepository(&repository);

    ShelfJobResult result;
    result.shelf = shelf;
    result.file = file;
    result.succeeded = shelfService.UndoLocalFileChanges(result.file);
    result.revertActiveFileState = result.succeeded;
    return result;
}

ShelfJobResult AppUi::RunRevertActiveFilesJob(const std::filesystem::path& repoRoot, const std::string& shelf, const std::vector<std::string>& files)
{
    GitRepository repository(repoRoot);
    ShelfService shelfService;
    shelfService.SetRepository(&repository);

    ShelfJobResult result;
    result.shelf = shelf;
    result.files = files;
    result.succeeded = true;
    for (const std::string& file : result.files)
        result.succeeded = shelfService.UndoLocalFileChanges(file) && result.succeeded;
    result.clearMainActiveFiles = result.succeeded;
    return result;
}

ShelfJobResult AppUi::RunRestoreShelfFileJob(const std::filesystem::path& repoRoot, const std::string& shelf, const std::string& file)
{
    GitRepository repository(repoRoot);
    ShelfService shelfService;
    shelfService.SetRepository(&repository);

    ShelfJobResult result;
    result.shelf = shelf;
    result.file = file;
    result.succeeded = shelfService.RestoreFileFromShelfToWorkingTree(result.shelf, result.file);
    result.addFileState = result.succeeded;
    return result;
}

ShelfJobResult AppUi::RunRestoreShelfFilesJob(const std::filesystem::path& repoRoot, const std::string& shelf, const std::vector<std::string>& files)
{
    GitRepository repository(repoRoot);
    ShelfService shelfService;
    shelfService.SetRepository(&repository);

    ShelfJobResult result;
    result.shelf = shelf;
    result.files = files;
    result.succeeded = true;
    for (const std::string& file : result.files)
        result.succeeded = shelfService.RestoreFileFromShelfToWorkingTree(result.shelf, file) && result.succeeded;
    result.addFilesState = result.succeeded;
    return result;
}

void AppUi::RunOpenFileDiffJob(const std::filesystem::path& repoRoot, const std::string& targetBranch, const std::string& file)
{
    GitRepository repository(repoRoot);
    ShelfService shelfService;
    shelfService.SetRepository(&repository);
    shelfService.SetTargetBranch(targetBranch);
    shelfService.OpenFileDiff(file);
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

void AppUi::SetWindow(Window* window)
{
    m_window = window;
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
            m_branches.clear();
            m_targetBranch = "main";
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
            if (!result.shelf.empty() && result.shelf != m_targetBranch && std::find(m_shelves.begin(), m_shelves.end(), result.shelf) == m_shelves.end())
                m_shelves.push_back(result.shelf);
            m_selectedBranch = result.shelf;
            m_workspaceState.SetActiveShelf(result.shelf);
            m_workspaceState.Save();
        }

        if (result.selectTargetBranch)
        {
            m_targetBranch = result.shelf.empty() ? "main" : result.shelf;
            m_selectedBranch = m_targetBranch;
            if (std::find(m_branches.begin(), m_branches.end(), m_targetBranch) == m_branches.end())
            {
                m_branches.push_back(m_targetBranch);
                std::sort(m_branches.begin(), m_branches.end());
            }
            m_workspaceState.SetTargetBranch(m_targetBranch);
            m_workspaceState.SetActiveShelf(m_targetBranch);
            m_workspaceState.Save();
            m_currentGitBranch = m_targetBranch;
            m_shelves.clear();
            m_shelfLinks.clear();
            m_shelfFiles.clear();
        }

        if (result.addFileState)
        {
            m_workspaceState.CheckOut(result.shelf, result.file);
            m_workspaceState.SetActiveShelf(result.shelf);
            m_workspaceState.Save();
        }

        if (result.addFilesState)
        {
            for (const std::string& file : result.files)
                m_workspaceState.CheckOut(result.shelf, file);
            m_workspaceState.SetActiveShelf(result.shelf);
            m_workspaceState.Save();
            m_selectedActiveShelf = result.shelf;
            m_selectedActiveFiles = result.files;
        }

        if (result.clearMainActiveFiles)
        {
            for (const std::string& file : result.files)
                m_workspaceState.RevertCheckedOutFile(result.shelf, file);
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
                    if (!result.files.empty())
                        return std::find(result.files.begin(), result.files.end(), file.path) != result.files.end();
                    return file.path == result.file;
                }), shelfFiles.files.end());
                break;
            }
        }

        if (result.deleteShelfState)
        {
            if (m_selectedBranch == result.shelf)
                ClearSelectedShelf();
            m_shelves.erase(std::remove(m_shelves.begin(), m_shelves.end(), result.shelf), m_shelves.end());
            m_shelfLinks.erase(std::remove_if(m_shelfLinks.begin(), m_shelfLinks.end(), [&result](const ShelfLinkEntry& link) {
                return link.shelf == result.shelf;
            }), m_shelfLinks.end());
            m_shelfFiles.erase(std::remove_if(m_shelfFiles.begin(), m_shelfFiles.end(), [&result](const ShelfCommittedFileList& files) {
                return files.shelf == result.shelf;
            }), m_shelfFiles.end());
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
    m_targetBranch = snapshot.targetBranch.empty() ? "main" : snapshot.targetBranch;
    m_shelfService.SetTargetBranch(m_targetBranch);
    m_workspaceState = snapshot.workspaceState;
    m_workspaceState.SetTargetBranch(m_targetBranch);
    m_branches = snapshot.branches;
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
        m_selectedBranch = m_targetBranch;

    if (m_selectedBranch != m_targetBranch && std::find(m_shelves.begin(), m_shelves.end(), m_selectedBranch) == m_shelves.end())
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
    m_targetBranch = "main";
    m_branches.clear();
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

void AppUi::RequestConfirmation(ConfirmationAction action, std::string_view shelf, std::string_view file)
{
    m_confirmationAction = action;
    m_confirmationShelf = shelf;
    m_confirmationFile = file;
    m_confirmationFiles = { m_confirmationFile };
    m_openConfirmationPopup = true;
}

void AppUi::RequestConfirmation(ConfirmationAction action, std::string_view shelf, const std::vector<std::string>& files)
{
    m_confirmationAction = action;
    m_confirmationShelf = shelf;
    m_confirmationFiles = files;
    m_confirmationFile = m_confirmationFiles.empty() ? std::string() : m_confirmationFiles.front();
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
        ui::widgets::Text(m_confirmationFiles.size() > 1 ? (std::to_string(m_confirmationFiles.size()) + " files") : m_confirmationFile);
    }
    else if (m_confirmationAction == ConfirmationAction::RestoreShelfFile)
    {
        ui::widgets::Text("Replace the current disk version with the shelf branch version for:");
        ui::widgets::Text(m_confirmationFiles.size() > 1 ? (std::to_string(m_confirmationFiles.size()) + " files") : m_confirmationFile);
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
    const std::vector<std::string> files = m_confirmationFiles.empty() ? std::vector<std::string>{ file } : m_confirmationFiles;
    ClearPendingConfirmation();

    if (action == ConfirmationAction::RevertActiveFile)
        RevertCheckedOutFilesConfirmed(shelf, files);
    else if (action == ConfirmationAction::RestoreShelfFile)
        RestoreShelfFiles(shelf, files);
}

void AppUi::ClearPendingConfirmation()
{
    m_confirmationAction = ConfirmationAction::None;
    m_confirmationShelf.clear();
    m_confirmationFile.clear();
    m_confirmationFiles.clear();
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
    constexpr std::string_view popupId = "Submit Target Branch";
    if (m_openMainSubmitPopup)
    {
        ui::widgets::OpenPopup(popupId);
        m_openMainSubmitPopup = false;
    }

    if (!ui::widgets::BeginModal(popupId))
        return;

    const std::vector<std::string> files = MainSubmittableFiles();
    ui::widgets::Text("Submit local changes to " + m_targetBranch + ".");
    ui::widgets::Text(m_targetBranch + " will be pulled before submitting.");
    ui::widgets::Text(std::to_string(files.size()) + " file(s)");
    if (m_currentGitBranch != m_targetBranch)
        ui::widgets::Text("Checkout " + m_targetBranch + " branch to submit.");
    if (!m_mainSubmitError.empty())
        ui::widgets::Text(m_mainSubmitError);

    ui::widgets::Separator();
    ui::widgets::InputText("Summary", m_mainSubmitSummaryInput.data(), m_mainSubmitSummaryInput.size());
    ui::widgets::InputTextMultiline("Description", m_mainSubmitDescriptionInput.data(), m_mainSubmitDescriptionInput.size());
    ui::widgets::Separator();

    const bool canSubmit = !files.empty() &&
        m_currentGitBranch == m_targetBranch &&
        !IsShelfBusy(m_targetBranch) &&
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

void AppUi::OpenShelvePopup(std::string_view shelf, bool submitAfterShelve)
{
    m_shelveShelf = shelf;
    m_shelveSubmitAfter = submitAfterShelve;
    std::fill(m_shelveSummaryInput.begin(), m_shelveSummaryInput.end(), '\0');
    std::fill(m_shelveDescriptionInput.begin(), m_shelveDescriptionInput.end(), '\0');
    constexpr std::string_view defaultSummary = "Shelve files";
    defaultSummary.copy(m_shelveSummaryInput.data(), std::min(defaultSummary.size(), m_shelveSummaryInput.size() - 1));
    m_shelveError.clear();
    m_openShelvePopup = true;
}

void AppUi::DrawShelvePopup()
{
    constexpr std::string_view popupId = "Shelve";
    if (m_openShelvePopup)
    {
        ui::widgets::OpenPopup(popupId);
        m_openShelvePopup = false;
    }

    if (!ui::widgets::BeginModal(popupId))
        return;

    const std::vector<std::string> files = m_workspaceState.CheckedOutFiles(m_shelveShelf);
    if (m_shelveSubmitAfter)
        ui::widgets::Text("Commit active changes to shelf, then submit:");
    else
        ui::widgets::Text("Commit active changes to shelf:");
    ui::widgets::Text(m_shelveShelf);
    ui::widgets::Text(std::to_string(files.size()) + " file(s)");
    if (!m_shelveError.empty())
        ui::widgets::Text(m_shelveError);

    ui::widgets::Separator();
    ui::widgets::InputText("Summary", m_shelveSummaryInput.data(), m_shelveSummaryInput.size());
    ui::widgets::InputTextMultiline("Description", m_shelveDescriptionInput.data(), m_shelveDescriptionInput.size());
    ui::widgets::Separator();

    const bool canShelve = !m_shelveShelf.empty() &&
        !files.empty() &&
        !IsShelfBusy(m_shelveShelf) &&
        m_shelveSummaryInput[0] != '\0';
    ui::widgets::BeginDisabled(!canShelve);
    if (ui::widgets::Button(m_shelveSubmitAfter ? "Submit" : "Shelve"))
    {
        if (m_shelveSubmitAfter)
            ShelveAndSubmitShelf(m_shelveShelf);
        else
            ShelveShelf(m_shelveShelf);
        ui::widgets::CloseCurrentPopup();
    }
    ui::widgets::EndDisabled();

    ui::widgets::SameLine();
    if (ui::widgets::Button("Cancel"))
    {
        m_shelveError.clear();
        m_shelveShelf.clear();
        m_shelveSubmitAfter = false;
        ui::widgets::CloseCurrentPopup();
    }

    ui::widgets::EndModal();
}

void AppUi::OpenCreateShelfPopup()
{
    std::fill(m_newShelfNameInput.begin(), m_newShelfNameInput.end(), '\0');
    m_createShelfError.clear();
    m_openCreateShelfPopup = true;
}

void AppUi::DrawCreateShelfPopup()
{
    constexpr std::string_view popupId = "Create Shelf";
    if (m_openCreateShelfPopup)
    {
        ui::widgets::OpenPopup(popupId);
        m_openCreateShelfPopup = false;
    }

    if (!ui::widgets::BeginModal(popupId))
        return;

    const size_t selectedCount = m_selectedActiveFiles.size();
    ui::widgets::Text("Create a shelf from " + m_targetBranch + ".");
    ui::widgets::Text(std::to_string(selectedCount) + " selected file(s) will move into it.");
    if (!m_createShelfError.empty())
        ui::widgets::Text(m_createShelfError);

    ui::widgets::Separator();
    ui::widgets::InputText("Branch name", m_newShelfNameInput.data(), m_newShelfNameInput.size());
    ui::widgets::Separator();

    const bool canCreate = m_repository.has_value() && m_newShelfNameInput[0] != '\0' && !IsShelfBusy(m_targetBranch);
    ui::widgets::BeginDisabled(!canCreate);
    if (ui::widgets::Button("Create"))
    {
        CreateShelfFromInput();
        ui::widgets::CloseCurrentPopup();
    }
    ui::widgets::EndDisabled();

    ui::widgets::SameLine();
    if (ui::widgets::Button("Cancel"))
    {
        m_createShelfError.clear();
        ui::widgets::CloseCurrentPopup();
    }

    ui::widgets::EndModal();
}

void AppUi::OpenCreateBranchPopup()
{
    std::fill(m_newBranchNameInput.begin(), m_newBranchNameInput.end(), '\0');
    m_createBranchError.clear();
    m_openCreateBranchPopup = true;
}

void AppUi::DrawCreateBranchPopup()
{
    constexpr std::string_view popupId = "Create Branch";
    if (m_openCreateBranchPopup)
    {
        ui::widgets::OpenPopup(popupId);
        m_openCreateBranchPopup = false;
    }

    if (!ui::widgets::BeginModal(popupId))
        return;

    ui::widgets::Text("Create a branch from " + m_targetBranch + ".");
    if (HasUnshelvedChanges())
        ui::widgets::Text("Shelve or revert active changes before creating a branch.");
    if (!m_createBranchError.empty())
        ui::widgets::Text(m_createBranchError);

    ui::widgets::Separator();
    ui::widgets::InputText("Branch name", m_newBranchNameInput.data(), m_newBranchNameInput.size());
    ui::widgets::Separator();

    const bool canCreate = m_repository.has_value() &&
        m_newBranchNameInput[0] != '\0' &&
        !HasUnshelvedChanges() &&
        !IsShelfBusy(m_targetBranch);
    ui::widgets::BeginDisabled(!canCreate);
    if (ui::widgets::Button("Create"))
    {
        CreateTargetBranchFromInput();
        ui::widgets::CloseCurrentPopup();
    }
    ui::widgets::EndDisabled();

    ui::widgets::SameLine();
    if (ui::widgets::Button("Cancel"))
    {
        m_createBranchError.clear();
        ui::widgets::CloseCurrentPopup();
    }

    ui::widgets::EndModal();
}

void AppUi::Draw()
{
    PollAsyncOperations();
    RefreshRepositoryDataIfNeeded();
    PruneSelectedActiveFiles();

    constexpr std::array defaultLayout = {
        ui::widgets::DockspaceDefaultLayout{ "Log", ui::widgets::DockspaceSide::Down, 30.0f },
        ui::widgets::DockspaceDefaultLayout{ "Workspace Explorer", ui::widgets::DockspaceSide::Left, 24.0f },
        ui::widgets::DockspaceDefaultLayout{ "File Changes", ui::widgets::DockspaceSide::Center, 100.0f },
    };

    DrawAppTitleBar();
    ui::widgets::DrawDockspace(defaultLayout, ui::widgets::TitleBarHeight());

    DrawWorkspaceExplorer();
    DrawFileChanges();
    DrawLog();
    DrawConfirmationPopup();
    DrawMainSubmitPopup();
    DrawShelvePopup();
    DrawCreateShelfPopup();
    DrawCreateBranchPopup();
}

void AppUi::DrawAppTitleBar()
{
    const std::string subtitle = m_hasSourcePath ? m_sourcePath.filename().string() : std::string(P4VGIT_VERSION_STRING);
    const ui::widgets::TitleBarResult titleBar = ui::widgets::DrawTitleBar(P4VGIT_APP_NAME, subtitle, m_window != nullptr && m_window->IsMaximized());
    if (m_window == nullptr)
        return;

    m_window->SetTitleBarHitTestRegion({ true, ui::widgets::TitleBarHeight(), titleBar.dragRegionRight });

    if (titleBar.close)
        m_window->RequestClose();
    if (titleBar.minimize)
        m_window->Minimize();
    if (titleBar.maximize)
        m_window->ToggleMaximize();
    if (titleBar.drag)
        m_window->StartMoveDrag();
}

void AppUi::DrawWorkspaceExplorer()
{
    if (ui::widgets::BeginWindow("Workspace Explorer"))
    {
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
            if (!m_branches.empty())
            {
                ui::widgets::Text("Target Branch");
                constexpr float createBranchButtonWidth = 28.0f;
                constexpr float branchControlGap = 8.0f;
                const float branchComboWidth = std::max(120.0f, ui::widgets::AvailableWidth() - createBranchButtonWidth - branchControlGap);
                ui::widgets::SetNextItemWidth(branchComboWidth);
                if (ui::widgets::BeginCombo("##TargetBranch", m_targetBranch))
                {
                    const bool blockBranchSwitch = HasUnshelvedChanges();
                    for (const std::string& branch : m_branches)
                    {
                        const bool canSelectBranch = branch == m_targetBranch || !blockBranchSwitch;
                        if (ui::widgets::Selectable(branch, branch == m_targetBranch, canSelectBranch))
                            SelectTargetBranch(branch);
                    }

                    ui::widgets::EndCombo();
                }

                ui::widgets::SameLine();
                const bool canOpenCreateBranch = !HasUnshelvedChanges() && !IsShelfBusy(m_targetBranch);
                ui::widgets::BeginDisabled(!canOpenCreateBranch);
                if (ui::widgets::Button("+"))
                    OpenCreateBranchPopup();
                ui::widgets::EndDisabled();

                if (HasUnshelvedChanges())
                    ui::widgets::Text("Shelve or revert active changes before switching branches.");
            }

            if (m_mainBehindCount > 0)
            {
                ui::widgets::Text(m_targetBranch + " has " + std::to_string(m_mainBehindCount) + " update(s).");
                ui::widgets::SameLine();
                const bool canPull = m_currentGitBranch == m_targetBranch && !IsShelfBusy(m_targetBranch);
                ui::widgets::BeginDisabled(!canPull);
                if (ui::widgets::Button("Pull"))
                    PullMain();
                ui::widgets::EndDisabled();
            }
            else if (m_mainRemoteAvailable)
            {
                ui::widgets::Text(m_targetBranch + " is up to date.");
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
        if (!m_hasSourcePath || !m_repository.has_value())
        {
            if (m_repositoryLoadFuture.has_value())
                ui::widgets::Spinner("Loading repository");
            else
                ui::widgets::Text("Select a Git repository folder to inspect changes.");
            ui::widgets::EndWindow();
            return;
        }

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
                const std::vector<std::string> actionFiles = SelectedFilesForShelf(*activeShelf, relativePath);
                RevertCheckedOutFiles(*activeShelf, actionFiles);
            }
        }
        else
        {
            if (ui::widgets::MenuItem("Check out", true))
            {
                SelectShelf(m_targetBranch);
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
        if (shelf.shelf == m_targetBranch)
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
    DrawShelfPanel(m_targetBranch, true);

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
    const std::string title = isMainShelf ? ("Main (" + m_targetBranch + ")") : shelf;
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
            if (ui::widgets::MenuItem("Submit", !shelfBusy))
            {
                SubmitShelf(shelf);
            }

            if (ui::widgets::MenuItem("Shelve", !shelfBusy))
                OpenShelvePopup(shelf);

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
        for (size_t fileIndex = 0; fileIndex < filesCopy.size(); ++fileIndex)
            DrawShelfFile(shelf, filesCopy, fileIndex);
    }

    if (ui::widgets::BeginContextMenuForCurrentWindow("ActiveChangesContext/" + shelf))
    {
        if (ui::widgets::MenuItem("Create Shelf", !IsShelfBusy(m_targetBranch)))
        {
            if (m_selectedActiveShelf != shelf)
            {
                m_selectedActiveShelf = shelf;
                m_selectedActiveFiles.clear();
                m_hasLastSelectedActiveFileIndex = false;
            }
            OpenCreateShelfPopup();
        }

        ui::widgets::EndContextMenu();
    }

    if (!m_selectedActiveFiles.empty() && ui::widgets::DidClickCurrentWindowBlank())
        ClearActiveFileSelection();

    if (!isMainShelf)
    {
        ui::widgets::Separator();
				bool open = ui::widgets::BeginTreeNode("Files in Shelf##" + shelf);

				if (ui::widgets::BeginContextMenuForLastItem())
				{
					if (ui::widgets::MenuItem("Restore", true))
					{
            const std::vector<GitStatusEntry> shelfFiles = ShelfFiles(shelf);
						RestoreShelfFiles(shelf, shelfFiles);
					}
					ui::widgets::EndContextMenu();
				}

        if (open)
        {
            const std::vector<GitStatusEntry> shelfFiles = ShelfFiles(shelf);
            if (shelfFiles.empty())
            {
                ui::widgets::Text("No committed files in this shelf yet.");
            }
            else
            {
                for (size_t fileIndex = 0; fileIndex < shelfFiles.size(); ++fileIndex)
                    DrawShelfCommittedFile(shelf, shelfFiles, fileIndex);
            }

            ui::widgets::EndTreeNode();
        }

    }

    ui::widgets::EndTreeNode();

    if (shelfBusy)
        ui::widgets::EndDisabled();
}

void AppUi::DrawShelfFile(const std::string& shelf, const std::vector<std::string>& files, size_t fileIndex)
{
    if (fileIndex >= files.size())
        return;

    const std::string& file = files[fileIndex];
    const bool selected = IsActiveFileSelected(shelf, file);
    if (ui::widgets::Selectable(file + "##" + shelf + "/" + file, selected))
        SelectActiveFile(shelf, files, fileIndex);

    const std::vector<std::string> dragFiles = SelectedFilesForShelf(shelf, file);
    const std::string dragLabel = dragFiles.size() > 1 ? (std::to_string(dragFiles.size()) + " files") : file;
    ui::widgets::DragDropSource("p4v-git-file", BuildDragPayload(shelf, dragFiles), dragLabel);

    if (ui::widgets::BeginContextMenuForLastItem())
    {
        const std::vector<std::string> actionFiles = SelectedFilesForShelf(shelf, file);
        if (ui::widgets::MenuItem("Diff", true))
            OpenFileDiff(file);
        if (ui::widgets::MenuItem("Revert", true))
            RevertCheckedOutFiles(shelf, actionFiles);
        ui::widgets::EndContextMenu();
    }
}

void AppUi::DrawShelfCommittedFile(const std::string& shelf, const std::vector<GitStatusEntry>& files, size_t fileIndex)
{
    if (fileIndex >= files.size())
        return;

    const GitStatusEntry& file = files[fileIndex];
    const std::string label = file.status + "  " + file.path;
    const bool selected = IsShelfFileSelected(shelf, file.path);
    if (ui::widgets::Selectable(label + "##shelf-file/" + shelf + "/" + file.path, selected))
        SelectShelfFile(shelf, files, fileIndex);

    if (ui::widgets::BeginContextMenuForLastItem())
    {
        const std::vector<std::string> actionFiles = SelectedShelfFilesForShelf(shelf, file.path);
        if (ui::widgets::MenuItem("Restore", !IsShelfBusy(shelf)))
        {
            const bool hasLocalChanges = std::any_of(actionFiles.begin(), actionFiles.end(), [this](const std::string& actionFile) {
                return HasLocalChanges(actionFile);
            });
            if (hasLocalChanges)
                RequestConfirmation(ConfirmationAction::RestoreShelfFile, shelf, actionFiles);
            else
                RestoreShelfFiles(shelf, actionFiles);
        }

        if (ui::widgets::MenuItem("Revert", !IsShelfBusy(shelf)))
            RevertShelfFiles(shelf, actionFiles);
        ui::widgets::EndContextMenu();
    }
}

void AppUi::SelectShelf(std::string_view shelf)
{
    if (shelf.empty() || shelf == m_targetBranch)
    {
        m_selectedBranch = m_targetBranch;
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
    m_selectedBranch = m_targetBranch;
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
    for (const std::string& file : m_workspaceState.CheckedOutFiles(m_targetBranch))
    {
        if (!IsFileActiveInShelf(file))
            files.push_back(file);
    }

    for (const GitStatusEntry& entry : m_statusEntries)
    {
        if (!IsFileActiveInShelf(entry.path) && std::find(files.begin(), files.end(), entry.path) == files.end())
            files.push_back(entry.path);
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

    if (HasLocalChanges(relativePath))
        return m_targetBranch;

    return std::nullopt;
}

bool AppUi::IsFileActiveInShelf(std::string_view relativePath) const
{
    for (const ShelfWorkspaceFiles& shelf : m_workspaceState.Shelves())
    {
        if (shelf.shelf == m_targetBranch)
            continue;

        if (std::find(shelf.files.begin(), shelf.files.end(), relativePath) != shelf.files.end())
            return true;
    }

    return false;
}

bool AppUi::HasUnshelvedChanges() const
{
    if (!m_statusEntries.empty())
        return true;

    return std::any_of(m_workspaceState.Shelves().begin(), m_workspaceState.Shelves().end(), [](const ShelfWorkspaceFiles& shelf) {
        return !shelf.files.empty();
    });
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

    return std::any_of(m_statusEntries.begin(), m_statusEntries.end(), [relativePath](const GitStatusEntry& entry) {
        return entry.path == relativePath;
    });
}

bool AppUi::IsActiveFileSelected(std::string_view shelf, std::string_view file) const
{
    if (m_selectedActiveShelf != shelf)
        return false;

    return std::find(m_selectedActiveFiles.begin(), m_selectedActiveFiles.end(), file) != m_selectedActiveFiles.end();
}

std::vector<std::string> AppUi::SelectedFilesForShelf(std::string_view shelf, std::string_view fallbackFile) const
{
    if (m_selectedActiveShelf == shelf &&
        std::find(m_selectedActiveFiles.begin(), m_selectedActiveFiles.end(), fallbackFile) != m_selectedActiveFiles.end())
    {
        return m_selectedActiveFiles;
    }

    return { std::string(fallbackFile) };
}

std::string AppUi::BuildDragPayload(std::string_view shelf, const std::vector<std::string>& files) const
{
    std::string payload(shelf);
    for (const std::string& file : files)
    {
        payload += '\n';
        payload += file;
    }

    return payload;
}

std::vector<std::string> AppUi::ActiveFilesForShelf(std::string_view shelf) const
{
    if (shelf == m_targetBranch)
        return MainActiveFiles();

    const std::vector<std::string>& files = m_workspaceState.CheckedOutFiles(shelf);
    return { files.begin(), files.end() };
}

void AppUi::ClearActiveFileSelection()
{
    m_selectedActiveShelf.clear();
    m_selectedActiveFiles.clear();
    m_hasLastSelectedActiveFileIndex = false;
    m_lastSelectedActiveFileIndex = 0;
}

void AppUi::PruneSelectedActiveFiles()
{
    if (m_selectedActiveShelf.empty() || m_selectedActiveFiles.empty())
        return;

    const std::vector<std::string> activeFiles = ActiveFilesForShelf(m_selectedActiveShelf);
    m_selectedActiveFiles.erase(std::remove_if(m_selectedActiveFiles.begin(), m_selectedActiveFiles.end(), [&activeFiles](const std::string& selectedFile) {
        return std::find(activeFiles.begin(), activeFiles.end(), selectedFile) == activeFiles.end();
    }), m_selectedActiveFiles.end());

    if (m_selectedActiveFiles.empty())
        ClearActiveFileSelection();
}

void AppUi::SelectActiveFile(const std::string& shelf, const std::vector<std::string>& files, size_t fileIndex)
{
    if (fileIndex >= files.size())
        return;

    const bool sameShelf = m_selectedActiveShelf == shelf;
    if (ui::widgets::IsShiftDown() && sameShelf && m_hasLastSelectedActiveFileIndex)
    {
        const size_t first = std::min(m_lastSelectedActiveFileIndex, fileIndex);
        const size_t last = std::max(m_lastSelectedActiveFileIndex, fileIndex);
        m_selectedActiveFiles.clear();
        for (size_t index = first; index <= last && index < files.size(); ++index)
            m_selectedActiveFiles.push_back(files[index]);
    }
    else if (ui::widgets::IsCtrlDown() && sameShelf)
    {
        auto selectedFile = std::find(m_selectedActiveFiles.begin(), m_selectedActiveFiles.end(), files[fileIndex]);
        if (selectedFile == m_selectedActiveFiles.end())
            m_selectedActiveFiles.push_back(files[fileIndex]);
        else
            m_selectedActiveFiles.erase(selectedFile);
    }
    else
    {
        m_selectedActiveFiles = { files[fileIndex] };
    }

    m_selectedActiveShelf = shelf;
    m_lastSelectedActiveFileIndex = fileIndex;
    m_hasLastSelectedActiveFileIndex = true;
}

bool AppUi::IsShelfFileSelected(std::string_view shelf, std::string_view file) const
{
    if (m_selectedShelfFileShelf != shelf)
        return false;

    return std::find(m_selectedShelfFiles.begin(), m_selectedShelfFiles.end(), file) != m_selectedShelfFiles.end();
}

std::vector<std::string> AppUi::SelectedShelfFilesForShelf(std::string_view shelf, std::string_view fallbackFile) const
{
    if (m_selectedShelfFileShelf == shelf &&
        std::find(m_selectedShelfFiles.begin(), m_selectedShelfFiles.end(), fallbackFile) != m_selectedShelfFiles.end())
    {
        return m_selectedShelfFiles;
    }

    return { std::string(fallbackFile) };
}

void AppUi::SelectShelfFile(const std::string& shelf, const std::vector<GitStatusEntry>& files, size_t fileIndex)
{
    if (fileIndex >= files.size())
        return;

    const bool sameShelf = m_selectedShelfFileShelf == shelf;
    if (ui::widgets::IsShiftDown() && sameShelf && m_hasLastSelectedShelfFileIndex)
    {
        const size_t first = std::min(m_lastSelectedShelfFileIndex, fileIndex);
        const size_t last = std::max(m_lastSelectedShelfFileIndex, fileIndex);
        m_selectedShelfFiles.clear();
        for (size_t index = first; index <= last && index < files.size(); ++index)
            m_selectedShelfFiles.push_back(files[index].path);
    }
    else if (ui::widgets::IsCtrlDown() && sameShelf)
    {
        auto selectedFile = std::find(m_selectedShelfFiles.begin(), m_selectedShelfFiles.end(), files[fileIndex].path);
        if (selectedFile == m_selectedShelfFiles.end())
            m_selectedShelfFiles.push_back(files[fileIndex].path);
        else
            m_selectedShelfFiles.erase(selectedFile);
    }
    else
    {
        m_selectedShelfFiles = { files[fileIndex].path };
    }

    m_selectedShelfFileShelf = shelf;
    m_lastSelectedShelfFileIndex = fileIndex;
    m_hasLastSelectedShelfFileIndex = true;
}

void AppUi::MoveCheckedOutFile(std::string_view payload, std::string_view toShelf)
{
    std::vector<std::string_view> lines;
    size_t cursor = 0;
    while (cursor <= payload.size())
    {
        const size_t next = payload.find('\n', cursor);
        if (next == std::string_view::npos)
        {
            lines.push_back(payload.substr(cursor));
            break;
        }

        lines.push_back(payload.substr(cursor, next - cursor));
        cursor = next + 1;
    }

    if (lines.size() < 2)
        return;

    const std::string_view fromShelf = lines.front();
    if (fromShelf == toShelf)
        return;

    std::vector<std::string> movedFiles;
    for (size_t index = 1; index < lines.size(); ++index)
    {
        if (lines[index].empty())
            continue;

        m_workspaceState.MoveCheckedOutFile(fromShelf, toShelf, lines[index]);
        movedFiles.push_back(std::string(lines[index]));
    }

    if (movedFiles.empty())
        return;

    m_selectedActiveShelf = std::string(toShelf);
    m_selectedActiveFiles = movedFiles;
    m_hasLastSelectedActiveFileIndex = false;
    m_workspaceState.Save();
    std::cout << "Moved " << movedFiles.size() << " file(s) from " << fromShelf << " to " << toShelf << '\n';
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

void AppUi::RevertCheckedOutFiles(const std::string& shelf, const std::vector<std::string>& files)
{
    for (const std::string& file : files)
    {
        if (HasLocalChanges(file))
        {
            RequestConfirmation(ConfirmationAction::RevertActiveFile, shelf, files);
            return;
        }
    }

    for (const std::string& file : files)
        RevertCheckedOutFile(shelf, file);
}

void AppUi::RevertCheckedOutFilesConfirmed(const std::string& shelf, const std::vector<std::string>& files)
{
    if (IsShelfBusy(shelf) || files.empty())
        return;

    if (files.size() == 1)
    {
        RevertCheckedOutFile(shelf, files.front());
        return;
    }

    const std::filesystem::path repoRoot = m_sourcePath;
    StartShelfJob(shelf, "Reverting", std::async(std::launch::async, [repoRoot, shelf, files]() {
        return RunRevertActiveFilesJob(repoRoot, shelf, files);
    }));
}

void AppUi::OpenFileDiff(const std::string& file)
{
    if (!m_repository.has_value() || file.empty())
        return;

    const std::filesystem::path repoRoot = m_sourcePath;
    const std::string targetBranch = m_targetBranch;
    std::thread([repoRoot, targetBranch, file]() {
        RunOpenFileDiffJob(repoRoot, targetBranch, file);
    }).detach();
}

void AppUi::RevertShelfFile(const std::string& shelf, const std::string& file)
{
    if (IsShelfBusy(shelf))
        return;

    const std::filesystem::path repoRoot = m_sourcePath;
    const std::string targetBranch = m_targetBranch;
    StartShelfJob(shelf, "Reverting", std::async(std::launch::async, [repoRoot, targetBranch, shelf, file]() {
        return RunRevertShelfFileJob(repoRoot, targetBranch, shelf, file);
    }));
}

void AppUi::RevertShelfFiles(const std::string& shelf, const std::vector<std::string>& files)
{
    if (IsShelfBusy(shelf) || files.empty())
        return;

    if (files.size() == 1)
    {
        RevertShelfFile(shelf, files.front());
        return;
    }

    const std::filesystem::path repoRoot = m_sourcePath;
    const std::string targetBranch = m_targetBranch;
    StartShelfJob(shelf, "Reverting", std::async(std::launch::async, [repoRoot, targetBranch, shelf, files]() {
        return RunRevertShelfFilesJob(repoRoot, targetBranch, shelf, files);
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

void AppUi::RestoreShelfFiles(const std::string& shelf, const std::vector<std::string>& files)
{
    if (IsShelfBusy(shelf) || files.empty())
        return;

    if (files.size() == 1)
    {
        RestoreShelfFile(shelf, files.front());
        return;
    }

    const std::filesystem::path repoRoot = m_sourcePath;
    StartShelfJob(shelf, "Restoring", std::async(std::launch::async, [repoRoot, shelf, files]() {
        return RunRestoreShelfFilesJob(repoRoot, shelf, files);
    }));
}

void AppUi::RestoreShelfFiles(const std::string& shelf, const std::vector<GitStatusEntry>& files)
{
    std::vector<std::string> paths;
    paths.reserve(files.size());
    for (const GitStatusEntry& file : files)
        paths.push_back(file.path);

    RestoreShelfFiles(shelf, paths);
}

void AppUi::CreateShelfFromInput()
{
    if (!m_repository.has_value())
        return;

    const std::string shelfName = m_newShelfNameInput.data();
    if (shelfName.empty())
        return;

    const std::filesystem::path repoRoot = m_sourcePath;
    const std::string targetBranch = m_targetBranch;
    std::vector<std::string> files;
    if (!m_selectedActiveShelf.empty())
        files = m_selectedActiveFiles;

    StartShelfJob(targetBranch, "Creating shelf", std::async(std::launch::async, [repoRoot, targetBranch, shelfName, files = std::move(files)]() {
        return RunCreateShelfJob(repoRoot, targetBranch, shelfName, files);
    }));
    std::fill(m_newShelfNameInput.begin(), m_newShelfNameInput.end(), '\0');
}

void AppUi::CreateTargetBranchFromInput()
{
    if (!m_repository.has_value())
        return;

    const std::string branchName = m_newBranchNameInput.data();
    if (branchName.empty())
        return;

    if (HasUnshelvedChanges())
    {
        m_createBranchError = "Shelve or revert active changes before creating a branch.";
        return;
    }

    if (std::find(m_branches.begin(), m_branches.end(), branchName) != m_branches.end())
    {
        m_createBranchError = "Branch already exists.";
        return;
    }

    const std::filesystem::path repoRoot = m_sourcePath;
    const std::string baseBranch = m_targetBranch;
    StartShelfJob(m_targetBranch, "Creating branch", std::async(std::launch::async, [repoRoot, baseBranch, branchName]() {
        return RunCreateTargetBranchJob(repoRoot, baseBranch, branchName);
    }));
    std::fill(m_newBranchNameInput.begin(), m_newBranchNameInput.end(), '\0');
}

void AppUi::SelectTargetBranch(const std::string& branch)
{
    if (!m_repository.has_value() || branch.empty() || branch == m_targetBranch || IsShelfBusy(m_targetBranch))
        return;

    if (HasUnshelvedChanges())
    {
        std::cout << "Cannot switch target branch while there are unshelved active changes.\n";
        return;
    }

    const std::filesystem::path repoRoot = m_sourcePath;
    StartShelfJob(m_targetBranch, "Switching branch", std::async(std::launch::async, [repoRoot, branch]() {
        return RunSelectTargetBranchJob(repoRoot, branch);
    }));
}

void AppUi::ShelveShelf(const std::string& shelf)
{
    if (IsShelfBusy(shelf))
        return;

    const std::filesystem::path repoRoot = m_sourcePath;
    const std::string targetBranch = m_targetBranch;
    const std::vector<std::string> files = m_workspaceState.CheckedOutFiles(shelf);
    const std::string summary = m_shelveSummaryInput.data();
    const std::string description = m_shelveDescriptionInput.data();
    if (files.empty())
    {
        m_shelveError = "No files to shelve.";
        return;
    }
    if (summary.empty())
    {
        m_shelveError = "Summary is required.";
        return;
    }

    StartShelfJob(shelf, "Shelving", std::async(std::launch::async, [repoRoot, targetBranch, shelf, files, summary, description]() {
        return RunShelveShelfJob(repoRoot, targetBranch, shelf, files, summary, description);
    }));
}

void AppUi::ShelveAndSubmitShelf(const std::string& shelf)
{
    if (IsShelfBusy(shelf))
        return;

    const std::filesystem::path repoRoot = m_sourcePath;
    const std::string targetBranch = m_targetBranch;
    const std::vector<std::string> files = m_workspaceState.CheckedOutFiles(shelf);
    const std::string summary = m_shelveSummaryInput.data();
    const std::string description = m_shelveDescriptionInput.data();
    if (files.empty())
    {
        m_shelveError = "No files to shelve before submitting.";
        return;
    }
    if (summary.empty())
    {
        m_shelveError = "Summary is required.";
        return;
    }

    StartShelfJob(shelf, "Shelving and submitting", std::async(std::launch::async, [repoRoot, targetBranch, shelf, files, summary, description]() {
        return RunShelveAndSubmitShelfJob(repoRoot, targetBranch, shelf, files, summary, description);
    }));
}

void AppUi::SubmitShelf(const std::string& shelf)
{
    if (IsShelfBusy(shelf))
        return;

    const std::filesystem::path repoRoot = m_sourcePath;
    const std::string targetBranch = m_targetBranch;
    const std::vector<std::string> files = m_workspaceState.CheckedOutFiles(shelf);
    StartShelfJob(shelf, "Submitting", std::async(std::launch::async, [repoRoot, targetBranch, shelf, files]() {
        return RunSubmitShelfJob(repoRoot, targetBranch, shelf, files);
    }));
}

void AppUi::SubmitMainFromPopup()
{
    if (IsShelfBusy(m_targetBranch))
        return;

    std::vector<std::string> files = MainSubmittableFiles();
    const std::string summary = m_mainSubmitSummaryInput.data();
    const std::string description = m_mainSubmitDescriptionInput.data();
    if (files.empty())
    {
        m_mainSubmitError = "No changes to submit.";
        return;
    }

    if (summary.empty())
    {
        m_mainSubmitError = "Summary is required.";
        return;
    }

    const std::filesystem::path repoRoot = m_sourcePath;
    const std::string targetBranch = m_targetBranch;
    StartShelfJob(targetBranch, "Submitting", std::async(std::launch::async, [repoRoot, targetBranch, files = std::move(files), summary, description]() {
        return RunSubmitMainJob(repoRoot, targetBranch, files, summary, description);
    }));
}

void AppUi::PullMain()
{
    if (IsShelfBusy(m_targetBranch))
        return;

    const std::filesystem::path repoRoot = m_sourcePath;
    const std::string targetBranch = m_targetBranch;
    StartShelfJob(targetBranch, "Pulling", std::async(std::launch::async, [repoRoot, targetBranch]() {
        return RunPullMainJob(repoRoot, targetBranch);
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
