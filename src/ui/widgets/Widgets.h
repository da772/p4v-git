#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace p4vgit::ui::widgets
{
struct TitleBarResult
{
    bool minimize = false;
    bool maximize = false;
    bool close = false;
    bool drag = false;
    float dragRegionRight = 0.0f;
};

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
extern void DrawDockspace(std::span<const DockspaceDefaultLayout> defaultLayout, float topInset);
extern float TitleBarHeight();
extern TitleBarResult DrawTitleBar(std::string_view title, std::string_view subtitle, bool maximized);
extern float FrameRate();
extern bool BeginWindow(std::string_view title);
extern void EndWindow();
extern void DrawWindowHeader(std::string_view title);
extern void Text(std::string_view text);
extern bool Link(std::string_view label);
extern bool Button(std::string_view label);
extern bool InputText(std::string_view label, char* buffer, size_t bufferSize);
extern bool InputTextMultiline(std::string_view label, char* buffer, size_t bufferSize);
extern void BeginDisabled(bool disabled = true);
extern void EndDisabled();
extern void Spinner(std::string_view label);
extern bool BeginCombo(std::string_view label, std::string_view preview);
extern bool Selectable(std::string_view label, bool selected);
extern void EndCombo();
extern void SameLine();
extern void Separator();
extern bool BeginTreeNode(std::string_view label, bool autoOpen = false);
extern void EndTreeNode();
extern void TreeLeaf(std::string_view label);
extern bool BeginContextMenuForLastItem();
extern bool BeginContextMenuForCurrentWindow(std::string_view id);
extern bool DidClickCurrentWindowBlank();
extern bool MenuItem(std::string_view label, bool enabled);
extern void EndContextMenu();
extern bool IsCtrlDown();
extern bool IsShiftDown();
extern void OpenPopup(std::string_view id);
extern bool BeginModal(std::string_view id);
extern void EndModal();
extern void CloseCurrentPopup();
extern bool DragDropSource(std::string_view type, std::string_view payload, std::string_view label);
extern std::optional<std::string> AcceptDragDropPayload(std::string_view type);
extern void BeginScrollRegion(std::string_view id);
extern void BeginScrollRegion(std::string_view id, bool scrollToBottom);
extern bool IsCurrentScrollRegionAtBottom();
extern bool DidUserScrollCurrentRegion();
extern void ScrollCurrentRegionToBottom();
extern void EndScrollRegion();
extern void ShowDemoWindow(bool* open);
}
