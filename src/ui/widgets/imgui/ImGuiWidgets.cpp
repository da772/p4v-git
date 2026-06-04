#include "ui/widgets/Widgets.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <optional>
#include <string>

namespace p4vgit::ui::widgets
{
static std::string ToString(std::string_view text)
{
    return std::string(text);
}

static ImGuiDir ToImGuiDir(DockspaceSide side)
{
    switch (side)
    {
    case DockspaceSide::Left:
        return ImGuiDir_Left;
    case DockspaceSide::Right:
        return ImGuiDir_Right;
    case DockspaceSide::Up:
        return ImGuiDir_Up;
    case DockspaceSide::Down:
        return ImGuiDir_Down;
    case DockspaceSide::Center:
        break;
    }

    return ImGuiDir_Left;
}

static float ToDockRatio(float screenPercent)
{
    return std::clamp(screenPercent / 100.0f, 0.05f, 0.95f);
}

static void BuildDefaultDockspaceLayout(ImGuiID dockspaceId, const ImGuiViewport* viewport, std::span<const DockspaceDefaultLayout> defaultLayout)
{
    if (ImGui::DockBuilderGetNode(dockspaceId) != nullptr)
        return;

    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

    ImGuiID remainingId = dockspaceId;

    for (const DockspaceDefaultLayout& placement : defaultLayout)
    {
        if (placement.window.empty())
            continue;

        const std::string windowName = ToString(placement.window);
        if (placement.side == DockspaceSide::Center)
        {
            ImGui::DockBuilderDockWindow(windowName.c_str(), remainingId);
            continue;
        }

        ImGuiID dockId = 0;
        ImGui::DockBuilderSplitNode(remainingId, ToImGuiDir(placement.side), ToDockRatio(placement.screenPercent), &dockId, &remainingId);
        ImGui::DockBuilderDockWindow(windowName.c_str(), dockId);
    }

    ImGui::DockBuilderFinish(dockspaceId);
}

void DrawDockspace()
{
    DrawDockspace({});
}

void DrawDockspace(std::span<const DockspaceDefaultLayout> defaultLayout)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("MainDockspaceHost", nullptr, window_flags);
    ImGui::PopStyleVar(3);

    const ImGuiID dockspace_id = ImGui::GetID("MainDockspace");
    if (!defaultLayout.empty())
        BuildDefaultDockspaceLayout(dockspace_id, viewport, defaultLayout);

    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

    ImGui::End();
}

float FrameRate()
{
    return ImGui::GetIO().Framerate;
}

bool BeginWindow(std::string_view title)
{
    const std::string label = ToString(title);
    return ImGui::Begin(label.c_str());
}

void EndWindow()
{
    ImGui::End();
}

void DrawWindowHeader(std::string_view title)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = ImGui::GetFrameHeight();
    const ImVec2 max = ImVec2(cursor.x + width, cursor.y + height);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(cursor, max, ImGui::GetColorU32(ImGuiCol_Header), style.FrameRounding);

    ImGui::SetCursorScreenPos(ImVec2(cursor.x + style.FramePadding.x, cursor.y + style.FramePadding.y));
    ImGui::TextUnformatted(title.data(), title.data() + title.size());

    ImGui::SetCursorScreenPos(ImVec2(cursor.x, cursor.y + height + style.ItemSpacing.y));
    ImGui::Separator();
}

void Text(std::string_view text)
{
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
}

bool Link(std::string_view label)
{
    const std::string labelText = ToString(label);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextLink));
    ImGui::TextUnformatted(labelText.c_str());
    ImGui::PopStyleColor();

    const bool hovered = ImGui::IsItemHovered();
    if (hovered)
    {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x, max.y), max, ImGui::GetColorU32(ImGuiCol_TextLink));
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    return hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
}

bool Button(std::string_view label)
{
    const std::string labelText = ToString(label);
    return ImGui::Button(labelText.c_str());
}

bool InputText(std::string_view label, char* buffer, size_t bufferSize)
{
    const std::string labelText = ToString(label);
    return ImGui::InputText(labelText.c_str(), buffer, bufferSize);
}

bool InputTextMultiline(std::string_view label, char* buffer, size_t bufferSize)
{
    const std::string labelText = ToString(label);
    return ImGui::InputTextMultiline(labelText.c_str(), buffer, bufferSize, ImVec2(420.0f, 120.0f));
}

void BeginDisabled(bool disabled)
{
    ImGui::BeginDisabled(disabled);
}

void EndDisabled()
{
    ImGui::EndDisabled();
}

