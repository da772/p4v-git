#include "git/ShelfService.h"

#include "platform/Process.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <system_error>
#include <chrono>
#include <utility>

namespace p4vgit
{
struct GitHubRepositoryInfo
{
    std::string owner;
    std::string name;

    std::string ApiRepositoryPath() const { return owner + "/" + name; }
};

static std::string TrimText(std::string text)
{
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '\t'))
        text.pop_back();

    size_t start = 0;
    while (start < text.size() && (text[start] == ' ' || text[start] == '\t'))
        ++start;

    return text.substr(start);
}

static std::string EnvironmentValue(const char* name)
{
#ifdef _WIN32
    char* value = nullptr;
    size_t valueSize = 0;
    if (_dupenv_s(&value, &valueSize, name) != 0 || value == nullptr)
        return {};

    std::string result = value;
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    if (value == nullptr)
        return {};

    return value;
#endif
}

static std::string GitHubToken()
{
    std::string token = EnvironmentValue("GH_TOKEN");
    if (!token.empty())
        return token;

    return EnvironmentValue("GITHUB_TOKEN");
}

static std::string ShellQuote(std::string_view text)
{
    std::string quoted = "\"";
    for (char ch : text)
    {
        if (ch == '"')
            quoted += "\\\"";
        else
            quoted += ch;
    }
    quoted += '"';
    return quoted;
}

static std::string SafeFileName(std::string_view text)
{
    std::string safe;
    for (const char ch : text)
    {
        if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '.' || ch == '-' || ch == '_')
            safe += ch;
        else
            safe += '_';
    }

    return safe.empty() ? "file" : safe;
}

static GitCommandResult RunExternalCommand(std::string_view label, const std::string& command, bool logOutput = true, bool logCommand = true)
{
    if (logCommand)
        std::cout << label << '\n';

    const ProcessResult processResult = RunHiddenCommand(command);
    GitCommandResult result;
    result.exitCode = processResult.exitCode;
    result.output = processResult.output;
    if (logOutput && !result.output.empty())
        std::cout << result.output << '\n';
    if (logCommand && !result.Succeeded())
        std::cout << "external command failed with exit code " << result.exitCode << '\n';

    return result;
}

static std::string UrlEncode(std::string_view text)
{
    const char* hex = "0123456789ABCDEF";
    std::string encoded;

    for (const unsigned char ch : text)
    {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~')
        {
            encoded += static_cast<char>(ch);
            continue;
        }

        encoded += '%';
        encoded += hex[(ch >> 4) & 0x0F];
        encoded += hex[ch & 0x0F];
    }

    return encoded;
}

static std::string JsonEscape(std::string_view text)
{
    std::string escaped;
    for (const char ch : text)
    {
        switch (ch)
        {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += ch;
            break;
        }
    }

    return escaped;
}

static std::string JsonStringValue(std::string_view json, std::string_view key)
{
    const std::string keyPattern = "\"" + std::string(key) + "\"";
    const size_t keyPosition = json.find(keyPattern);
    if (keyPosition == std::string_view::npos)
        return {};

    const size_t colonPosition = json.find(':', keyPosition + keyPattern.size());
    if (colonPosition == std::string_view::npos)
        return {};

    const size_t quotePosition = json.find('"', colonPosition + 1);
    if (quotePosition == std::string_view::npos)
        return {};

    std::string value;
    bool escaped = false;
    for (size_t index = quotePosition + 1; index < json.size(); ++index)
    {
        const char ch = json[index];
        if (escaped)
        {
            value += ch;
            escaped = false;
            continue;
        }

        if (ch == '\\')
        {
            escaped = true;
            continue;
        }

        if (ch == '"')
            return value;

        value += ch;
    }

    return {};
}

