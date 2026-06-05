# p4v-git

<img src="icon.png" alt="p4v-git icon" width="96">

`p4v-git` is a cross-platform desktop Git client inspired by Perforce P4V. The goal is to provide a familiar workspace-oriented UI for browsing a repository, organizing active file changes into shelves, reviewing shelf diffs, opening/merging pull requests, and submitting changes back to a selected target branch.

Current version: `0.1.24`

The application is built with C++20, CMake, GLFW, Vulkan, and Dear ImGui docking.

## Features

- P4V-style Workspace Explorer, File Changes, and Log panels while keeping Git as the source-control backend.
- File checkout intent that lets users group local Git changes into Perforce-like shelves without staging files directly.
- Shelf workflows backed by Git branches and GitHub pull requests, including shelve, submit, link, restore, revert, and delete actions.
- Target-branch selection so teams can submit shelves into the branch they actually use, not only `main`.
- Active change lists that support multi-select, drag/drop between shelves, and VS Code diffs.
- Background Git/GitHub operations with visible UI busy states so the app feels like a desktop client instead of a command wrapper.

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

See `CHANGELOG.txt` for versioned changes.
