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
    m_checkedOutFiles.clear();

    std::ifstream file(m_statePath);
    if (!file)
        return true;

    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string json = buffer.str();

    m_activeShelf = ReadJsonString(json, "activeShelf");
    m_checkedOutFiles = ReadJsonStringArray(json, "checkedOutFiles");
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
    file << "  \"checkedOutFiles\": [";
    for (size_t i = 0; i < m_checkedOutFiles.size(); ++i)
    {
        if (i > 0)
            file << ", ";
        file << "\"" << Escape(m_checkedOutFiles[i]) << "\"";
    }
    file << "]\n";
    file << "}\n";

    return true;
}

void WorkspaceState::SetActiveShelf(std::string shelf)
{
    m_activeShelf = std::move(shelf);
}

bool WorkspaceState::IsCheckedOut(std::string_view relativePath) const
{
    return std::find(m_checkedOutFiles.begin(), m_checkedOutFiles.end(), relativePath) != m_checkedOutFiles.end();
}

void WorkspaceState::CheckOut(std::string relativePath)
{
    if (!IsCheckedOut(relativePath))
        m_checkedOutFiles.push_back(std::move(relativePath));
}

void WorkspaceState::ClearCheckedOutFiles()
{
    m_checkedOutFiles.clear();
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
}