static std::optional<bool> JsonBooleanValue(std::string_view json, std::string_view key)
{
    const std::string keyPattern = "\"" + std::string(key) + "\"";
    const size_t keyPosition = json.find(keyPattern);
    if (keyPosition == std::string_view::npos)
        return std::nullopt;

    const size_t colonPosition = json.find(':', keyPosition + keyPattern.size());
    if (colonPosition == std::string_view::npos)
        return std::nullopt;

    size_t valuePosition = colonPosition + 1;
    while (valuePosition < json.size() && std::isspace(static_cast<unsigned char>(json[valuePosition])))
        ++valuePosition;

    if (json.substr(valuePosition, 4) == "true")
        return true;
    if (json.substr(valuePosition, 5) == "false")
        return false;

    return std::nullopt;
}

static std::optional<int> ShelfNumberFromUrl(std::string_view shelfUrl)
{
    constexpr std::string_view marker = "/pull/";
    const size_t markerPosition = shelfUrl.rfind(marker);
    if (markerPosition == std::string_view::npos)
        return std::nullopt;

    size_t valuePosition = markerPosition + marker.size();
    int value = 0;
    bool hasDigit = false;
    while (valuePosition < shelfUrl.size() && std::isdigit(static_cast<unsigned char>(shelfUrl[valuePosition])))
    {
        hasDigit = true;
        value = value * 10 + (shelfUrl[valuePosition] - '0');
        ++valuePosition;
    }

    if (!hasDigit)
        return std::nullopt;

    return value;
}

static std::optional<GitHubRepositoryInfo> ParseGitHubRemote(std::string remoteUrl)
{
    remoteUrl = TrimText(remoteUrl);
    if (remoteUrl.ends_with(".git"))
        remoteUrl.resize(remoteUrl.size() - 4);

    const std::string httpsPrefix = "https://github.com/";
    const std::string sshPrefix = "git@github.com:";
    const std::string sshUrlPrefix = "ssh://git@github.com/";

    if (remoteUrl.rfind(httpsPrefix, 0) == 0)
        remoteUrl = remoteUrl.substr(httpsPrefix.size());
    else if (remoteUrl.rfind(sshPrefix, 0) == 0)
        remoteUrl = remoteUrl.substr(sshPrefix.size());
    else if (remoteUrl.rfind(sshUrlPrefix, 0) == 0)
        remoteUrl = remoteUrl.substr(sshUrlPrefix.size());
    else
        return std::nullopt;

    const size_t separator = remoteUrl.find('/');
    if (separator == std::string::npos || separator == 0 || separator + 1 >= remoteUrl.size())
        return std::nullopt;

    return GitHubRepositoryInfo{
        remoteUrl.substr(0, separator),
        remoteUrl.substr(separator + 1),
    };
}

static std::string CredentialPassword(std::string_view credentialOutput)
{
    std::istringstream stream{ std::string(credentialOutput) };
    std::string line;
    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        constexpr std::string_view passwordPrefix = "password=";
        if (line.rfind(passwordPrefix, 0) == 0)
            return line.substr(passwordPrefix.size());
    }

    return {};
}

static std::string GitHubTokenFromCredentialHelper(const GitRepository& repository, bool logCommand)
{
    const std::filesystem::path requestDirectory = repository.GitDir(logCommand) / "p4v-git";
    std::error_code error;
    std::filesystem::create_directories(requestDirectory, error);

    const std::filesystem::path requestPath = requestDirectory / "github-credential-input.txt";
    {
        std::ofstream requestFile(requestPath, std::ios::trunc);
        if (!requestFile.is_open())
            return {};

        requestFile << "protocol=https\n"
                    << "host=github.com\n"
                    << "\n";
    }

    const GitCommandResult result = RunExternalCommand(
        "Git credential helper: checking for GitHub API credentials",
        "git -C " + ShellQuote(repository.Root().string()) + " credential fill < " + ShellQuote(requestPath.string()) + " 2>&1",
        false,
        logCommand);
    if (!result.Succeeded())
        return {};

    return CredentialPassword(result.output);
}

static std::string GitHubAuthToken(const GitRepository& repository, bool logCommand = true)
{
    std::string token = GitHubToken();
    if (!token.empty())
        return token;

    return GitHubTokenFromCredentialHelper(repository, logCommand);
}

