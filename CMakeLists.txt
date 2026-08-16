cmake_minimum_required(VERSION 3.21)
project(cutefin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Gui Concurrent Test)

option(CUTEFIN_ENABLE_MPV "Build the libmpv-backed player" ON)
if(CUTEFIN_ENABLE_MPV)
    find_package(MPV)
    # QOpenGLWidget (MpvWidget's base class) lives in Qt6::OpenGLWidgets, a
    # separate module from Widgets since Qt 6 -- DESIGN.md §2's component
    # list predates the player and doesn't mention it.
    find_package(Qt6 QUIET COMPONENTS OpenGLWidgets)
    if(NOT MPV_FOUND OR NOT Qt6OpenGLWidgets_FOUND)
        message(WARNING "libmpv and/or Qt6::OpenGLWidgets not found; "
                         "continuing without player support. Set CUTEFIN_ENABLE_MPV=OFF "
                         "to silence this warning.")
        set(CUTEFIN_ENABLE_MPV OFF)
    endif()
endif()

# ---------------------------------------------------------------------------
# cutefin_core: everything in src/core (Settings, NetworkQueue, ImageCache,
# BlurHash, Formatting) and src/api (Models, JellyfinClient). This is the
# library later phases (ui/, player/) link against and unit-test independently
# of the GUI. Each subdirectory appends its own sources via target_sources()
# so parallel agents editing core/ and api/ never touch this file.
# ---------------------------------------------------------------------------
add_library(cutefin_core STATIC)
target_include_directories(cutefin_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(cutefin_core PUBLIC Qt6::Widgets Qt6::Network Qt6::Gui Qt6::Concurrent)

add_subdirectory(src/core)
add_subdirectory(src/api)

# ---------------------------------------------------------------------------
# cutefin executable. src/main.cpp lives here; src/ui and src/player append
# their own sources via target_sources() in their own CMakeLists.txt as those
# directories are filled in, so nothing else in this file needs to change.
# ---------------------------------------------------------------------------
add_executable(cutefin src/main.cpp)
target_link_libraries(cutefin PRIVATE cutefin_core Qt6::Widgets Qt6::Network Qt6::Gui)

add_subdirectory(src/ui)

if(CUTEFIN_ENABLE_MPV)
    add_subdirectory(src/player)
    target_link_libraries(cutefin PRIVATE MPV::MPV Qt6::OpenGLWidgets)
    target_compile_definitions(cutefin PRIVATE CUTEFIN_ENABLE_MPV)
endif()

enable_testing()
add_subdirectory(tests)
