#include "git/WorkspaceState.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace p4vgit
{
bool WorkspaceState::Load(const std::filesystem::path& repoRoot)
{
    m_statePath = repoRoot / ".git" / "p4v-git" / "state.json";
    m_activeShelf.clear();
    m_shelfFiles.clear();

    std::ifstream file(m_statePath);
    if (!file)
        return true;

    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string json = buffer.str();

    m_activeShelf = ReadJsonString(json, "activeShelf");
    m_shelfFiles = ReadShelfFiles(json);
    if (m_shelfFiles.empty())
    {
        std::vector<std::string> legacyFiles = ReadJsonStringArray(json, "checkedOutFiles");
        if (!legacyFiles.empty() && !m_activeShelf.empty())
            m_shelfFiles.push_back({ m_activeShelf, std::move(legacyFiles) });
    }
    return true;
}

bool WorkspaceState::Save() const
{
    std::filesystem::create_directories(m_statePath.parent_path());

    std::ofstream file(m_statePath, std::ios::trunc);
    if (!file)
        return false;

    file << "{\n";
    file << "  \"activeShelf\": \"" << Escape(m_activeShelf) << "\",\n";
    file << "  \"shelves\": [\n";
    for (size_t shelfIndex = 0; shelfIndex < m_shelfFiles.size(); ++shelfIndex)
    {
        const ShelfWorkspaceFiles& shelf = m_shelfFiles[shelfIndex];
        file << "    { \"branch\": \"" << Escape(shelf.shelf) << "\", \"checkedOutFiles\": [";
        for (size_t fileIndex = 0; fileIndex < shelf.files.size(); ++fileIndex)
        {
            if (fileIndex > 0)
                file << ", ";
            file << "\"" << Escape(shelf.files[fileIndex]) << "\"";
        }
        file << "] }";
        if (shelfIndex + 1 < m_shelfFiles.size())
            file << ",";
        file << "\n";
    }
    file << "  ]\n";
    file << "}\n";

    return true;
}

void WorkspaceState::SetActiveShelf(std::string shelf)
{
    m_activeShelf = std::move(shelf);
}

const std::vector<std::string>& WorkspaceState::CheckedOutFiles() const
{
    return CheckedOutFiles(m_activeShelf);
}

const std::vector<std::string>& WorkspaceState::CheckedOutFiles(std::string_view shelf) const
{
    static const std::vector<std::string> emptyFiles;
    for (const ShelfWorkspaceFiles& shelfFiles : m_shelfFiles)
    {
        if (shelfFiles.shelf == shelf)
            return shelfFiles.files;
    }

    return emptyFiles;
}

bool WorkspaceState::IsCheckedOut(std::string_view relativePath) const
{
    return std::any_of(m_shelfFiles.begin(), m_shelfFiles.end(), [relativePath](const ShelfWorkspaceFiles& shelfFiles) {
        return std::find(shelfFiles.files.begin(), shelfFiles.files.end(), relativePath) != shelfFiles.files.end();
    });
}

bool WorkspaceState::IsCheckedOut(std::string_view shelf, std::string_view relativePath) const
{
    const std::vector<std::string>& files = CheckedOutFiles(shelf);
    return std::find(files.begin(), files.end(), relativePath) != files.end();
}

void WorkspaceState::CheckOut(std::string relativePath)
{
    CheckOut(m_activeShelf, std::move(relativePath));
}

void WorkspaceState::CheckOut(std::string_view shelf, std::string relativePath)
{
    if (shelf.empty())
        return;

    for (auto shelfIterator = m_shelfFiles.begin(); shelfIterator != m_shelfFiles.end();)
    {
        shelfIterator->files.erase(std::remove(shelfIterator->files.begin(), shelfIterator->files.end(), relativePath), shelfIterator->files.end());
        if (shelfIterator->files.empty())
            shelfIterator = m_shelfFiles.erase(shelfIterator);
        else
            ++shelfIterator;
    }

    MutableCheckedOutFiles(shelf).push_back(std::move(relativePath));
}

void WorkspaceState::MoveCheckedOutFile(std::string_view fromShelf, std::string_view toShelf, std::string_view relativePath)
{
    if (fromShelf.empty() || toShelf.empty() || fromShelf == toShelf)
        return;

    RevertCheckedOutFile(fromShelf, relativePath);
    CheckOut(toShelf, std::string(relativePath));
}

void WorkspaceState::RevertCheckedOutFile(std::string_view shelf, std::string_view relativePath)
{
    for (auto shelfIterator = m_shelfFiles.begin(); shelfIterator != m_shelfFiles.end(); ++shelfIterator)
    {
        if (shelfIterator->shelf != shelf)
            continue;

        shelfIterator->files.erase(std::remove(shelfIterator->files.begin(), shelfIterator->files.end(), relativePath), shelfIterator->files.end());
        if (shelfIterator->files.empty())
            m_shelfFiles.erase(shelfIterator);
        return;
    }
}

void WorkspaceState::DeleteShelf(std::string_view shelf)
{
    m_shelfFiles.erase(std::remove_if(m_shelfFiles.begin(), m_shelfFiles.end(), [shelf](const ShelfWorkspaceFiles& shelfFiles) {
        return shelfFiles.shelf == shelf;
    }), m_shelfFiles.end());

    if (m_activeShelf == shelf)
        m_activeShelf.clear();
}

void WorkspaceState::ClearCheckedOutFiles()
{
    if (m_activeShelf.empty())
        m_shelfFiles.clear();
    else
        DeleteShelf(m_activeShelf);
}

std::string WorkspaceState::Escape(std::string_view text)
{
    std::string escaped;
    for (char ch : text)
    {
        if (ch == '\\' || ch == '"')
            escaped.push_back('\\');
        escaped.push_back(ch);
    }
    return escaped;
}

std::string WorkspaceState::Unescape(std::string_view text)
{
    std::string unescaped;
    bool escaping = false;
    for (char ch : text)
    {
        if (escaping)
        {
            unescaped.push_back(ch);
            escaping = false;
            continue;
        }
        if (ch == '\\')
        {
            escaping = true;
            continue;
        }
        unescaped.push_back(ch);
    }
    return unescaped;
}

std::vector<std::string> WorkspaceState::ReadJsonStringArray(std::string_view json, std::string_view key)
{
    std::vector<std::string> values;
    const std::string marker = "\"" + std::string(key) + "\"";
    const size_t keyPos = json.find(marker);
    if (keyPos == std::string_view::npos)
        return values;

    const size_t arrayStart = json.find('[', keyPos);
    const size_t arrayEnd = json.find(']', arrayStart);
    if (arrayStart == std::string_view::npos || arrayEnd == std::string_view::npos)
        return values;

    size_t cursor = arrayStart;
    while (cursor < arrayEnd)
    {
        const size_t valueStart = json.find('"', cursor);
        if (valueStart == std::string_view::npos || valueStart >= arrayEnd)
            break;
        const size_t valueEnd = json.find('"', valueStart + 1);
        if (valueEnd == std::string_view::npos || valueEnd > arrayEnd)
            break;

        values.push_back(Unescape(json.substr(valueStart + 1, valueEnd - valueStart - 1)));
        cursor = valueEnd + 1;
    }

    return values;
}

std::string WorkspaceState::ReadJsonString(std::string_view json, std::string_view key)
{
    const std::string marker = "\"" + std::string(key) + "\"";
    const size_t keyPos = json.find(marker);
    if (keyPos == std::string_view::npos)
        return {};

    const size_t colon = json.find(':', keyPos);
    const size_t valueStart = json.find('"', colon);
    const size_t valueEnd = json.find('"', valueStart + 1);
    if (colon == std::string_view::npos || valueStart == std::string_view::npos || valueEnd == std::string_view::npos)
        return {};

    return Unescape(json.substr(valueStart + 1, valueEnd - valueStart - 1));
}

std::string WorkspaceState::ReadObjectString(std::string_view jsonObject, std::string_view key)
{
    return ReadJsonString(jsonObject, key);
}

std::vector<ShelfWorkspaceFiles> WorkspaceState::ReadShelfFiles(std::string_view json)
{
    std::vector<ShelfWorkspaceFiles> shelves;
    const std::string marker = "\"shelves\"";
    const size_t keyPos = json.find(marker);
    if (keyPos == std::string_view::npos)
        return shelves;

    const size_t arrayStart = json.find('[', keyPos);
    if (arrayStart == std::string_view::npos)
        return shelves;

    size_t cursor = arrayStart + 1;
    int arrayDepth = 1;
    while (cursor < json.size() && arrayDepth > 0)
    {
        if (json[cursor] == ']')
        {
            --arrayDepth;
            ++cursor;
            continue;
        }

        if (json[cursor] != '{')
        {
            ++cursor;
            continue;
        }

        const size_t objectStart = cursor;
        int objectDepth = 1;
        ++cursor;
        while (cursor < json.size() && objectDepth > 0)
        {
            if (json[cursor] == '{')
                ++objectDepth;
            else if (json[cursor] == '}')
                --objectDepth;
            ++cursor;
        }

        const std::string_view object = json.substr(objectStart, cursor - objectStart);
        ShelfWorkspaceFiles shelf;
        shelf.shelf = ReadObjectString(object, "branch");
        shelf.files = ReadJsonStringArray(object, "checkedOutFiles");
        if (!shelf.shelf.empty())
            shelves.push_back(std::move(shelf));
    }

    return shelves;
}

std::vector<std::string>& WorkspaceState::MutableCheckedOutFiles(std::string_view shelf)
{
    for (ShelfWorkspaceFiles& shelfFiles : m_shelfFiles)
    {
        if (shelfFiles.shelf == shelf)
            return shelfFiles.files;
    }

    m_shelfFiles.push_back({ std::string(shelf), {} });
    return m_shelfFiles.back().files;
}
}