static std::string GitHubApiHeaders(const std::string& token)
{
    std::string headers = "-H " + ShellQuote("Accept: application/vnd.github+json") + " ";
    headers += "-H " + ShellQuote("X-GitHub-Api-Version: 2022-11-28") + " ";
    if (!token.empty())
        headers += "-H " + ShellQuote("Authorization: Bearer " + token) + " ";

    return headers;
}

static std::string FindGitHubShelfUrl(const GitRepository& repository, std::string_view shelfBranch, std::string_view baseBranch, bool logCommand, std::string_view state)
{
    if (shelfBranch.empty())
        return {};

    const std::optional<GitHubRepositoryInfo> repositoryInfo = ParseGitHubRemote(repository.RemoteUrl("origin", logCommand));
    if (!repositoryInfo.has_value())
    {
        if (logCommand)
            std::cout << "Cannot find shelf link: origin is not a GitHub remote.\n";
        return {};
    }

    const std::string token = GitHubAuthToken(repository, logCommand);
    const std::string branch = std::string(shelfBranch);
    const std::string shelvesUrl = "https://api.github.com/repos/" + repositoryInfo->ApiRepositoryPath() +
        "/pulls?state=" + std::string(state) +
        "&head=" + UrlEncode(repositoryInfo->owner + ":" + branch) +
        "&base=" + UrlEncode(baseBranch) + "&sort=updated&direction=desc";

    const GitCommandResult result = RunExternalCommand(
        "GitHub API: checking for an existing shelf",
        "curl -sS " + GitHubApiHeaders(token) + ShellQuote(shelvesUrl) + " 2>&1",
        logCommand,
        logCommand);

    return JsonStringValue(result.output, "html_url");
}

void ShelfService::SetRepository(GitRepository* repository)
{
    m_repository = repository;
}

void ShelfService::SetTargetBranch(std::string branch)
{
    m_targetBranch = branch.empty() ? "main" : std::move(branch);
}

std::string ShelfService::UserPrefix() const
{
    return BuildUserPrefix(true);
}

std::string ShelfService::BuildUserPrefix(bool logCommand) const
{
    if (m_repository == nullptr)
        return "shelves/user/" + SanitizeBranchPart(TargetBranch()) + "/";

    return "shelves/" + SanitizeBranchPart(m_repository->UserName(logCommand)) + "/" + SanitizeBranchPart(TargetBranch()) + "/";
}

std::string ShelfService::MakeShelfBranch(std::string_view shelfName) const
{
    return UserPrefix() + SanitizeBranchPart(std::string(shelfName));
}

std::vector<std::string> ShelfService::Shelves(bool logCommand) const
{
    std::vector<std::string> shelves;
    if (m_repository == nullptr)
        return shelves;

    const std::string prefix = BuildUserPrefix(logCommand);
    for (const std::string& branch : m_repository->LocalBranches(logCommand))
    {
        if (branch.rfind(prefix, 0) == 0)
            shelves.push_back(branch);
    }

    return shelves;
}

bool ShelfService::IsShelfValid(std::string_view shelfBranch, bool logCommand) const
{
    if (m_repository == nullptr || shelfBranch.empty() || shelfBranch == TargetBranch())
        return false;

    const std::string prefix = BuildUserPrefix(logCommand);
    if (shelfBranch.rfind(prefix, 0) != 0)
        return false;

    return m_repository->BranchExists(shelfBranch, logCommand);
}

std::string ShelfService::FindShelfLink(std::string_view shelfBranch, bool logCommand) const
{
    if (m_repository == nullptr || !IsShelfValid(shelfBranch, logCommand))
        return {};

    return FindGitHubShelfUrl(*m_repository, shelfBranch, TargetBranch(), logCommand, "all");
}

