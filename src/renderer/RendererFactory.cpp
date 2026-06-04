#include "renderer/RendererFactory.h"

#include "renderer/vulkan/VulkanRenderer.h"

namespace p4vgit
{
std::unique_ptr<Renderer> CreateRenderer(RendererApi api)
{
    switch (api)
    {
    case RendererApi::Vulkan:
        return std::make_unique<VulkanRenderer>();
    }

    return nullptr;
}
}
