#include "platform/glfw/GlfwWindow.h"

#include "imgui_impl_glfw.h"
#include "platform/IconData.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <windows.h>
#include <shellapi.h>

#include "platform/windows/P4vGitWindowsResource.h"
#endif

#include <cstdio>

namespace p4vgit
{
#ifndef __APPLE__
void ApplyRuntimeWindowIcon(GLFWwindow* window)
{
    const AppIconPixels iconPixels = RuntimeWindowIcon();
    if (iconPixels.rgba.empty())
        return;

    GLFWimage icon;
    icon.width = iconPixels.width;
    icon.height = iconPixels.height;
    icon.pixels = const_cast<unsigned char*>(iconPixels.rgba.data());
    glfwSetWindowIcon(window, 1, &icon);
}
#endif

#ifdef _WIN32
void ApplyWindowsWindowIcon(GLFWwindow* window)
{
    HWND handle = glfwGetWin32Window(window);
    if (handle == nullptr)
        return;

    HINSTANCE instance = GetModuleHandleW(nullptr);
    HICON bigIcon = nullptr;
    HICON smallIcon = nullptr;

    wchar_t modulePath[MAX_PATH] = {};
    if (GetModuleFileNameW(instance, modulePath, static_cast<DWORD>(sizeof(modulePath) / sizeof(modulePath[0]))) > 0)
        ExtractIconExW(modulePath, 0, &bigIcon, &smallIcon, 1);

    if (bigIcon == nullptr)
    {
        bigIcon = static_cast<HICON>(LoadImageW(
            instance,
            MAKEINTRESOURCEW(IDI_P4VGIT),
            IMAGE_ICON,
            GetSystemMetrics(SM_CXICON),
            GetSystemMetrics(SM_CYICON),
            LR_DEFAULTCOLOR));
    }

    if (smallIcon == nullptr)
    {
        smallIcon = static_cast<HICON>(LoadImageW(
            instance,
            MAKEINTRESOURCEW(IDI_P4VGIT),
            IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON),
            GetSystemMetrics(SM_CYSMICON),
            LR_DEFAULTCOLOR));
    }

    if (bigIcon != nullptr)
    {
        SendMessageW(handle, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(bigIcon));
        SetClassLongPtrW(handle, GCLP_HICON, reinterpret_cast<LONG_PTR>(bigIcon));
    }
    if (smallIcon != nullptr)
    {
        SendMessageW(handle, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
        SendMessageW(handle, WM_SETICON, 2, reinterpret_cast<LPARAM>(smallIcon));
        SetClassLongPtrW(handle, GCLP_HICONSM, reinterpret_cast<LONG_PTR>(smallIcon));
    }
}
#endif

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
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

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

#ifndef __APPLE__
    ApplyRuntimeWindowIcon(m_window);
#endif

#ifdef _WIN32
    ApplyWindowsWindowIcon(m_window);
#endif

    glfwShowWindow(m_window);

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

bool GlfwWindow::IsFocused() const
{
    return glfwGetWindowAttrib(m_window, GLFW_FOCUSED) != 0;
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