std::vector<GitStatusEntry> ShelfService::ShelfFiles(std::string_view shelfBranch, bool logCommand) const
{
    std::vector<GitStatusEntry> files;
    if (m_repository == nullptr || !IsShelfValid(shelfBranch, logCommand))
        return files;

    const std::string range = TargetBranch() + "..." + std::string(shelfBranch);
    std::istringstream stream(m_repository->Run("diff --name-status " + Quote(range) + " --", logCommand).output);
    std::string line;
    while (std::getline(stream, line))
    {
        line = TrimText(line);
        if (line.empty())
            continue;

        const size_t firstTab = line.find('\t');
        if (firstTab == std::string::npos)
            continue;

        const size_t lastTab = line.rfind('\t');
        files.push_back({
            line.substr(lastTab + 1),
            line.substr(0, firstTab),
        });
    }

    return files;
}

bool ShelfService::CreateShelf(std::string_view shelfName)
{
    if (m_repository == nullptr)
        return false;

    const std::string branch = MakeShelfBranch(shelfName);
    if (m_repository->BranchExists(branch))
        return true;

    return m_repository->Run("branch " + Quote(branch) + " " + Quote(TargetBranch())).Succeeded();
}

bool ShelfService::ShelveFiles(std::string_view shelfBranch, const std::vector<std::string>& files, std::string_view summary, std::string_view description)
{
    if (m_repository == nullptr || shelfBranch.empty() || files.empty() || summary.empty())
        return false;

    if (!m_repository->BranchExists(shelfBranch))
    {
        if (!m_repository->Run("branch " + Quote(shelfBranch)).Succeeded())
            return false;
    }

    const std::filesystem::path worktreeRoot = m_repository->GitDir() / "p4v-git" / "worktrees" / SanitizeBranchPart(std::string(shelfBranch));
    std::error_code error;
    std::filesystem::remove_all(worktreeRoot, error);
    std::filesystem::create_directories(worktreeRoot.parent_path(), error);

    if (!m_repository->Run("worktree add --force " + Quote(worktreeRoot.string()) + " " + Quote(shelfBranch)).Succeeded())
        return false;

    bool addSucceeded = true;
    for (const std::string& file : files)
    {
        const std::filesystem::path sourcePath = m_repository->Root() / file;
        const std::filesystem::path targetPath = worktreeRoot / file;
        std::filesystem::create_directories(targetPath.parent_path(), error);

        if (std::filesystem::exists(sourcePath))
            std::filesystem::copy_file(sourcePath, targetPath, std::filesystem::copy_options::overwrite_existing, error);
        else
            std::filesystem::remove(targetPath, error);

        GitRepository shelfWorktree(worktreeRoot);
        addSucceeded = shelfWorktree.Run("add -- " + Quote(file)).Succeeded() && addSucceeded;
    }

    bool committed = false;
    if (addSucceeded)
    {
        GitRepository shelfWorktree(worktreeRoot);
        std::string commitCommand = "commit -m " + Quote(summary);
        if (!description.empty())
            commitCommand += " -m " + Quote(description);
        committed = shelfWorktree.Run(commitCommand).Succeeded();
    }

    m_repository->Run("worktree remove --force " + Quote(worktreeRoot.string()));

    return addSucceeded && committed;
}

bool ShelfService::RevertFileFromShelf(std::string_view shelfBranch, std::string_view file)
{
    if (m_repository == nullptr || shelfBranch.empty() || file.empty() || !m_repository->BranchExists(shelfBranch))
        return false;

    const std::filesystem::path worktreeRoot = m_repository->GitDir() / "p4v-git" / "worktrees" / (SanitizeBranchPart(std::string(shelfBranch)) + "-remove");
    std::error_code error;
    std::filesystem::remove_all(worktreeRoot, error);
    std::filesystem::create_directories(worktreeRoot.parent_path(), error);

    if (!m_repository->Run("worktree add --force " + Quote(worktreeRoot.string()) + " " + Quote(shelfBranch)).Succeeded())
        return false;

    GitRepository shelfWorktree(worktreeRoot);
    const std::string fileText = std::string(file);
    const bool restoredFromMain = shelfWorktree.Run("checkout " + Quote(TargetBranch()) + " -- " + Quote(fileText)).Succeeded();
    if (!restoredFromMain)
        shelfWorktree.Run("rm --ignore-unmatch -- " + Quote(fileText));

    const bool addSucceeded = shelfWorktree.Run("add -- " + Quote(fileText)).Succeeded();
    const GitCommandResult commitResult = shelfWorktree.Run("commit -m " + Quote("Revert file from shelf"));
    m_repository->Run("worktree remove --force " + Quote(worktreeRoot.string()));

    if (commitResult.Succeeded())
    {
        if (!addSucceeded)
            return false;

        return m_repository->Run("push -u origin " + Quote(shelfBranch)).Succeeded();
    }

    return commitResult.output.find("nothing to commit") != std::string::npos ||
           commitResult.output.find("no changes added to commit") != std::string::npos;
}

