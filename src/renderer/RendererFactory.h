#pragma once

#include <memory>

namespace p4vgit
{
class Renderer;

enum class RendererApi
{
    Vulkan,
};

extern std::unique_ptr<Renderer> CreateRenderer(RendererApi api);
}
