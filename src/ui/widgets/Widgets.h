#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace p4vgit::ui::widgets
{
enum class DockspaceSide
{
    Left,
    Right,
    Up,
    Down,
    Center,
};

struct DockspaceDefaultLayout
{
    std::string_view window;
    DockspaceSide side = DockspaceSide::Left;
    float screenPercent = 25.0f;
};

extern void DrawDockspace();
extern void DrawDockspace(std::span<const DockspaceDefaultLayout> defaultLayout);
extern bool BeginWindow(std::string_view title);
extern void EndWindow();
extern void DrawWindowHeader(std::string_view title);
extern void Text(std::string_view text);
extern bool Button(std::string_view label);
extern bool InputText(std::string_view label, char* buffer, size_t bufferSize);
extern void SameLine();
extern void Separator();
extern bool BeginTreeNode(std::string_view label);
extern void EndTreeNode();
extern void TreeLeaf(std::string_view label);
extern void BeginScrollRegion(std::string_view id);
extern void EndScrollRegion();
extern void ShowDemoWindow(bool* open);
}
