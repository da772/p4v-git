#include "platform/WindowFactory.h"

#include "platform/glfw/GlfwWindow.h"

namespace p4vgit
{
std::unique_ptr<Window> CreateWindow(WindowBackend backend)
{
    switch (backend)
    {
    case WindowBackend::Glfw:
        return std::make_unique<GlfwWindow>();
    }

    return nullptr;
}
}
