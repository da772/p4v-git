#pragma once

namespace p4vgit
{
class GuiRendererBackend
{
public:
    virtual ~GuiRendererBackend() = default;

    virtual void InitializeGuiRendererBackend() = 0;
    virtual void ShutdownGuiRendererBackend() = 0;
    virtual void BeginGuiRendererFrame() = 0;
};
}
