#include "log/StdoutLog.h"

#include <iostream>

namespace p4vgit
{
StdoutLog::~StdoutLog()
{
    StopCapture();
}

void StdoutLog::StartCapture()
{
    if (m_originalBuffer != nullptr)
        return;

    m_originalBuffer = std::cout.rdbuf(this);
}

void StdoutLog::StopCapture()
{
    if (m_originalBuffer == nullptr)
        return;

    std::cout.rdbuf(m_originalBuffer);
    m_originalBuffer = nullptr;
}

std::vector<std::string> StdoutLog::Lines() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<std::string> lines = m_lines;
    if (!m_currentLine.empty())
        lines.push_back(m_currentLine);

    return lines;
}

int StdoutLog::overflow(int ch)
{
    if (ch == traits_type::eof())
        return traits_type::not_eof(ch);

    Append(static_cast<char>(ch));
    return ch;
}

std::streamsize StdoutLog::xsputn(const char* text, std::streamsize count)
{
    for (std::streamsize i = 0; i < count; ++i)
        Append(text[i]);

    return count;
}

void StdoutLog::Append(char ch)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_originalBuffer != nullptr)
        m_originalBuffer->sputc(ch);

    if (ch == '\n')
    {
        FlushCurrentLine();
        return;
    }

    if (ch != '\r')
        m_currentLine.push_back(ch);
}

void StdoutLog::FlushCurrentLine()
{
    m_lines.push_back(m_currentLine);
    m_currentLine.clear();

    constexpr size_t maxLines = 1000;
    if (m_lines.size() > maxLines)
        m_lines.erase(m_lines.begin(), m_lines.begin() + (m_lines.size() - maxLines));
}
}
