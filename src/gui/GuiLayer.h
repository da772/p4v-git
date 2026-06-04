#pragma once

#include "gui/GuiDrawData.h"

namespace p4vgit
{
class GuiRendererBackend;
class Window;

class GuiLayer
{
public:
    virtual ~GuiLayer() = default;

    virtual void Initialize(Window& window, GuiRendererBackend& renderer_backend) = 0;
    virtual void Shutdown() = 0;
    virtual void BeginFrame() = 0;
    virtual void EndFrame() = 0;
    virtual GuiDrawData GetDrawData() const = 0;
};
}
