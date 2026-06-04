#pragma once

#include <mutex>
#include <streambuf>
#include <string>
#include <vector>

namespace p4vgit
{
class StdoutLog final : public std::streambuf
{
public:
    StdoutLog() = default;
    ~StdoutLog() override;

    StdoutLog(const StdoutLog&) = delete;
    StdoutLog& operator=(const StdoutLog&) = delete;

    void StartCapture();
    void StopCapture();
    std::vector<std::string> Lines() const;

protected:
    int overflow(int ch) override;
    std::streamsize xsputn(const char* text, std::streamsize count) override;

private:
    void Append(char ch);
    void FlushCurrentLine();

    mutable std::mutex m_mutex;
    std::streambuf* m_originalBuffer = nullptr;
    std::string m_currentLine;
    std::vector<std::string> m_lines;
};
}
