#include "gui/imgui/ImGuiLayer.h"

#include "gui/GuiRendererBackend.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "platform/Window.h"

#include <GLFW/glfw3.h>

#include <cstdint>

namespace p4vgit
{
static ImVec4 Color(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha = 255)
{
    constexpr float scale = 1.0f / 255.0f;
    return ImVec4(
        static_cast<float>(red) * scale,
        static_cast<float>(green) * scale,
        static_cast<float>(blue) * scale,
        static_cast<float>(alpha) * scale);
}

static void ApplyP4vGitStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.CellPadding = ImVec2(8.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 5.0f);
    style.IndentSpacing = 16.0f;
    style.ScrollbarSize = 13.0f;
    style.GrabMinSize = 9.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.TabBorderSize = 0.0f;
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 2.0f;
    style.GrabRounding = 2.0f;
    style.TabRounding = 3.0f;
    style.WindowMenuButtonPosition = ImGuiDir_None;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = Color(223, 228, 230);
    colors[ImGuiCol_TextDisabled] = Color(117, 128, 135);
    colors[ImGuiCol_WindowBg] = Color(24, 27, 30);
    colors[ImGuiCol_ChildBg] = Color(20, 23, 26);
    colors[ImGuiCol_PopupBg] = Color(28, 32, 35);
    colors[ImGuiCol_Border] = Color(54, 62, 67);
    colors[ImGuiCol_BorderShadow] = Color(0, 0, 0, 0);
    colors[ImGuiCol_FrameBg] = Color(32, 37, 41);
    colors[ImGuiCol_FrameBgHovered] = Color(42, 51, 55);
    colors[ImGuiCol_FrameBgActive] = Color(50, 65, 68);
    colors[ImGuiCol_TitleBg] = Color(19, 22, 25);
    colors[ImGuiCol_TitleBgActive] = Color(24, 29, 32);
    colors[ImGuiCol_TitleBgCollapsed] = Color(19, 22, 25);
    colors[ImGuiCol_MenuBarBg] = Color(24, 27, 30);
    colors[ImGuiCol_ScrollbarBg] = Color(18, 20, 22);
    colors[ImGuiCol_ScrollbarGrab] = Color(63, 72, 76);
    colors[ImGuiCol_ScrollbarGrabHovered] = Color(83, 96, 101);
    colors[ImGuiCol_ScrollbarGrabActive] = Color(102, 119, 124);
    colors[ImGuiCol_CheckMark] = Color(95, 190, 176);
    colors[ImGuiCol_SliderGrab] = Color(95, 190, 176);
    colors[ImGuiCol_SliderGrabActive] = Color(128, 217, 201);
    colors[ImGuiCol_Button] = Color(39, 47, 51);
    colors[ImGuiCol_ButtonHovered] = Color(51, 64, 68);
    colors[ImGuiCol_ButtonActive] = Color(62, 84, 86);
    colors[ImGuiCol_Header] = Color(35, 43, 47);
    colors[ImGuiCol_HeaderHovered] = Color(45, 61, 64);
    colors[ImGuiCol_HeaderActive] = Color(57, 82, 83);
    colors[ImGuiCol_Separator] = Color(56, 65, 70);
    colors[ImGuiCol_SeparatorHovered] = Color(80, 150, 140);
    colors[ImGuiCol_SeparatorActive] = Color(95, 190, 176);
    colors[ImGuiCol_ResizeGrip] = Color(95, 190, 176, 45);
    colors[ImGuiCol_ResizeGripHovered] = Color(95, 190, 176, 90);
    colors[ImGuiCol_ResizeGripActive] = Color(95, 190, 176, 150);
    colors[ImGuiCol_Tab] = Color(28, 33, 36);
    colors[ImGuiCol_TabHovered] = Color(47, 63, 66);
    colors[ImGuiCol_TabActive] = Color(36, 45, 48);
    colors[ImGuiCol_TabUnfocused] = Color(24, 28, 31);
    colors[ImGuiCol_TabUnfocusedActive] = Color(31, 37, 40);
    colors[ImGuiCol_DockingPreview] = Color(95, 190, 176, 115);
    colors[ImGuiCol_DockingEmptyBg] = Color(17, 20, 22);
    colors[ImGuiCol_PlotLines] = Color(118, 132, 138);
    colors[ImGuiCol_PlotLinesHovered] = Color(237, 171, 85);
    colors[ImGuiCol_PlotHistogram] = Color(95, 190, 176);
    colors[ImGuiCol_PlotHistogramHovered] = Color(237, 171, 85);
    colors[ImGuiCol_TableHeaderBg] = Color(32, 38, 42);
    colors[ImGuiCol_TableBorderStrong] = Color(60, 69, 74);
    colors[ImGuiCol_TableBorderLight] = Color(42, 49, 53);
    colors[ImGuiCol_TableRowBg] = Color(0, 0, 0, 0);
    colors[ImGuiCol_TableRowBgAlt] = Color(255, 255, 255, 7);
    colors[ImGuiCol_TextSelectedBg] = Color(95, 190, 176, 72);
    colors[ImGuiCol_DragDropTarget] = Color(237, 171, 85, 210);
    colors[ImGuiCol_NavHighlight] = Color(95, 190, 176);
    colors[ImGuiCol_TextLink] = Color(126, 201, 240);
}

ImGuiLayer::~ImGuiLayer()
{
    Shutdown();
}

void ImGuiLayer::Initialize(Window& window, GuiRendererBackend& m_rendererbackend)
{
    m_rendererBackend = &m_rendererbackend;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ApplyP4vGitStyle();

    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(window.ContentScale());
    style.FontScaleDpi = window.ContentScale();
    io.ConfigDpiScaleFonts = true;

    ImGui_ImplGlfw_InitForVulkan(static_cast<GLFWwindow*>(window.NativeHandle()), true);
    m_rendererBackend->InitializeGuiRendererBackend();

    m_initialized = true;
}

void ImGuiLayer::Shutdown()
{
    if (!m_initialized)
        return;

    m_rendererBackend->ShutdownGuiRendererBackend();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    m_rendererBackend = nullptr;
    m_initialized = false;
}

void ImGuiLayer::BeginFrame()
{
    m_rendererBackend->BeginGuiRendererFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::EndFrame()
{
    ImGui::Render();
}

GuiDrawData ImGuiLayer::GetDrawData() const
{
    return GuiDrawData(ImGui::GetDrawData());
}
}
