#pragma once

#include "log/StdoutLog.h"
#include "ui/AppUi.h"

#include <memory>

namespace p4vgit
{
class GuiLayer;
class Renderer;
class Window;

class Application
{
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int Run();

private:
    bool Initialize();
    void MainLoop();
    void Shutdown();

    std::unique_ptr<Window> m_window;
    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<GuiLayer> m_guiLayer;
    StdoutLog m_stdoutLog;
    AppUi m_appUi;
    bool m_wasWindowFocused = false;
};
}
