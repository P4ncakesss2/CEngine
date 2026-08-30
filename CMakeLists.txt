cmake_minimum_required(VERSION 3.25)

project(cengine C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Vulkan REQUIRED)

add_subdirectory(external/cglm)
add_subdirectory(external/glfw)
add_subdirectory(external/vma)
add_subdirectory(external/box3d)

file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/core/*.c"
    "${CMAKE_SOURCE_DIR}/src/core/*.cpp"
)

list(FILTER CORE_SOURCES EXCLUDE REGEX "/src/game/")
list(FILTER CORE_SOURCES EXCLUDE REGEX "/src/editor/")
list(FILTER CORE_SOURCES EXCLUDE REGEX "/src/tool/")

file(GLOB_RECURSE CORE_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/core/*.h"
    "${CMAKE_SOURCE_DIR}/src/core/*.def"
)

set_source_files_properties(
    ${CORE_HEADERS}
    PROPERTIES HEADER_FILE_ONLY TRUE
)

set(CIMGUI_DIR "${CMAKE_SOURCE_DIR}/external/cimgui")
set(IMGUI_DIR "${CIMGUI_DIR}/imgui")

add_library(cimgui STATIC
    "${CIMGUI_DIR}/cimgui.cpp"
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_demo.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp"
)

target_include_directories(cimgui PUBLIC
    "${CIMGUI_DIR}"
    "${IMGUI_DIR}"
    "${IMGUI_DIR}/backends"
)

target_link_libraries(cimgui PRIVATE
    glfw
    Vulkan::Vulkan
)


add_library(cengine_core OBJECT
    ${CORE_SOURCES}
    ${CORE_HEADERS}
)

target_include_directories(cengine_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src/core
    ${Vulkan_INCLUDE_DIRS}
)

target_link_libraries(cengine_core PUBLIC
    cglm
    glfw
    VulkanMemoryAllocator
    Vulkan::Vulkan
    cimgui
    box3d::box3d
)

file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/game/*.c"
    "${CMAKE_SOURCE_DIR}/src/game/*.cpp"
)

add_executable(cengine_game
    ${GAME_SOURCES}
)

target_link_libraries(cengine_game PRIVATE
    cengine_core
)

file(GLOB_RECURSE EDITOR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/editor/*.c"
    "${CMAKE_SOURCE_DIR}/src/editor/*.cpp"
)

add_executable(cengine_editor
    ${EDITOR_SOURCES}
)

target_link_libraries(cengine_editor PRIVATE
    cengine_core
)