bool ShelfService::RestoreFileFromShelfToWorkingTree(std::string_view shelfBranch, std::string_view file)
{
    if (m_repository == nullptr || shelfBranch.empty() || file.empty() || !m_repository->BranchExists(shelfBranch))
        return false;

    const std::string fileText = std::string(file);
    const std::string objectName = std::string(shelfBranch) + ":" + fileText;
    if (m_repository->Run("cat-file -e " + Quote(objectName)).Succeeded())
        return m_repository->Run("checkout " + Quote(shelfBranch) + " -- " + Quote(fileText)).Succeeded();

    std::error_code error;
    const std::filesystem::path filePath = m_repository->Root() / fileText;
    if (std::filesystem::exists(filePath, error))
        std::filesystem::remove(filePath, error);

    return !error;
}

bool ShelfService::UndoLocalFileChanges(std::string_view file)
{
    if (m_repository == nullptr || file.empty())
        return false;

    const std::string fileText = std::string(file);
    if (m_repository->Run("restore --staged --worktree -- " + Quote(fileText)).Succeeded())
        return true;

    return m_repository->Run("clean -f -- " + Quote(fileText)).Succeeded();
}

bool ShelfService::OpenFileDiff(std::string_view file) const
{
    if (m_repository == nullptr || file.empty())
        return false;

    const std::string fileText = std::string(file);
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path diffDirectory = m_repository->GitDir(false) / "p4v-git" / "diffs";
    std::error_code error;
    std::filesystem::create_directories(diffDirectory, error);
    if (error)
    {
        std::cout << "Cannot open diff: failed to create temp diff directory.\n";
        return false;
    }

    const std::string safeName = SafeFileName(TargetBranch()) + "_" + std::to_string(timestamp) + "_" + SafeFileName(fileText);
    const std::filesystem::path basePath = diffDirectory / ("base_" + safeName);
    const std::filesystem::path workingFallbackPath = diffDirectory / ("working_" + safeName);
    const std::filesystem::path workingPath = m_repository->Root() / fileText;

    const GitCommandResult baseResult = m_repository->Run("show " + Quote(TargetBranch() + ":" + fileText), false);
    {
        std::ofstream baseFile(basePath, std::ios::binary | std::ios::trunc);
        if (!baseFile.is_open())
        {
            std::cout << "Cannot open diff: failed to write base temp file.\n";
            return false;
        }

        if (baseResult.Succeeded())
            baseFile << baseResult.output;
    }

    std::filesystem::path rightPath = workingPath;
    if (!std::filesystem::exists(workingPath, error))
    {
        std::ofstream workingFile(workingFallbackPath, std::ios::binary | std::ios::trunc);
        if (!workingFile.is_open())
        {
            std::cout << "Cannot open diff: failed to write working temp file.\n";
            return false;
        }
        rightPath = workingFallbackPath;
    }

    const GitCommandResult result = RunExternalCommand(
        "Opening diff: VS Code",
        "code --reuse-window --diff " + ShellQuote(basePath.string()) + " " + ShellQuote(rightPath.string()) + " 2>&1",
        true,
        true);

    if (!result.Succeeded())
        std::cout << "Cannot open VS Code diff. Make sure the `code` command is available on PATH.\n";

    return result.Succeeded();
}

