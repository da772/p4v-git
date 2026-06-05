#pragma once

#include "platform/Window.h"

struct GLFWwindow;

namespace p4vgit
{
class GlfwWindow final : public Window
{
public:
    GlfwWindow() = default;
    ~GlfwWindow() override;

    GlfwWindow(const GlfwWindow&) = delete;
    GlfwWindow& operator=(const GlfwWindow&) = delete;

    bool Initialize(const WindowConfig& config) override;
    void Shutdown() override;
    bool ShouldClose() const override;
    void PollEvents() override;
    void RequestClose() override;
    void Minimize() override;
    void ToggleMaximize() override;
    bool IsMaximized() const override;
    void StartMoveDrag() override;
    void SetTitleBarHitTestRegion(const TitleBarHitTestRegion& region) override;
    bool IsMinimized() const override;
    bool IsFocused() const override;
    void Sleep(int milliseconds) const override;
    float ContentScale() const override;
    FramebufferSize GetFramebufferSize() const override;
    void* NativeHandle() const override;

    const char* const* GetRequiredVulkanInstanceExtensions(uint32_t* count) const override;
    VkSurfaceKHR CreateVulkanSurface(VkInstance instance, const VkAllocationCallbacks* allocator) const override;
    bool IsPointInTitleBarHitTest(double clientX, double clientY) const;

private:
    static void ErrorCallback(int error, const char* description);
    static float GetPrimaryMonitorScale();
    void InstallNativeMessageHook();
    void RemoveNativeMessageHook();
    void UpdateMoveDrag();

    GLFWwindow* m_window = nullptr;
    TitleBarHitTestRegion m_titleBarHitTestRegion;
    float m_contentScale = 1.0f;
    double m_dragOffsetX = 0.0;
    double m_dragOffsetY = 0.0;
    bool m_dragging = false;
    bool m_initialized = false;
};
}
