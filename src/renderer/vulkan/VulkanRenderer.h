#pragma once

#include "renderer/Renderer.h"

#include <vulkan/vulkan.h>

#include <cstdint>

struct ImGui_ImplVulkanH_Window;
struct ImDrawData;
template<typename T>
struct ImVector;

namespace p4vgit
{
class Window;

class VulkanRenderer final : public Renderer
{
public:
    VulkanRenderer() = default;
    ~VulkanRenderer() override;

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    void Initialize(Window& window) override;
    void Shutdown() override;
    void WaitIdle() const override;
    void ResizeIfNeeded(Window& window) override;
    void Render(const GuiDrawData& draw_data) override;

    void InitializeGuiRendererBackend() override;
    void ShutdownGuiRendererBackend() override;
    void BeginGuiRendererFrame() override;

private:
    void SetupVulkan(Window& window);
    void SetupVulkanWindow(Window& window);
    void CleanupVulkanWindow();
    void CleanupVulkan();
    void FrameRender(ImDrawData* draw_data);
    void FramePresent();

    uint32_t ImageCount() const;
    VkRenderPass RenderPass() const;

    static void CheckVkResult(VkResult result);
    static bool IsExtensionAvailable(const ImVector<VkExtensionProperties>& properties, const char* extension);

    VkAllocationCallbacks* m_allocator = nullptr;
    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    uint32_t m_queueFamily = static_cast<uint32_t>(-1);
    VkQueue m_queue = VK_NULL_HANDLE;
    VkPipelineCache m_pipelineCache = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    ImGui_ImplVulkanH_Window* m_windowData = nullptr;
    uint32_t m_minImageCount = 2;
    bool m_swapchainRebuild = false;
    bool m_initialized = false;
    bool m_imguiBackendInitialized = false;
};
}
