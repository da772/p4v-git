#include "ui/widgets/Widgets.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cstdint>
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

static ImU32 Color(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha = 255)
{
    return IM_COL32(red, green, blue, alpha);
}

static float UiScale()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport == nullptr)
        return 1.0f;

    return std::max(1.0f, viewport->DpiScale);
}

static bool DrawTitleBarButton(std::string_view id, ImU32 color, ImU32 hoverColor, float radius)
{
    const std::string label = ToString(id);
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const ImVec2 center = ImVec2(cursor.x + radius, cursor.y + radius);
    ImGui::InvisibleButton(label.c_str(), ImVec2(radius * 2.0f, radius * 2.0f));
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    ImGui::GetWindowDrawList()->AddCircleFilled(center, radius, hovered ? hoverColor : color, 22);
    return clicked;
}

static float DistanceSquared(ImVec2 lhs, ImVec2 rhs)
{
    const float x = lhs.x - rhs.x;
    const float y = lhs.y - rhs.y;
    return x * x + y * y;
}

static void BuildDefaultDockspaceLayout(ImGuiID dockspaceId, ImVec2 dockspaceSize, std::span<const DockspaceDefaultLayout> defaultLayout)
{
    if (ImGui::DockBuilderGetNode(dockspaceId) != nullptr)
        return;

    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, dockspaceSize);

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
    DrawDockspace(defaultLayout, 0.0f);
}

void DrawDockspace(std::span<const DockspaceDefaultLayout> defaultLayout, float topInset)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float clampedTopInset = std::clamp(topInset, 0.0f, viewport->WorkSize.y);
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + clampedTopInset));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - clampedTopInset));
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

    const ImGuiID dockspace_id = ImGui::GetID("MainDockspaceV2");
    if (!defaultLayout.empty())
        BuildDefaultDockspaceLayout(dockspace_id, ImGui::GetContentRegionAvail(), defaultLayout);

    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

    ImGui::End();
}

float TitleBarHeight()
{
    return 34.0f * UiScale();
}

TitleBarResult DrawTitleBar(std::string_view title, std::string_view subtitle, bool maximized)
{
    TitleBarResult result;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float height = TitleBarHeight();
    const float scale = UiScale();
    const float edgePadding = 10.0f * scale;
    const float buttonRadius = 5.5f * scale;
    const float buttonGap = 7.0f * scale;

    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, height));
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.075f, 0.086f, 0.095f, 1.0f));
    ImGui::Begin("AppTitleBar", nullptr, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetWindowPos();
    const ImVec2 max = ImVec2(min.x + ImGui::GetWindowWidth(), min.y + height);
    drawList->AddRectFilled(min, max, Color(19, 22, 25));
    drawList->AddLine(ImVec2(min.x, max.y - 1.0f), ImVec2(max.x, max.y - 1.0f), Color(54, 62, 67));

    const float controlWidth = buttonRadius * 6.0f + buttonGap * 2.0f;
    const float controlX = max.x - edgePadding - controlWidth;
    const float controlY = min.y + (height - buttonRadius * 2.0f) * 0.5f;
    ImGui::SetCursorScreenPos(ImVec2(controlX, controlY));
    result.minimize = DrawTitleBarButton("##minimize", Color(238, 190, 86), Color(255, 210, 110), buttonRadius);
    ImGui::SameLine(0.0f, buttonGap);
    result.maximize = DrawTitleBarButton("##maximize", maximized ? Color(96, 168, 130) : Color(99, 199, 122), Color(126, 222, 150), buttonRadius);
    ImGui::SameLine(0.0f, buttonGap);
    result.close = DrawTitleBarButton("##close", Color(238, 95, 88), Color(255, 120, 112), buttonRadius);

    const std::string titleText = ToString(title);
    const std::string subtitleText = ToString(subtitle);
    const std::string text = subtitleText.empty() ? titleText : titleText + "  |  " + subtitleText;
    const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
    ImGui::SetCursorScreenPos(ImVec2(min.x + (ImGui::GetWindowWidth() - textSize.x) * 0.5f, min.y + (height - textSize.y) * 0.5f + 1.0f * scale));
    ImGui::TextUnformatted(text.c_str());

    static double lastTitleBarClickTime = -1.0;
    static ImVec2 lastTitleBarClickPosition = ImVec2(0.0f, 0.0f);
    static bool titleBarDragCandidate = false;
    static bool titleBarDragStarted = false;

    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 dragMin = min;
    const ImVec2 dragMax = ImVec2(std::max(min.x, controlX - edgePadding), max.y);
    result.dragRegionRight = dragMax.x - min.x;
    const bool titleBarHovered = ImGui::IsMouseHoveringRect(dragMin, dragMax, false);

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        titleBarDragCandidate = false;
        titleBarDragStarted = false;
    }

    if (titleBarHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        const double now = ImGui::GetTime();
        const ImVec2 clickPosition = io.MousePos;
        const float maxDistance = std::max(8.0f * scale, io.MouseDoubleClickMaxDist);
        if (lastTitleBarClickTime >= 0.0 &&
            now - lastTitleBarClickTime <= static_cast<double>(io.MouseDoubleClickTime) &&
            DistanceSquared(clickPosition, lastTitleBarClickPosition) <= maxDistance * maxDistance)
        {
            result.maximize = true;
            lastTitleBarClickTime = -1.0;
            titleBarDragCandidate = false;
        }
        else
        {
            lastTitleBarClickTime = now;
            lastTitleBarClickPosition = clickPosition;
            titleBarDragCandidate = true;
        }
    }

    if (!result.maximize && titleBarDragCandidate && !titleBarDragStarted && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 8.0f * scale))
    {
        result.drag = true;
        titleBarDragStarted = true;
    }

    ImGui::End();
    return result;
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

float AvailableWidth()
{
    return ImGui::GetContentRegionAvail().x;
}

void SetNextItemWidth(float width)
{
    ImGui::SetNextItemWidth(width);
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
    return ImGui::BeginCombo(labelText.c_str(), previewText.c_str(), ImGuiComboFlags_WidthFitPreview);
}

bool Selectable(std::string_view label, bool selected, bool enabled)
{
    const std::string labelText = ToString(label);
    ImGui::BeginDisabled(!enabled);
    const bool clicked = enabled && ImGui::Selectable(labelText.c_str(), selected);
    ImGui::EndDisabled();
    return clicked;
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

bool DidClickCurrentWindowBlank()
{
    return ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
           ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
           !ImGui::IsAnyItemHovered() &&
           !ImGui::IsAnyItemActive();
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
