#pragma once

#include <cstdint>
#include <span>

namespace p4vgit
{
struct AppIconPixels
{
    int width = 0;
    int height = 0;
    std::span<const uint8_t> rgba;
};

extern AppIconPixels RuntimeWindowIcon();
}
