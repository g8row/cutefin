cmake_minimum_required(VERSION 3.21)
project(cutefin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Network
    Concurrent
    Qml
    Quick
    QuickControls2
    OpenGL
    Svg
    Test
)

qt_standard_project_setup(REQUIRES 6.5)
qt_policy(SET QTP0004 NEW)

option(CUTEFIN_ENABLE_MPV "Build the libmpv-backed embedded player" ON)
if(CUTEFIN_ENABLE_MPV)
    find_package(MPV)
    if(NOT MPV_FOUND)
        message(WARNING "libmpv not found; continuing without embedded mpv playback. "
                        "Set CUTEFIN_ENABLE_MPV=OFF to silence this warning.")
        set(CUTEFIN_ENABLE_MPV OFF)
    endif()
endif()

# ---------------------------------------------------------------------------
# cutefin_core: headless C++17 core (Settings, NetworkQueue, ImageCache,
# BlurHash, Formatting, TrackSelection, Models, JellyfinClient,
# PlaybackNavigation). Zero Qt6::Widgets dependency.
# ---------------------------------------------------------------------------
add_library(cutefin_core STATIC)
target_include_directories(cutefin_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(cutefin_core PUBLIC Qt6::Core Qt6::Gui Qt6::Network Qt6::Concurrent)

add_subdirectory(src/core)
add_subdirectory(src/api)

# ---------------------------------------------------------------------------
# cutefin executable: Qt 6 Quick / QML frontend + embedded libmpv player
# ---------------------------------------------------------------------------
qt_add_executable(cutefin src/main.cpp)

target_link_libraries(cutefin PRIVATE
    cutefin_core
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::OpenGL
    Qt6::Svg
)

add_subdirectory(src/app)
add_subdirectory(src/player)

if(CUTEFIN_ENABLE_MPV)
    target_link_libraries(cutefin PRIVATE MPV::MPV)
    target_compile_definitions(cutefin PRIVATE CUTEFIN_ENABLE_MPV)
endif()

if(APPLE)
    target_link_libraries(cutefin PRIVATE "-framework IOKit")
elseif(UNIX)
    find_package(Qt6 QUIET COMPONENTS DBus)
    if(Qt6DBus_FOUND)
        target_link_libraries(cutefin PRIVATE Qt6::DBus)
        target_compile_definitions(cutefin PRIVATE CUTEFIN_HAVE_DBUS)
    endif()
endif()

set_source_files_properties(qml/theme/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

file(GLOB CUTEFIN_ICON_RESOURCES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS "qml/icons/*.svg")

qt_add_qml_module(cutefin
    URI Cutefin
    VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml/Cutefin"
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        qml/Main.qml
        qml/theme/Theme.qml
        qml/components/AppIcon.qml
        qml/components/AppButton.qml
        qml/components/AppTextField.qml
        qml/components/BadgePill.qml
        qml/components/EmptyState.qml
        qml/components/PosterCard.qml
        qml/components/LandscapeCard.qml
        qml/components/HeroBanner.qml
        qml/components/MediaRow.qml
        qml/components/SidebarNav.qml
        qml/components/TopBar.qml
        qml/components/ToastHost.qml
        qml/pages/ServerSelectPage.qml
        qml/pages/LoginPage.qml
        qml/pages/HomePage.qml
        qml/pages/LibraryPage.qml
        qml/pages/DetailPage.qml
        qml/pages/SearchPage.qml
        qml/pages/SettingsPage.qml
        qml/player/PlayerView.qml
        qml/player/PlayerSeekBar.qml
        qml/player/PlayerDrawer.qml
        qml/player/StatsOverlay.qml
        qml/player/SkipAndUpNextOverlay.qml
    RESOURCES
        ${CUTEFIN_ICON_RESOURCES}
)

enable_testing()
add_subdirectory(tests)
