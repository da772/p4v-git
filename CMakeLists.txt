cmake_minimum_required(VERSION 3.24)

project(p4v-git
    VERSION 0.1.0
    DESCRIPTION "A P4V-style Git desktop client"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan REQUIRED)

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

target_include_directories(imgui PUBLIC
    external/imgui
    external/imgui/backends
)

target_link_libraries(imgui PUBLIC
    glfw
    Vulkan::Vulkan
)

add_executable(p4v-git
    src/main.cpp
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
