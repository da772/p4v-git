#include "platform/glfw/GlfwWindow.h"

#include "imgui_impl_glfw.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdio>

namespace p4vgit
{
GlfwWindow::~GlfwWindow()
{
    Shutdown();
}

bool GlfwWindow::Initialize(const WindowConfig& config)
{
    glfwSetErrorCallback(ErrorCallback);
    if (!glfwInit())
        return false;

    if (!glfwVulkanSupported())
    {
        std::fprintf(stderr, "GLFW: Vulkan is not supported\n");
        glfwTerminate();
        return false;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    m_contentScale = GetPrimaryMonitorScale();
    m_window = glfwCreateWindow(
        static_cast<int>(static_cast<float>(config.width) * m_contentScale),
        static_cast<int>(static_cast<float>(config.height) * m_contentScale),
        config.title.c_str(),
        nullptr,
        nullptr);

    if (m_window == nullptr)
    {
        glfwTerminate();
        return false;
    }

    m_initialized = true;
    return true;
}

void GlfwWindow::Shutdown()
{
    if (!m_initialized)
        return;

    if (m_window != nullptr)
    {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }

    glfwTerminate();
    m_initialized = false;
}

bool GlfwWindow::ShouldClose() const
{
    return glfwWindowShouldClose(m_window) != 0;
}

void GlfwWindow::PollEvents()
{
    glfwPollEvents();
}

bool GlfwWindow::IsMinimized() const
{
    return glfwGetWindowAttrib(m_window, GLFW_ICONIFIED) != 0;
}

void GlfwWindow::Sleep(int milliseconds) const
{
    ImGui_ImplGlfw_Sleep(milliseconds);
}

float GlfwWindow::ContentScale() const
{
    return m_contentScale;
}

FramebufferSize GlfwWindow::GetFramebufferSize() const
{
    FramebufferSize size;
    glfwGetFramebufferSize(m_window, &size.width, &size.height);
    return size;
}

void* GlfwWindow::NativeHandle() const
{
    return m_window;
}

const char* const* GlfwWindow::GetRequiredVulkanInstanceExtensions(uint32_t* count) const
{
    return glfwGetRequiredInstanceExtensions(count);
}

VkSurfaceKHR GlfwWindow::CreateVulkanSurface(VkInstance instance, const VkAllocationCallbacks* allocator) const
{
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (glfwCreateWindowSurface(instance, m_window, allocator, &surface) != VK_SUCCESS)
        return VK_NULL_HANDLE;

    return surface;
}

void GlfwWindow::ErrorCallback(int error, const char* description)
{
    std::fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

float GlfwWindow::GetPrimaryMonitorScale()
{
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (monitor == nullptr)
        return 1.0f;

    float x_scale = 1.0f;
    float y_scale = 1.0f;
    glfwGetMonitorContentScale(monitor, &x_scale, &y_scale);
    return x_scale > y_scale ? x_scale : y_scale;
}
}
