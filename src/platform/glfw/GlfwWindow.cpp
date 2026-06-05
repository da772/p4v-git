#include "platform/glfw/GlfwWindow.h"

#include "imgui_impl_glfw.h"
#include "platform/IconData.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>

#include "platform/windows/P4vGitWindowsResource.h"

#ifdef IsMaximized
#undef IsMaximized
#endif

#ifdef IsMinimized
#undef IsMinimized
#endif
#endif

#include <cstdio>
#include <unordered_map>
#include <utility>

namespace p4vgit
{
#ifdef _WIN32
struct WindowsSubclassState
{
    GlfwWindow* window = nullptr;
    WNDPROC previousProc = nullptr;
};

static std::unordered_map<HWND, WindowsSubclassState> g_windowsSubclassStates;

static LRESULT CallPreviousWindowProc(HWND handle, UINT message, WPARAM wParam, LPARAM lParam)
{
    const auto state = g_windowsSubclassStates.find(handle);
    if (state != g_windowsSubclassStates.end() && state->second.previousProc != nullptr)
        return CallWindowProcW(state->second.previousProc, handle, message, wParam, lParam);

    return DefWindowProcW(handle, message, wParam, lParam);
}

static LRESULT CALLBACK P4vGitWindowProc(HWND handle, UINT message, WPARAM wParam, LPARAM lParam)
{
    const auto state = g_windowsSubclassStates.find(handle);
    GlfwWindow* window = state != g_windowsSubclassStates.end() ? state->second.window : nullptr;

    if (message == WM_NCHITTEST && window != nullptr)
    {
        const LRESULT hit = CallPreviousWindowProc(handle, message, wParam, lParam);
        if (hit != HTCLIENT)
            return hit;

        POINT cursor{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(handle, &cursor);
        if (window->IsPointInTitleBarHitTest(static_cast<double>(cursor.x), static_cast<double>(cursor.y)))
            return HTCAPTION;

        return HTCLIENT;
    }

    if (message == WM_NCLBUTTONDBLCLK && window != nullptr && wParam == HTCAPTION)
    {
        window->ToggleMaximize();
        return 0;
    }

    if (message == WM_NCDESTROY)
    {
        const LRESULT result = CallPreviousWindowProc(handle, message, wParam, lParam);
        g_windowsSubclassStates.erase(handle);
        return result;
    }

    return CallPreviousWindowProc(handle, message, wParam, lParam);
}
#endif

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
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
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
    InstallNativeMessageHook();
#endif

    glfwShowWindow(m_window);

    m_initialized = true;
    return true;
}

void GlfwWindow::Shutdown()
{
    if (!m_initialized)
        return;

#ifdef _WIN32
    RemoveNativeMessageHook();
#endif

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
    UpdateMoveDrag();
}

void GlfwWindow::RequestClose()
{
    glfwSetWindowShouldClose(m_window, GLFW_TRUE);
}

void GlfwWindow::Minimize()
{
    glfwIconifyWindow(m_window);
}

void GlfwWindow::ToggleMaximize()
{
    if (IsMaximized())
        glfwRestoreWindow(m_window);
    else
        glfwMaximizeWindow(m_window);
}

bool GlfwWindow::IsMaximized() const
{
    return glfwGetWindowAttrib(m_window, GLFW_MAXIMIZED) != 0;
}

void GlfwWindow::StartMoveDrag()
{
    if (m_window == nullptr)
        return;

#ifdef _WIN32
    HWND handle = glfwGetWin32Window(m_window);
    if (handle == nullptr)
        return;

    ReleaseCapture();
    SendMessageW(handle, WM_NCLBUTTONDOWN, HTCAPTION, 0);
#else
    if (IsMaximized())
        return;

    double cursorX = 0.0;
    double cursorY = 0.0;
    glfwGetCursorPos(m_window, &cursorX, &cursorY);
    m_dragOffsetX = cursorX;
    m_dragOffsetY = cursorY;
    m_dragging = true;
#endif
}

void GlfwWindow::SetTitleBarHitTestRegion(const TitleBarHitTestRegion& region)
{
    m_titleBarHitTestRegion = region;
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

void GlfwWindow::InstallNativeMessageHook()
{
#ifdef _WIN32
    if (m_window == nullptr)
        return;

    HWND handle = glfwGetWin32Window(m_window);
    if (handle == nullptr || g_windowsSubclassStates.find(handle) != g_windowsSubclassStates.end())
        return;

    WindowsSubclassState state;
    state.window = this;
    state.previousProc = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(handle, GWLP_WNDPROC));
    g_windowsSubclassStates.emplace(handle, state);
    SetWindowLongPtrW(handle, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(P4vGitWindowProc));
#endif
}

void GlfwWindow::RemoveNativeMessageHook()
{
#ifdef _WIN32
    if (m_window == nullptr)
        return;

    HWND handle = glfwGetWin32Window(m_window);
    const auto state = g_windowsSubclassStates.find(handle);
    if (state == g_windowsSubclassStates.end())
        return;

    const LONG_PTR currentProc = GetWindowLongPtrW(handle, GWLP_WNDPROC);
    if (currentProc == reinterpret_cast<LONG_PTR>(P4vGitWindowProc) && state->second.previousProc != nullptr)
        SetWindowLongPtrW(handle, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(state->second.previousProc));

    g_windowsSubclassStates.erase(state);
#endif
}

bool GlfwWindow::IsPointInTitleBarHitTest(double clientX, double clientY) const
{
    if (!m_titleBarHitTestRegion.enabled)
        return false;

    if (m_titleBarHitTestRegion.height <= 0.0f || m_titleBarHitTestRegion.dragRegionRight <= 0.0f)
        return false;

    return clientX >= 0.0 &&
           clientY >= 0.0 &&
           clientX < static_cast<double>(m_titleBarHitTestRegion.dragRegionRight) &&
           clientY < static_cast<double>(m_titleBarHitTestRegion.height);
}

void GlfwWindow::UpdateMoveDrag()
{
    if (!m_dragging || m_window == nullptr)
        return;

    if (glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_LEFT) != GLFW_PRESS)
    {
        m_dragging = false;
        return;
    }

    int windowX = 0;
    int windowY = 0;
    double cursorX = 0.0;
    double cursorY = 0.0;
    glfwGetWindowPos(m_window, &windowX, &windowY);
    glfwGetCursorPos(m_window, &cursorX, &cursorY);

    const int nextX = windowX + static_cast<int>(cursorX - m_dragOffsetX);
    const int nextY = windowY + static_cast<int>(cursorY - m_dragOffsetY);
    if (std::pair(nextX, nextY) != std::pair(windowX, windowY))
        glfwSetWindowPos(m_window, nextX, nextY);
}
}