bool ShelfService::SubmitMain(const std::vector<std::string>& files, std::string_view summary, std::string_view description)
{
    if (m_repository == nullptr || files.empty() || summary.empty())
        return false;

    if (!PullMainForSubmit())
        return false;

    std::string addCommand = "add -A --";
    for (const std::string& file : files)
        addCommand += " " + Quote(file);

    if (!m_repository->Run(addCommand).Succeeded())
        return false;

    std::string commitCommand = "commit -m " + Quote(summary);
    if (!description.empty())
        commitCommand += " -m " + Quote(description);

    if (!m_repository->Run(commitCommand).Succeeded())
        return false;

    return m_repository->Run("push origin " + Quote(TargetBranch())).Succeeded();
}

MainSyncStatus ShelfService::RefreshMain(bool logCommand) const
{
    MainSyncStatus status;
    if (m_repository == nullptr)
        return status;

    const std::string remoteRef = "origin/" + TargetBranch();
    status.fetched = m_repository->Run("fetch origin " + Quote(TargetBranch()), logCommand).Succeeded();
    status.remoteAvailable = m_repository->Run("rev-parse --verify --quiet " + Quote(remoteRef), logCommand).Succeeded();
    if (!status.remoteAvailable)
        return status;

    const std::string countText = TrimText(m_repository->Run("rev-list --count " + Quote(TargetBranch() + ".." + remoteRef), logCommand).output);
    if (!countText.empty())
    {
        try
        {
            status.behindCount = std::max(0, std::stoi(countText));
        }
        catch (...)
        {
            status.behindCount = 0;
        }
    }

    return status;
}

bool ShelfService::PullMain()
{
    return PullMainForSubmit();
}

bool ShelfService::PullMainForSubmit()
{
    if (m_repository == nullptr)
        return false;

    if (m_repository->CurrentBranch() != TargetBranch())
    {
        std::cout << "Cannot pull: current Git branch is not " << TargetBranch() << ".\n";
        return false;
    }

    if (!m_repository->Run("fetch origin " + Quote(TargetBranch())).Succeeded())
        return false;

    const GitCommandResult pullResult = m_repository->Run("pull --no-rebase --autostash origin " + Quote(TargetBranch()));
    if (pullResult.Succeeded())
        return true;

    std::cout << "Pull failed. Resolve merge issues before submitting.\n";
    OpenDefaultMergeTool();
    return false;
}

bool ShelfService::OpenDefaultMergeTool() const
{
    if (m_repository == nullptr)
        return false;

    const GitCommandResult result = RunExternalCommand(
        "Opening merge tool: VS Code",
        "code --reuse-window " + ShellQuote(m_repository->Root().string()) + " 2>&1",
        true,
        true);

    if (!result.Succeeded())
        std::cout << "Cannot open VS Code. Make sure the `code` command is available on PATH.\n";

    return result.Succeeded();
}

