#include "gui/imgui/ImGuiLayer.h"

#include "gui/GuiRendererBackend.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "platform/Window.h"

#include <GLFW/glfw3.h>

namespace p4vgit
{
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

    ImGui::StyleColorsDark();

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
