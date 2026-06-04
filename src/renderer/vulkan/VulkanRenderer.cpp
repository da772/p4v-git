#include "renderer/vulkan/VulkanRenderer.h"

#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "platform/Window.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace p4vgit
{
VulkanRenderer::~VulkanRenderer()
{
    Shutdown();
}

void VulkanRenderer::Initialize(Window& window)
{
    SetupVulkan(window);
    SetupVulkanWindow(window);
    m_initialized = true;
}

void VulkanRenderer::Shutdown()
{
    if (!m_initialized)
        return;

    CleanupVulkanWindow();
    CleanupVulkan();
    m_initialized = false;
}

void VulkanRenderer::WaitIdle() const
{
    if (m_device != VK_NULL_HANDLE)
        CheckVkResult(vkDeviceWaitIdle(m_device));
}

void VulkanRenderer::ResizeIfNeeded(Window& window)
{
    const FramebufferSize framebuffer_size = window.GetFramebufferSize();
    const int framebuffer_width = framebuffer_size.width;
    const int framebuffer_height = framebuffer_size.height;

    if (framebuffer_width <= 0 || framebuffer_height <= 0)
        return;

    if (!m_swapchainRebuild &&
        m_windowData->Width == framebuffer_width &&
        m_windowData->Height == framebuffer_height)
    {
        return;
    }

    ImGui_ImplVulkan_SetMinImageCount(m_minImageCount);
    ImGui_ImplVulkanH_CreateOrResizeWindow(
        m_instance,
        m_physicalDevice,
        m_device,
        m_windowData,
        m_queueFamily,
        m_allocator,
        framebuffer_width,
        framebuffer_height,
        m_minImageCount,
        0);

    m_windowData->FrameIndex = 0;
    m_swapchainRebuild = false;
}

void VulkanRenderer::Render(const GuiDrawData& gui_draw_data)
{
    ImDrawData* draw_data = static_cast<ImDrawData*>(gui_draw_data.NativeDrawData());
    if (draw_data == nullptr || draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f)
        return;

    const ImVec4 clear_color = ImVec4(0.10f, 0.11f, 0.13f, 1.00f);
    m_windowData->ClearValue.color.float32[0] = clear_color.x * clear_color.w;
    m_windowData->ClearValue.color.float32[1] = clear_color.y * clear_color.w;
    m_windowData->ClearValue.color.float32[2] = clear_color.z * clear_color.w;
    m_windowData->ClearValue.color.float32[3] = clear_color.w;

    FrameRender(draw_data);
    FramePresent();
}

void VulkanRenderer::InitializeGuiRendererBackend()
{
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.ApiVersion = VK_API_VERSION_1_0;
    init_info.Instance = m_instance;
    init_info.PhysicalDevice = m_physicalDevice;
    init_info.Device = m_device;
    init_info.QueueFamily = m_queueFamily;
    init_info.Queue = m_queue;
    init_info.PipelineCache = m_pipelineCache;
    init_info.DescriptorPool = m_descriptorPool;
    init_info.MinImageCount = m_minImageCount;
    init_info.ImageCount = ImageCount();
    init_info.Allocator = m_allocator;
    init_info.PipelineInfoMain.RenderPass = RenderPass();
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.CheckVkResultFn = CheckVkResult;
    ImGui_ImplVulkan_Init(&init_info);

    m_imguiBackendInitialized = true;
}

void VulkanRenderer::ShutdownGuiRendererBackend()
{
    if (!m_imguiBackendInitialized)
        return;

    ImGui_ImplVulkan_Shutdown();
    m_imguiBackendInitialized = false;
}

void VulkanRenderer::BeginGuiRendererFrame()
{
    ImGui_ImplVulkan_NewFrame();
}

uint32_t VulkanRenderer::ImageCount() const
{
    return m_windowData->ImageCount;
}

VkRenderPass VulkanRenderer::RenderPass() const
{
    return m_windowData->RenderPass;
}

void VulkanRenderer::CheckVkResult(VkResult result)
{
    if (result == VK_SUCCESS)
        return;

    std::fprintf(stderr, "[vulkan] Error: VkResult = %d\n", result);
    if (result < 0)
        std::abort();
}

bool VulkanRenderer::IsExtensionAvailable(const ImVector<VkExtensionProperties>& properties, const char* extension)
{
    for (const VkExtensionProperties& property : properties)
    {
        if (std::strcmp(property.extensionName, extension) == 0)
            return true;
    }

    return false;
}

void VulkanRenderer::SetupVulkan(Window& window)
{
    VkResult err;

    ImVector<const char*> m_instanceextensions;
    uint32_t extensions_count = 0;
    const char* const* required_extensions = window.GetRequiredVulkanInstanceExtensions(&extensions_count);
    for (uint32_t i = 0; i < extensions_count; ++i)
        m_instanceextensions.push_back(required_extensions[i]);

    {
        VkApplicationInfo app_info = {};
        app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app_info.pApplicationName = "p4v-git";
        app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
        app_info.pEngineName = "p4v-git";
        app_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
        app_info.apiVersion = VK_API_VERSION_1_0;

        VkInstanceCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        create_info.pApplicationInfo = &app_info;

        uint32_t properties_count = 0;
        err = vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, nullptr);
        CheckVkResult(err);

        ImVector<VkExtensionProperties> properties;
        properties.resize(static_cast<int>(properties_count));
        err = vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, properties.Data);
        CheckVkResult(err);

        if (IsExtensionAvailable(properties, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
            m_instanceextensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
        if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
        {
            m_instanceextensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
            create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        }
#endif

        create_info.enabledExtensionCount = static_cast<uint32_t>(m_instanceextensions.Size);
        create_info.ppEnabledExtensionNames = m_instanceextensions.Data;
        err = vkCreateInstance(&create_info, m_allocator, &m_instance);
        CheckVkResult(err);
    }

    m_physicalDevice = ImGui_ImplVulkanH_SelectPhysicalDevice(m_instance);
    if (m_physicalDevice == VK_NULL_HANDLE)
    {
        std::fprintf(stderr, "Vulkan: no suitable physical device found\n");
        std::abort();
    }

    m_queueFamily = ImGui_ImplVulkanH_SelectQueueFamilyIndex(m_physicalDevice);
    if (m_queueFamily == static_cast<uint32_t>(-1))
    {
        std::fprintf(stderr, "Vulkan: no graphics queue family found\n");
        std::abort();
    }

    {
        ImVector<const char*> m_deviceextensions;
        m_deviceextensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

        uint32_t properties_count = 0;
        err = vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &properties_count, nullptr);
        CheckVkResult(err);

        ImVector<VkExtensionProperties> properties;
        properties.resize(static_cast<int>(properties_count));
        err = vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &properties_count, properties.Data);
        CheckVkResult(err);

#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
        if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
            m_deviceextensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif

        const float m_queuepriority[] = { 1.0f };
        VkDeviceQueueCreateInfo m_queueinfo = {};
        m_queueinfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        m_queueinfo.queueFamilyIndex = m_queueFamily;
        m_queueinfo.queueCount = 1;
        m_queueinfo.pQueuePriorities = m_queuepriority;

        VkDeviceCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create_info.queueCreateInfoCount = 1;
        create_info.pQueueCreateInfos = &m_queueinfo;
        create_info.enabledExtensionCount = static_cast<uint32_t>(m_deviceextensions.Size);
        create_info.ppEnabledExtensionNames = m_deviceextensions.Data;

        err = vkCreateDevice(m_physicalDevice, &create_info, m_allocator, &m_device);
        CheckVkResult(err);
        vkGetDeviceQueue(m_device, m_queueFamily, 0, &m_queue);
    }

    {
        VkDescriptorPoolSize pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE },
            { VK_DESCRIPTOR_TYPE_SAMPLER, IMGUI_IMPL_VULKAN_MINIMUM_SAMPLER_POOL_SIZE },
        };

        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.poolSizeCount = static_cast<uint32_t>(IM_ARRAYSIZE(pool_sizes));
        pool_info.pPoolSizes = pool_sizes;

        for (const VkDescriptorPoolSize& pool_size : pool_sizes)
            pool_info.maxSets += pool_size.descriptorCount;

        err = vkCreateDescriptorPool(m_device, &pool_info, m_allocator, &m_descriptorPool);
        CheckVkResult(err);
    }
}

