#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <vulkan/vulkan.h>

namespace p4vgit
{
struct WindowConfig
{
    std::string title = "p4v-git";
    int width = 1280;
    int height = 800;
};

struct FramebufferSize
{
    int width = 0;
    int height = 0;
};

class Window
{
public:
    virtual ~Window() = default;

    virtual bool Initialize(const WindowConfig& config) = 0;
    virtual void Shutdown() = 0;
    virtual bool ShouldClose() const = 0;
    virtual void PollEvents() = 0;
    virtual bool IsMinimized() const = 0;
    virtual void Sleep(int milliseconds) const = 0;
    virtual float ContentScale() const = 0;
    virtual FramebufferSize GetFramebufferSize() const = 0;
    virtual void* NativeHandle() const = 0;

    virtual const char* const* GetRequiredVulkanInstanceExtensions(uint32_t* count) const = 0;
    virtual VkSurfaceKHR CreateVulkanSurface(VkInstance instance, const VkAllocationCallbacks* allocator) const = 0;
};
}
