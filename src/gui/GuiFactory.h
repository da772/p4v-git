#pragma once

#include <memory>

namespace p4vgit
{
class GuiLayer;

enum class GuiBackend
{
    ImGui,
};

extern std::unique_ptr<GuiLayer> CreateGuiLayer(GuiBackend backend);
}
