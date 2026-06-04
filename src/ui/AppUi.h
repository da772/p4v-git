#pragma once

#include <array>
#include <filesystem>
#include <string>

namespace p4vgit
{
class StdoutLog;

class AppUi
{
public:
    AppUi();

    void SetStdoutLog(const StdoutLog* stdoutLog);
    void Draw();

private:
    void DrawWorkspaceExplorer();
    void DrawFileChanges();
    void DrawLog();
    void UseSourcePath(const std::filesystem::path& path);
    void DrawDirectory(const std::filesystem::path& path, int depth);
    static std::string PathLabel(const std::filesystem::path& path);

    const StdoutLog* m_stdoutLog = nullptr;
    std::array<char, 1024> m_sourcePathInput = {};
    std::filesystem::path m_sourcePath;
    bool m_hasSourcePath = false;
    std::string m_sourcePathError;
};
}
