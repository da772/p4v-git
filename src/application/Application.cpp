#include "application/Application.h"

#include "gui/GuiFactory.h"
#include "gui/GuiLayer.h"
#include "platform/WindowFactory.h"
#include "renderer/Renderer.h"
#include "renderer/RendererFactory.h"

#include <cstdlib>

namespace p4vgit
{
Application::Application() = default;

Application::~Application()
{
    Shutdown();
}

int Application::Run()
{
    if (!Initialize())
        return EXIT_FAILURE;

    MainLoop();
    Shutdown();

    return EXIT_SUCCESS;
}

bool Application::Initialize()
{
    m_window = CreateWindow(WindowBackend::Glfw);
    if (m_window == nullptr || !m_window->Initialize({ "p4v-git", 1280, 800 }))
        return false;

    m_renderer = CreateRenderer(RendererApi::Vulkan);
    m_renderer->Initialize(*m_window);

    m_guiLayer = CreateGuiLayer(GuiBackend::ImGui);
    m_guiLayer->Initialize(*m_window, *m_renderer);

    return true;
}

void Application::MainLoop()
{
    while (!m_window->ShouldClose())
    {
        m_window->PollEvents();

        m_renderer->ResizeIfNeeded(*m_window);

        if (m_window->IsMinimized())
        {
            m_window->Sleep(10);
            continue;
        }

        m_guiLayer->BeginFrame();
        m_appUi.Draw();
        m_guiLayer->EndFrame();

        m_renderer->Render(m_guiLayer->GetDrawData());
    }
}

void Application::Shutdown()
{
    if (m_renderer != nullptr)
        m_renderer->WaitIdle();

    if (m_guiLayer != nullptr)
    {
        m_guiLayer->Shutdown();
        m_guiLayer.reset();
    }

    if (m_renderer != nullptr)
    {
        m_renderer->Shutdown();
        m_renderer.reset();
    }

    if (m_window != nullptr)
    {
        m_window->Shutdown();
        m_window.reset();
    }
}
}