std::string ShelfService::EnsureShelfLink(std::string_view shelfBranch)
{
    if (m_repository == nullptr || shelfBranch.empty())
        return {};

    if (!m_repository->Run("push -u origin " + Quote(shelfBranch)).Succeeded())
        return {};

    const std::optional<GitHubRepositoryInfo> repositoryInfo = ParseGitHubRemote(m_repository->RemoteUrl("origin"));
    if (!repositoryInfo.has_value())
    {
        std::cout << "Cannot create shelf link: origin is not a GitHub remote.\n";
        return {};
    }

    const std::string token = GitHubAuthToken(*m_repository);
    const std::string branch = std::string(shelfBranch);
    const std::string shelvesUrl = "https://api.github.com/repos/" + repositoryInfo->ApiRepositoryPath() +
        "/pulls?state=open&head=" + UrlEncode(repositoryInfo->owner + ":" + branch) + "&base=" + UrlEncode(TargetBranch());

    const GitCommandResult existingResult = RunExternalCommand(
        "GitHub API: checking for an existing shelf",
        "curl -sS " + GitHubApiHeaders(token) + ShellQuote(shelvesUrl) + " 2>&1");
    const std::string existingUrl = JsonStringValue(existingResult.output, "html_url");
    if (!existingUrl.empty())
        return existingUrl;

    if (token.empty())
    {
        std::cout << "Cannot create shelf link: sign in with Git Credential Manager or set GH_TOKEN/GITHUB_TOKEN with GitHub repo access.\n";
        return {};
    }

    const std::filesystem::path requestDirectory = m_repository->GitDir() / "p4v-git";
    std::error_code error;
    std::filesystem::create_directories(requestDirectory, error);

    const std::filesystem::path requestPath = requestDirectory / "github-shelf-create.json";
    const std::string title = "Shelf: " + branch;
    const std::string body = "Created by p4v-git from shelf `" + branch + "`.";
    {
        std::ofstream requestFile(requestPath, std::ios::trunc);
        if (!requestFile.is_open())
        {
            std::cout << "Cannot create shelf link: failed to write GitHub API request file.\n";
            return {};
        }

        requestFile << "{"
                    << "\"title\":\"" << JsonEscape(title) << "\","
                    << "\"head\":\"" << JsonEscape(branch) << "\","
                    << "\"base\":\"" << JsonEscape(TargetBranch()) << "\","
                    << "\"body\":\"" << JsonEscape(body) << "\""
                    << "}";
    }

    const std::string createUrl = "https://api.github.com/repos/" + repositoryInfo->ApiRepositoryPath() + "/pulls";
    const std::string dataArgument = "@" + requestPath.string();
    const GitCommandResult createResult = RunExternalCommand(
        "GitHub API: creating shelf",
        "curl -sS -X POST " + GitHubApiHeaders(token) + ShellQuote(createUrl) + " --data-binary " + ShellQuote(dataArgument) + " 2>&1");
    const std::string createdUrl = JsonStringValue(createResult.output, "html_url");
    if (!createdUrl.empty())
        return createdUrl;

    const std::string message = JsonStringValue(createResult.output, "message");
    if (!message.empty())
        std::cout << "Cannot create shelf link: " << message << '\n';

    return {};
}

std::string ShelfService::ShelveFilesAndEnsureShelfLink(std::string_view shelfBranch, const std::vector<std::string>& files, std::string_view summary, std::string_view description)
{
    if (m_repository == nullptr || shelfBranch.empty())
        return {};

    if (!files.empty() && !ShelveFiles(shelfBranch, files, summary, description))
        return {};

    return EnsureShelfLink(shelfBranch);
}

ShelfSubmitResult ShelfService::SubmitShelf(std::string_view shelfBranch, const std::vector<std::string>& files)
{
    ShelfSubmitResult submitResult;
    if (m_repository == nullptr || shelfBranch.empty())
        return submitResult;

    if (!PullMainForSubmit())
        return submitResult;

    const std::string shelfUrl = EnsureShelfLink(shelfBranch);
    if (shelfUrl.empty())
        return submitResult;
    submitResult.shelfUrl = shelfUrl;

    const std::optional<GitHubRepositoryInfo> repositoryInfo = ParseGitHubRemote(m_repository->RemoteUrl("origin"));
    if (!repositoryInfo.has_value())
    {
        std::cout << "Cannot submit shelf: origin is not a GitHub remote.\n";
        return submitResult;
    }

    const std::string token = GitHubAuthToken(*m_repository);
    if (token.empty())
    {
        std::cout << "Cannot submit shelf: sign in with Git Credential Manager or set GH_TOKEN/GITHUB_TOKEN with GitHub repo access.\n";
        return submitResult;
    }

    const std::optional<int> shelfNumber = ShelfNumberFromUrl(shelfUrl);
    if (!shelfNumber.has_value())
    {
        std::cout << "Cannot submit shelf: failed to determine shelf number from " << shelfUrl << '\n';
        return submitResult;
    }

    const std::filesystem::path requestDirectory = m_repository->GitDir() / "p4v-git";
    std::error_code error;
    std::filesystem::create_directories(requestDirectory, error);

    const std::string branch = std::string(shelfBranch);
    const std::filesystem::path requestPath = requestDirectory / "github-shelf-merge.json";
    {
        std::ofstream requestFile(requestPath, std::ios::trunc);
        if (!requestFile.is_open())
        {
            std::cout << "Cannot submit shelf: failed to write GitHub API request file.\n";
            return submitResult;
        }

        requestFile << "{"
                    << "\"commit_title\":\"Submit " << JsonEscape(branch) << "\","
                    << "\"commit_message\":\"Merged by p4v-git.\","
                    << "\"merge_method\":\"merge\""
                    << "}";
    }

    const std::string mergeUrl = "https://api.github.com/repos/" + repositoryInfo->ApiRepositoryPath() +
        "/pulls/" + std::to_string(*shelfNumber) + "/merge";
    const std::string dataArgument = "@" + requestPath.string();
    const GitCommandResult mergeResult = RunExternalCommand(
        "GitHub API: submitting shelf into " + TargetBranch(),
        "curl -sS -X PUT " + GitHubApiHeaders(token) + ShellQuote(mergeUrl) + " --data-binary " + ShellQuote(dataArgument) + " 2>&1");

    const std::optional<bool> merged = JsonBooleanValue(mergeResult.output, "merged");
    if (merged.value_or(false))
    {
        std::cout << "Submitted shelf into " << TargetBranch() << ": " << shelfUrl << '\n';
        submitResult.merged = true;
        submitResult.branchDeleted = DeleteShelf(shelfBranch, true);
        RestoreFilesFromMain(files);
        return submitResult;
    }

    const std::string message = JsonStringValue(mergeResult.output, "message");
    if (!message.empty())
        std::cout << "Cannot submit shelf: " << message << '\n';
    else
        std::cout << "Cannot submit shelf: GitHub did not report a successful merge.\n";

    return submitResult;
}

