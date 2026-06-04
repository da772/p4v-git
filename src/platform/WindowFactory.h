#pragma once

#include "platform/Window.h"

#include <memory>

namespace p4vgit
{
enum class WindowBackend
{
    Glfw,
};

extern std::unique_ptr<Window> CreateWindow(WindowBackend backend);
}
