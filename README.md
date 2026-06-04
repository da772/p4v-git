# p4v-git

A cross-platform C++ desktop application aiming to provide a P4V-style interface backed by Git.

Current version: `0.1.4`

The first milestone boots a GLFW window, initializes Vulkan, enables ImGui docking, uses FIFO present mode for VSync, and shows the Dear ImGui demo window.

## Prerequisites

- CMake 3.24 or newer
- A C++20 compiler
  - Windows: Visual Studio 2022 or newer
  - macOS: Xcode command line tools or Apple Clang
  - Linux: GCC or Clang
- Vulkan SDK and loader
  - Windows/macOS: install the LunarG Vulkan SDK and ensure `VULKAN_SDK` is set
  - Linux: install Vulkan development packages for your distribution
  
## Setup

Clone dependencies:

```sh
git submodule update --init --recursive
```

Configure:

```sh
cmake -S . -B build
```

Build:

```sh
cmake --build build --config Debug
```

Run:

```sh
# Windows
.\build\Debug\p4v-git.exe

# Linux/macOS
./build/p4v-git
```

## Notes

- GLFW and ImGui are vendored as git submodules under `external/`.
- The Vulkan swapchain is created with `VK_PRESENT_MODE_FIFO_KHR`, which is guaranteed by Vulkan and provides VSync behavior.
- ImGui is pinned to the docking branch so later P4V-style panes can be built with dockable layouts.
- The application version is defined in the root `CMakeLists.txt` `project(... VERSION ...)` value. Bump it for each update; CMake generates `P4vGitVersion.h` from that value.
