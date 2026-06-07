cmake_minimum_required(VERSION 3.24)

project(p4v-git
    VERSION 0.2.3
    DESCRIPTION "A cross-platform P4V-inspired Git desktop client with shelves, workspace changes, and review workflows"
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

set(P4VGIT_RESOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/CHANGELOG.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/icon.ico"
    "${CMAKE_CURRENT_SOURCE_DIR}/icon.png"
)

set(P4VGIT_PLATFORM_RESOURCES)

if(WIN32)
    enable_language(RC)
    list(APPEND P4VGIT_PLATFORM_RESOURCES
        cmake/windows/p4v-git.rc
    )
endif()

if(APPLE)
    set(P4VGIT_MACOS_ICONSET "${CMAKE_CURRENT_BINARY_DIR}/p4v-git.iconset")
    set(P4VGIT_MACOS_ICON "${CMAKE_CURRENT_BINARY_DIR}/p4v-git.icns")

    add_custom_command(
        OUTPUT "${P4VGIT_MACOS_ICON}"
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${P4VGIT_MACOS_ICONSET}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${P4VGIT_MACOS_ICONSET}"
        COMMAND sips -z 16 16 "${CMAKE_CURRENT_SOURCE_DIR}/icon.png" --out "${P4VGIT_MACOS_ICONSET}/icon_16x16.png"
        COMMAND sips -z 32 32 "${CMAKE_CURRENT_SOURCE_DIR}/icon.png" --out "${P4VGIT_MACOS_ICONSET}/icon_16x16@2x.png"
        COMMAND sips -z 32 32 "${CMAKE_CURRENT_SOURCE_DIR}/icon.png" --out "${P4VGIT_MACOS_ICONSET}/icon_32x32.png"
        COMMAND sips -z 64 64 "${CMAKE_CURRENT_SOURCE_DIR}/icon.png" --out "${P4VGIT_MACOS_ICONSET}/icon_32x32@2x.png"
        COMMAND sips -z 128 128 "${CMAKE_CURRENT_SOURCE_DIR}/icon.png" --out "${P4VGIT_MACOS_ICONSET}/icon_128x128.png"
        COMMAND sips -z 256 256 "${CMAKE_CURRENT_SOURCE_DIR}/icon.png" --out "${P4VGIT_MACOS_ICONSET}/icon_128x128@2x.png"
        COMMAND sips -z 256 256 "${CMAKE_CURRENT_SOURCE_DIR}/icon.png" --out "${P4VGIT_MACOS_ICONSET}/icon_256x256.png"
        COMMAND sips -z 512 512 "${CMAKE_CURRENT_SOURCE_DIR}/icon.png" --out "${P4VGIT_MACOS_ICONSET}/icon_256x256@2x.png"
        COMMAND sips -z 512 512 "${CMAKE_CURRENT_SOURCE_DIR}/icon.png" --out "${P4VGIT_MACOS_ICONSET}/icon_512x512.png"
        COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/icon.png" "${P4VGIT_MACOS_ICONSET}/icon_512x512@2x.png"
        COMMAND iconutil -c icns "${P4VGIT_MACOS_ICONSET}" -o "${P4VGIT_MACOS_ICON}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/icon.png"
        VERBATIM
    )

    set_source_files_properties("${P4VGIT_MACOS_ICON}" PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources
    )
    list(APPEND P4VGIT_PLATFORM_RESOURCES
        "${P4VGIT_MACOS_ICON}"
    )
endif()

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

if(MSVC)
    target_compile_options(imgui PRIVATE /MP)
endif()

set(P4VGIT_SOURCES
    src/application/Application.cpp
    src/git/GitRepository.cpp
    src/git/ShelfService.cpp
    src/git/WorkspaceState.cpp
    src/gui/GuiFactory.cpp
    src/gui/imgui/ImGuiLayer.cpp
    src/log/StdoutLog.cpp
    src/main.cpp
    src/platform/IconData.cpp
    src/platform/Process.cpp
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
    src/platform/IconData.h
    src/platform/Process.h
    src/platform/Window.h
    src/platform/WindowFactory.h
    src/platform/UrlLauncher.h
    src/platform/glfw/GlfwWindow.h
    src/platform/windows/P4vGitWindowsResource.h
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
    ${P4VGIT_RESOURCE_FILES}
    ${P4VGIT_PLATFORM_RESOURCES}
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source" FILES
    ${P4VGIT_SOURCES}
    ${P4VGIT_HEADERS}
)

source_group("Generated" FILES
    ${P4VGIT_GENERATED_HEADERS}
)

source_group("Resources" FILES
    ${P4VGIT_RESOURCE_FILES}
    ${P4VGIT_PLATFORM_RESOURCES}
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

if(WIN32)
    target_link_libraries(p4v-git PRIVATE Shell32)
endif()

if(MSVC)
    target_link_options(p4v-git PRIVATE /ENTRY:mainCRTStartup)
endif()

if(MSVC)
    target_compile_options(p4v-git PRIVATE /MP /W4 /permissive-)
else()
    target_compile_options(p4v-git PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(p4v-git PROPERTIES FOLDER "Apps")
if(WIN32)
    set_target_properties(p4v-git PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
if(APPLE)
    set_target_properties(p4v-git PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_ICON_FILE "p4v-git.icns"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.p4vgit.app"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
endif()
set_target_properties(imgui PROPERTIES FOLDER "External")
set_target_properties(glfw PROPERTIES FOLDER "External")

if(UNIX AND NOT APPLE)
    configure_file(
        cmake/linux/p4v-git.desktop.in
        "${CMAKE_CURRENT_BINARY_DIR}/p4v-git.desktop"
        @ONLY
    )

    install(TARGETS p4v-git RUNTIME DESTINATION bin)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/icon.png"
        DESTINATION share/icons/hicolor/1024x1024/apps
        RENAME p4v-git.png
    )
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/p4v-git.desktop"
        DESTINATION share/applications
    )
endif()