bool ShelfService::RestoreFilesFromMain(const std::vector<std::string>& files)
{
    if (m_repository == nullptr || files.empty())
        return true;

    bool succeeded = m_repository->Run("fetch origin " + Quote(TargetBranch())).Succeeded();
    bool pulledMain = false;
    if (m_repository->CurrentBranch() == TargetBranch())
        pulledMain = PullMainForSubmit();
    succeeded = pulledMain && succeeded;

    for (const std::string& file : files)
    {
        const std::string fileText = std::string(file);
        const std::string objectName = "origin/" + TargetBranch() + ":" + fileText;
        if (pulledMain && m_repository->Run("cat-file -e " + Quote(objectName), false).Succeeded())
            succeeded = m_repository->Run("checkout " + Quote("origin/" + TargetBranch()) + " -- " + Quote(fileText)).Succeeded() && succeeded;
        else
            succeeded = m_repository->Run("restore --staged --worktree -- " + Quote(fileText)).Succeeded() && succeeded;
    }

    return succeeded;
}

bool ShelfService::DeleteShelf(std::string_view shelfBranch, bool deleteRemote)
{
    if (m_repository == nullptr || shelfBranch.empty())
        return false;

    bool succeeded = true;
    if (deleteRemote)
        succeeded = m_repository->Run("push origin --delete " + Quote(shelfBranch)).Succeeded() && succeeded;

    if (m_repository->BranchExists(shelfBranch))
        succeeded = m_repository->Run("branch -D " + Quote(shelfBranch)).Succeeded() && succeeded;

    return succeeded;
}

std::string ShelfService::SanitizeBranchPart(std::string text)
{
    std::replace_if(text.begin(), text.end(), [](char ch) {
        return !(ch >= 'a' && ch <= 'z') &&
               !(ch >= 'A' && ch <= 'Z') &&
               !(ch >= '0' && ch <= '9') &&
               ch != '-' &&
               ch != '_' &&
               ch != '.';
    }, '-');

    while (!text.empty() && (text.front() == '.' || text.front() == '-'))
        text.erase(text.begin());
    while (!text.empty() && (text.back() == '.' || text.back() == '-'))
        text.pop_back();

    if (text.empty())
        return "shelf";

    return text;
}

std::string ShelfService::Quote(std::string_view text)
{
    std::string quoted = "\"";
    for (char ch : text)
    {
        if (ch == '"')
            quoted += "\\\"";
        else
            quoted += ch;
    }
    quoted += '"';
    return quoted;
}
}
