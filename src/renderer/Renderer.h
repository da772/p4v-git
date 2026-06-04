#pragma once

#include "gui/GuiDrawData.h"
#include "gui/GuiRendererBackend.h"

namespace p4vgit
{
class Window;

class Renderer : public GuiRendererBackend
{
public:
    ~Renderer() override = default;

    virtual void Initialize(Window& window) = 0;
    virtual void Shutdown() = 0;
    virtual void WaitIdle() const = 0;
    virtual void ResizeIfNeeded(Window& window) = 0;
    virtual void Render(const GuiDrawData& draw_data) = 0;
};
}
