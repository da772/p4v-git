#include "gui/GuiFactory.h"

#include "gui/imgui/ImGuiLayer.h"

namespace p4vgit
{
std::unique_ptr<GuiLayer> CreateGuiLayer(GuiBackend backend)
{
    switch (backend)
    {
    case GuiBackend::ImGui:
        return std::make_unique<ImGuiLayer>();
    }

    return nullptr;
}
}
