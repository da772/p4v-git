#include "ui/widgets/Widgets.h"

#include "imgui.h"

#include <string>

namespace p4vgit::ui::widgets
{
void DrawDockspace()
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
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

    ImGui::End();
}

bool BeginWindow(std::string_view title)
{
    const std::string label(title);
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

void ShowDemoWindow(bool* open)
{
    ImGui::ShowDemoWindow(open);
}
}