void VulkanRenderer::SetupVulkanWindow(Window& window)
{
    m_surface = window.CreateVulkanSurface(m_instance, m_allocator);
    if (m_surface == VK_NULL_HANDLE)
    {
        std::fprintf(stderr, "Vulkan: failed to create window surface\n");
        std::abort();
    }

    VkBool32 m_surfacesupported = VK_FALSE;
    VkResult err = vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, m_queueFamily, m_surface, &m_surfacesupported);
    CheckVkResult(err);
    if (m_surfacesupported != VK_TRUE)
    {
        std::fprintf(stderr, "Vulkan: no WSI support on selected physical device\n");
        std::exit(EXIT_FAILURE);
    }

    const VkFormat requested_formats[] = {
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8_UNORM,
        VK_FORMAT_R8G8B8_UNORM,
    };

    m_windowData = new ImGui_ImplVulkanH_Window();
    m_windowData->Surface = m_surface;
    m_windowData->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
        m_physicalDevice,
        m_windowData->Surface,
        requested_formats,
        static_cast<size_t>(IM_ARRAYSIZE(requested_formats)),
        VK_COLORSPACE_SRGB_NONLINEAR_KHR);

    const VkPresentModeKHR present_modes[] = { VK_PRESENT_MODE_FIFO_KHR };
    m_windowData->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(
        m_physicalDevice,
        m_windowData->Surface,
        present_modes,
        IM_ARRAYSIZE(present_modes));

    if (m_windowData->PresentMode != VK_PRESENT_MODE_FIFO_KHR)
    {
        std::fprintf(stderr, "Vulkan: failed to select FIFO present mode for VSync\n");
        std::exit(EXIT_FAILURE);
    }

    const FramebufferSize framebuffer_size = window.GetFramebufferSize();

    ImGui_ImplVulkanH_CreateOrResizeWindow(
        m_instance,
        m_physicalDevice,
        m_device,
        m_windowData,
        m_queueFamily,
        m_allocator,
        framebuffer_size.width,
        framebuffer_size.height,
        m_minImageCount,
        0);
}

