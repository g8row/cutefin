# cutefin — Architecture & Design

A fast, modern, multiplatform **Qt 6 Quick (QML) + C++17** Jellyfin client with a **single-window embedded `libmpv` player**, engineered to look web-grade while running effortlessly on **dual-core systems**.

## Non-Negotiables

| Goal | Implementation |
|---|---|
| Single window, zero external windows | `MpvVideoItem` (`QQuickFramebufferObject` + `MPV_RENDER_API_TYPE_OPENGL`) renders inside the single `ApplicationWindow` (`qml/Main.qml`) beneath QML cinema overlays. |
| Modern, web-like UI without Electron/Chromium | Hardware-accelerated Qt 6 Quick SceneGraph + AOT-compiled QML (`qt_add_qml_module`) with Jellyfin Deep Cinema Dark + Light/System themes (`qml/theme/Theme.qml`) and 36 monochrome hardware-tinted SVG icons. |
| Dual-core friendly | Zero live Gaussian/MultiEffect blur shaders; `16×16` GPU-upscaled BlurHash placeholders; dedicated `LowPriority` `QThreadPool` capped at `≤ 2` decode threads; virtualized C++ `QAbstractListModel`s; 4 Hz throttled playback time updates; auto-enabled Dual-Core / Low-Power mode (`--profile=fast`, 32 MiB image RAM cap, reduced motion) on `≤ 2` core CPUs; browse shell hidden while playing. |
| Multiplatform | macOS (OpenGL 3.3 CoreProfile + VideoToolbox + IOKit sleep inhibition), Linux X11/Wayland (OpenGL + VA-API/NVDEC + optional D-Bus sleep inhibition), Windows (OpenGL + D3D11VA + `SetThreadExecutionState`). |

## Layout

```
cutefin/
├── CMakeLists.txt
├── cmake/FindMPV.cmake
├── src/
│   ├── main.cpp                   OpenGL RHI setup, LC_NUMERIC="C", QML engine & providers
│   ├── core/                      Settings, NetworkQueue, ImageCache (QImage), BlurHash, Formatting, TrackSelection
│   ├── api/                       Models, JellyfinClient (REST + Quick Connect), PlaybackNavigation
│   ├── app/                       AppController, MediaItemModel, HomeSectionModel, ImageProviders
│   └── player/                    MpvVideoItem, PlayerController, PlaybackReporter, TrickplayCache, SleepInhibitor
├── qml/
│   ├── Main.qml                   Single-window shell + inline PlayerView + ToastHost
│   ├── theme/Theme.qml            Jellyfin cinema dark/light/system design tokens
│   ├── icons/*.svg                36 hardware-tinted monochrome icons
│   ├── components/                AppIcon, AppButton, AppTextField, BadgePill, PosterCard, LandscapeCard, HeroBanner, MediaRow, SidebarNav, TopBar, ToastHost, EmptyState
│   ├── pages/                     ServerSelectPage, LoginPage (Password + Quick Connect), HomePage, LibraryPage, DetailPage, SearchPage, SettingsPage
│   └── player/                    PlayerView, PlayerSeekBar (Chapters + Trickplay), PlayerDrawer, StatsOverlay, SkipAndUpNextOverlay
└── tests/                         Offline QTest suites + opt-in `tst_live` (`ctest -L live`)
```

## Build & Test

```sh
# macOS (Homebrew Qt 6 + mpv)
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
cmake --build build

# Offline unit test suite
ctest --test-dir build -LE live --output-on-failure

# Opt-in live Jellyfin server test
CUTEFIN_TEST_SERVER="https://..." CUTEFIN_TEST_USER="..." CUTEFIN_TEST_PASS="..." \
  ./build/tests/tst_live
```

## Security Posture
When "Remember me" is enabled, the Jellyfin access token is persisted via `QSettings` (native plist/ini), matching official Jellyfin desktop clients. Turning "Remember me" off keeps the token in memory only.
