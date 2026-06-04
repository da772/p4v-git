cmake_minimum_required(VERSION 3.24)

project(p4v-git
    VERSION 0.1.5
    DESCRIPTION "A P4V-style Git desktop client"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

find_package(Vulkan REQUIRED)

configure_file(
    cmake/P4vGitVersion.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/P4vGitVersion.h"
    @ONLY
)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(external/glfw)

add_library(imgui STATIC
    external/imgui/imgui.cpp
    external/imgui/imgui_demo.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_tables.cpp
    external/imgui/imgui_widgets.cpp
    external/imgui/backends/imgui_impl_glfw.cpp
    external/imgui/backends/imgui_impl_vulkan.cpp
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/external/imgui" PREFIX "External\\imgui" FILES
    external/imgui/imgui.cpp
    external/imgui/imgui_demo.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_tables.cpp
    external/imgui/imgui_widgets.cpp
    external/imgui/backends/imgui_impl_glfw.cpp
    external/imgui/backends/imgui_impl_vulkan.cpp
)

target_include_directories(imgui PUBLIC
    external/imgui
    external/imgui/backends
)

target_link_libraries(imgui PUBLIC
    glfw
    Vulkan::Vulkan
)

set(P4VGIT_SOURCES
    src/application/Application.cpp
    src/git/GitRepository.cpp
    src/git/ShelfService.cpp
    src/git/WorkspaceState.cpp
    src/gui/GuiFactory.cpp
    src/gui/imgui/ImGuiLayer.cpp
    src/log/StdoutLog.cpp
    src/main.cpp
    src/platform/UrlLauncher.cpp
    src/platform/WindowFactory.cpp
    src/platform/glfw/GlfwWindow.cpp
    src/renderer/RendererFactory.cpp
    src/renderer/vulkan/VulkanRenderer.cpp
    src/ui/AppUi.cpp
    src/ui/widgets/imgui/ImGuiWidgets.cpp
)

set(P4VGIT_GENERATED_HEADERS
    "${CMAKE_CURRENT_BINARY_DIR}/generated/P4vGitVersion.h"
)

set(P4VGIT_HEADERS
    src/application/Application.h
    src/git/GitRepository.h
    src/git/ShelfService.h
    src/git/WorkspaceState.h
    src/gui/GuiDrawData.h
    src/gui/GuiFactory.h
    src/gui/GuiLayer.h
    src/gui/GuiRendererBackend.h
    src/gui/imgui/ImGuiLayer.h
    src/log/StdoutLog.h
    src/platform/Window.h
    src/platform/WindowFactory.h
    src/platform/UrlLauncher.h
    src/platform/glfw/GlfwWindow.h
    src/renderer/Renderer.h
    src/renderer/RendererFactory.h
    src/renderer/vulkan/VulkanRenderer.h
    src/ui/AppUi.h
    src/ui/widgets/Widgets.h
)

add_executable(p4v-git
    ${P4VGIT_SOURCES}
    ${P4VGIT_HEADERS}
    ${P4VGIT_GENERATED_HEADERS}
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source" FILES
    ${P4VGIT_SOURCES}
    ${P4VGIT_HEADERS}
)

source_group("Generated" FILES
    ${P4VGIT_GENERATED_HEADERS}
)

target_include_directories(p4v-git PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
    src
)

target_link_libraries(p4v-git PRIVATE
    imgui
    glfw
    Vulkan::Vulkan
)

if(MSVC)
    target_compile_options(p4v-git PRIVATE /W4 /permissive-)
else()
    target_compile_options(p4v-git PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(p4v-git PROPERTIES FOLDER "Apps")
set_target_properties(imgui PROPERTIES FOLDER "External")
set_target_properties(glfw PROPERTIES FOLDER "External")
