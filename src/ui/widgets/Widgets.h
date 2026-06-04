#pragma once

#include <string_view>

namespace p4vgit::ui::widgets
{
extern void DrawDockspace();
extern bool BeginWindow(std::string_view title);
extern void EndWindow();
extern void DrawWindowHeader(std::string_view title);
extern void Text(std::string_view text);
extern void ShowDemoWindow(bool* open);
}