void Spinner(std::string_view label)
{
    constexpr char frames[] = { '|', '/', '-', '\\' };
    const int frame = static_cast<int>(ImGui::GetTime() * 10.0) % 4;
    const std::string labelText = ToString(label);
    ImGui::Text("%c %s", frames[frame], labelText.c_str());
}

bool BeginCombo(std::string_view label, std::string_view preview)
{
    const std::string labelText = ToString(label);
    const std::string previewText = ToString(preview);
    return ImGui::BeginCombo(labelText.c_str(), previewText.c_str());
}

bool Selectable(std::string_view label, bool selected)
{
    const std::string labelText = ToString(label);
    return ImGui::Selectable(labelText.c_str(), selected);
}

void EndCombo()
{
    ImGui::EndCombo();
}

void SameLine()
{
    ImGui::SameLine();
}

void Separator()
{
    ImGui::Separator();
}

bool BeginTreeNode(std::string_view label, bool autoOpen)
{
    const std::string labelText = ToString(label);
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (autoOpen) ImGui::SetNextItemOpen(autoOpen);
    return ImGui::TreeNodeEx(labelText.c_str(), flags);
}

void EndTreeNode()
{
    ImGui::TreePop();
}

void TreeLeaf(std::string_view label)
{
    const std::string labelText = ToString(label);
    ImGui::TreeNodeEx(labelText.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
}

bool BeginContextMenuForLastItem()
{
    return ImGui::BeginPopupContextItem();
}

bool BeginContextMenuForCurrentWindow(std::string_view id)
{
    const std::string idText = ToString(id);
    return ImGui::BeginPopupContextWindow(idText.c_str(), ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems);
}

bool MenuItem(std::string_view label, bool enabled)
{
    const std::string labelText = ToString(label);
    return ImGui::MenuItem(labelText.c_str(), nullptr, false, enabled);
}

void EndContextMenu()
{
    ImGui::EndPopup();
}

void OpenPopup(std::string_view id)
{
    const std::string idText = ToString(id);
    ImGui::OpenPopup(idText.c_str());
}

bool BeginModal(std::string_view id)
{
    const std::string idText = ToString(id);
    return ImGui::BeginPopupModal(idText.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize);
}

void EndModal()
{
    ImGui::EndPopup();
}

void CloseCurrentPopup()
{
    ImGui::CloseCurrentPopup();
}

bool IsCtrlDown()
{
    return ImGui::GetIO().KeyCtrl;
}

bool IsShiftDown()
{
    return ImGui::GetIO().KeyShift;
}

bool DragDropSource(std::string_view type, std::string_view payload, std::string_view label)
{
    if (!ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        return false;

    const std::string typeText = ToString(type);
    const std::string payloadText = ToString(payload);
    ImGui::SetDragDropPayload(typeText.c_str(), payloadText.c_str(), payloadText.size() + 1);
    ImGui::TextUnformatted(label.data(), label.data() + label.size());
    ImGui::EndDragDropSource();
    return true;
}

std::optional<std::string> AcceptDragDropPayload(std::string_view type)
{
    if (!ImGui::BeginDragDropTarget())
        return std::nullopt;

    std::optional<std::string> payloadText;
    const std::string typeText = ToString(type);
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(typeText.c_str()))
    {
        payloadText = std::string(static_cast<const char*>(payload->Data), static_cast<size_t>(payload->DataSize > 0 ? payload->DataSize - 1 : 0));
    }

    ImGui::EndDragDropTarget();
    return payloadText;
}

void BeginScrollRegion(std::string_view id)
{
    BeginScrollRegion(id, false);
}

void BeginScrollRegion(std::string_view id, bool scrollToBottom)
{
    const std::string labelText = ToString(id);
    ImGui::BeginChild(labelText.c_str(), ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    if (scrollToBottom)
        ScrollCurrentRegionToBottom();
}

bool IsCurrentScrollRegionAtBottom()
{
    constexpr float bottomTolerance = 2.0f;
    return ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - bottomTolerance;
}

bool DidUserScrollCurrentRegion()
{
    const ImGuiIO& io = ImGui::GetIO();
    if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
        return false;

    return io.MouseWheel != 0.0f || ImGui::IsMouseDragging(ImGuiMouseButton_Left);
}

void ScrollCurrentRegionToBottom()
{
    ImGui::SetScrollY(ImGui::GetScrollMaxY());
}

void EndScrollRegion()
{
    ImGui::EndChild();
}

void ShowDemoWindow(bool* open)
{
    ImGui::ShowDemoWindow(open);
}
}
