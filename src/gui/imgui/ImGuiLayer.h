#pragma once

#include "gui/GuiLayer.h"

namespace p4vgit
{
class ImGuiLayer final : public GuiLayer
{
public:
    ImGuiLayer() = default;
    ~ImGuiLayer() override;

    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    void Initialize(Window& window, GuiRendererBackend& m_rendererbackend) override;
    void Shutdown() override;
    void BeginFrame() override;
    void EndFrame() override;
    GuiDrawData GetDrawData() const override;

private:
    GuiRendererBackend* m_rendererBackend = nullptr;
    bool m_initialized = false;
};
}
