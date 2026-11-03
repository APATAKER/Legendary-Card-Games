cmake_minimum_required(VERSION 3.25)
if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
endif()

project(LegendaryEncounters VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(SDL3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_path(STB_INCLUDE_DIRS "stb_image.h" REQUIRED)

add_executable(LegendaryEncounters WIN32
    src/main.cpp
    src/app/App.cpp
    src/app/Input.cpp
    src/app/Overlay.cpp
    src/gfx/Renderer2D.cpp
    src/gfx/Shader.cpp
    src/gfx/TextureCache.cpp
    src/gfx/stb_image_impl.cpp
    src/game/Assets.cpp
    src/game/Camera.cpp
    src/game/Table.cpp
    src/game/Setup.cpp
    src/game/Verify.cpp
)

target_include_directories(LegendaryEncounters PRIVATE src ${STB_INCLUDE_DIRS})

target_link_libraries(LegendaryEncounters PRIVATE
    SDL3::SDL3
    glad::glad
    imgui::imgui
    nlohmann_json::nlohmann_json
)

# Dev builds read assets straight from the source tree; a packaged build looks for an
# "assets" folder next to the executable first (see Assets.cpp).
target_compile_definitions(LegendaryEncounters PRIVATE
    LC_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
    LC_VERSION="${PROJECT_VERSION}"
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

if(MSVC)
    target_compile_options(LegendaryEncounters PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus /MP)
endif()

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT LegendaryEncounters)