void VulkanRenderer::CleanupVulkanWindow()
{
    if (m_windowData != nullptr)
    {
        ImGui_ImplVulkanH_DestroyWindow(m_instance, m_device, m_windowData, m_allocator);
        delete m_windowData;
        m_windowData = nullptr;
    }

    if (m_surface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(m_instance, m_surface, m_allocator);
        m_surface = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::CleanupVulkan()
{
    if (m_descriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(m_device, m_descriptorPool, m_allocator);
        m_descriptorPool = VK_NULL_HANDLE;
    }

    if (m_device != VK_NULL_HANDLE)
    {
        vkDestroyDevice(m_device, m_allocator);
        m_device = VK_NULL_HANDLE;
    }

    if (m_instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(m_instance, m_allocator);
        m_instance = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::FrameRender(ImDrawData* draw_data)
{
    VkSemaphore image_acquired_semaphore = m_windowData->FrameSemaphores[m_windowData->SemaphoreIndex].ImageAcquiredSemaphore;
    VkSemaphore render_complete_semaphore = m_windowData->FrameSemaphores[m_windowData->SemaphoreIndex].RenderCompleteSemaphore;

    VkResult err = vkAcquireNextImageKHR(
        m_device,
        m_windowData->Swapchain,
        UINT64_MAX,
        image_acquired_semaphore,
        VK_NULL_HANDLE,
        &m_windowData->FrameIndex);

    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
        m_swapchainRebuild = true;
    if (err == VK_ERROR_OUT_OF_DATE_KHR)
        return;
    if (err != VK_SUBOPTIMAL_KHR)
        CheckVkResult(err);

    ImGui_ImplVulkanH_Frame* frame_data = &m_windowData->Frames[m_windowData->FrameIndex];

    err = vkWaitForFences(m_device, 1, &frame_data->Fence, VK_TRUE, UINT64_MAX);
    CheckVkResult(err);
    err = vkResetFences(m_device, 1, &frame_data->Fence);
    CheckVkResult(err);
    err = vkResetCommandPool(m_device, frame_data->CommandPool, 0);
    CheckVkResult(err);

    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    err = vkBeginCommandBuffer(frame_data->CommandBuffer, &begin_info);
    CheckVkResult(err);

    VkRenderPassBeginInfo render_pass_info = {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = m_windowData->RenderPass;
    render_pass_info.framebuffer = frame_data->Framebuffer;
    render_pass_info.renderArea.extent.width = m_windowData->Width;
    render_pass_info.renderArea.extent.height = m_windowData->Height;
    render_pass_info.clearValueCount = 1;
    render_pass_info.pClearValues = &m_windowData->ClearValue;
    vkCmdBeginRenderPass(frame_data->CommandBuffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    ImGui_ImplVulkan_RenderDrawData(draw_data, frame_data->CommandBuffer);

    vkCmdEndRenderPass(frame_data->CommandBuffer);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &image_acquired_semaphore;
    submit_info.pWaitDstStageMask = &wait_stage;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &frame_data->CommandBuffer;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &render_complete_semaphore;

    err = vkEndCommandBuffer(frame_data->CommandBuffer);
    CheckVkResult(err);
    err = vkQueueSubmit(m_queue, 1, &submit_info, frame_data->Fence);
    CheckVkResult(err);
}

void VulkanRenderer::FramePresent()
{
    if (m_swapchainRebuild)
        return;

    VkSemaphore render_complete_semaphore = m_windowData->FrameSemaphores[m_windowData->SemaphoreIndex].RenderCompleteSemaphore;

    VkPresentInfoKHR present_info = {};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &render_complete_semaphore;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &m_windowData->Swapchain;
    present_info.pImageIndices = &m_windowData->FrameIndex;

    VkResult err = vkQueuePresentKHR(m_queue, &present_info);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
        m_swapchainRebuild = true;
    if (err == VK_ERROR_OUT_OF_DATE_KHR)
        return;
    if (err != VK_SUBOPTIMAL_KHR)
        CheckVkResult(err);

    m_windowData->SemaphoreIndex = (m_windowData->SemaphoreIndex + 1) % m_windowData->SemaphoreCount;
}
}
