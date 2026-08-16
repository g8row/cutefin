# cutefin — design contract

A native Qt 6 Widgets Jellyfin client with an embedded libmpv player.
This document is the implementation contract for **architecture and the core API**.
Agents implement against it and do not deviate without flagging it.

> **See also `FEATURES.md`** — the full capability spec (browsing, subtitles, chapters,
> trickplay, skip-intro, the debug/stats overlay, remote control, settings) and the
> phase each feature lands in. `DESIGN.md` says *how the code is shaped*;
> `FEATURES.md` says *what the app does*.

## 0. Non-negotiables

| Goal | How it is met |
|---|---|
| Native on Linux + macOS | Qt Widgets + `QStyle`. No global stylesheet. All chrome colors from `QPalette`. |
| Maximum format compatibility | libmpv (ffmpeg) plays the file; server direct-play, transcode only as fallback. |
| Low resource usage | Bounded image caches, viewport-lazy poster loads, capped request concurrency, no per-frame allocations. |
| Fast, smooth animations | 60 fps `QVariantAnimation`/`QPropertyAnimation` on cheap properties only. Never animate layouts. |

**Qt Widgets, not QML.** QML cannot look native on macOS without reimplementing the
platform style, and a scenegraph + JS engine per window costs more RAM than the
widget stack. Animation smoothness is achieved with explicit animators instead.

## 1. Layout

```
cutefin/
├── CMakeLists.txt
├── README.md
├── DESIGN.md
├── cmake/FindMPV.cmake
├── src/
│   ├── main.cpp
│   ├── core/          Settings, ImageCache, NetworkQueue, Formatting
│   ├── api/           JellyfinClient, Models
│   ├── ui/            MainWindow + pages
│   │   └── widgets/   PosterCard, MediaRow, SmoothScrollArea, AsyncImage, …
│   └── player/        MpvWidget, PlayerWindow, PlaybackReporter
└── tests/             Qt Test targets
```

One class per header/source pair, `PascalCase.h` / `PascalCase.cpp`, matching the
class name. Include guards use `#pragma once`.

## 2. Build

CMake ≥ 3.21, C++17, Ninja. Qt 6 `Widgets Network Gui Concurrent Test`.
libmpv is found via `pkg-config mpv` in `cmake/FindMPV.cmake`, with a
`MPV_ROOT`/`CMAKE_PREFIX_PATH` fallback so Homebrew and distro packages both work.

macOS dev build:
```sh
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
cmake --build build
```
`AUTOMOC ON`, `AUTOUIC OFF` (no `.ui` files — layouts are built in code so
animation-relevant widgets are directly addressable), `AUTORCC ON`.

## 3. API layer

Base: `https://<host>`, all paths below are appended verbatim. Every authenticated
request carries:

```
Authorization: MediaBrowser Token="<token>", Client="cutefin",
               Device="<hostname>", DeviceId="<stable-uuid>", Version="<x.y.z>"
```

`DeviceId` must be **stable across runs** — persist a generated `QUuid` in settings.
Jellyfin ties playback sessions to it. Do not use `QSysInfo::machineUniqueId()` alone;
it is empty on some platforms.

All endpoints below were verified live against Jellyfin **10.11.8**. None of the
deprecated `/Users/{userId}/…` forms are used.

| Purpose | Request |
|---|---|
| Probe server | `GET /System/Info/Public` → `{ServerName, Version, Id}` |
| Log in | `POST /Users/AuthenticateByName` `{"Username","Pw"}` → `{AccessToken, User:{Id,Name}}` (auth header without `Token=`) |
| Log out | `POST /Sessions/Logout` |
| Libraries | `GET /UserViews` → `Items[]` with `CollectionType` ∈ `movies,tvshows,music,boxsets` |
| Continue watching | `GET /UserItems/Resume?limit=16&mediaTypes=Video` |
| Next up | `GET /Shows/NextUp?userId={uid}&limit=16` |
| Latest in library | `GET /Items/Latest?parentId={viewId}&limit=16` → **bare JSON array**, not an `{Items:[]}` envelope |
| Item detail | `GET /Items/{id}?userId={uid}` |
| Seasons | `GET /Shows/{seriesId}/Seasons?userId={uid}` |
| Episodes | `GET /Shows/{seriesId}/Episodes?seasonId={id}&userId={uid}` |
| Search | `GET /Items?userId={uid}&searchTerm={q}&recursive=true&includeItemTypes=Movie,Series,Episode&limit=40` |

`/Items/Latest` returning a bare array while everything else returns `{"Items":[…]}`
is a real inconsistency in the API — handle both shapes in the parser.

### Images

```
GET /Items/{id}/Images/{type}?tag={tag}&maxWidth={w}&quality=90
```
`type` ∈ `Primary, Backdrop, Thumb, Logo`. **Always pass `tag`** (from
`ImageTags`/`BackdropImageTags`) — it makes the response immutable and cacheable
forever. **Always pass `maxWidth`** at the real display width × `devicePixelRatio`,
rounded up to the next 100, so the server downscales instead of the client.

Items also carry `ImageBlurHashes` — a BlurHash string per image tag. Decode it to a
tiny 8×8 pixmap and upscale as the placeholder behind a loading poster. That is what
makes the grid feel instant. Implement a compact BlurHash decoder in
`core/BlurHash.{h,cpp}` (~80 lines, base83 → DCT); do not add a dependency.

### Playback resolution

```
POST /Items/{id}/PlaybackInfo?userId={uid}
{ "DeviceProfile": {...}, "EnableDirectPlay": true,
  "EnableDirectStream": true, "AutoOpenLiveStream": true }
```
Response gives `PlaySessionId` and `MediaSources[]`. For each source:

- `SupportsDirectPlay || SupportsDirectStream` → play
  `/Videos/{id}/stream?static=true&mediaSourceId={msId}&api_key={token}`
- else → play `<base><TranscodingUrl>` (already contains its own query string and key)

Because mpv is the decoder, advertise a **permissive** device profile so the server
almost always picks direct play: containers `mkv,mp4,avi,webm,mov,ts,m2ts,flv,ogv`,
video `h264,hevc,av1,vp9,vp8,mpeg4,mpeg2video,vc1,theora`, audio
`aac,ac3,eac3,dts,truehd,flac,opus,mp3,vorbis,pcm,alac`. Verified: the test server
returns `SupportsDirectPlay: true` for its h264/eac3 mkv content under this profile.

### Progress reporting

- `POST /Sessions/Playing` on start
- `POST /Sessions/Playing/Progress` every 10 s and on pause/seek
- `POST /Sessions/Playing/Stopped` on stop

Body: `{ItemId, PlaySessionId, MediaSourceId, PositionTicks, IsPaused, CanSeek:true}`.
**Ticks are 100 ns units** — `ticks = seconds * 10'000'000`. Getting this wrong is
the classic bug: resume positions land in the wrong place by a factor of 10⁷.

### `JellyfinClient` shape

One `QNetworkAccessManager` for the whole app, HTTP/2 enabled, with a
`QNetworkDiskCache`. Requests go through `core/NetworkQueue` which caps **6 in-flight
requests**; image loads are queued at lower priority than metadata so a scrolling
grid never starves the page it is on.

Every call returns a `QNetworkReply*`-backed handle or takes a context `QObject*` +
callback so replies are auto-cancelled when the requesting widget dies. A reply that
outlives its widget and writes into freed memory is the other classic bug here —
always `connect(reply, …, contextObject, lambda)` so Qt disconnects on destruction.

## 4. Models

Plain value types in `api/Models.h`, each with `static T fromJson(const QJsonObject&)`:

- `ServerInfo { name, version, id, url }`
- `UserSession { serverUrl, userId, userName, token, deviceId }`
- `MediaItem` — the workhorse: `id, name, type, overview, tagline, productionYear,
  runTimeTicks, communityRating, criticRating, officialRating, genres, studios,
  people, imageTags{}, backdropTags[], blurHashes{}, seriesId, seriesName, seasonId,
  seasonName, indexNumber, parentIndexNumber, userData{played, playedPercentage,
  playbackPositionTicks, isFavorite}`
- `MediaSource { id, container, supportsDirectPlay, supportsDirectStream,
  transcodingUrl, streams[] }`
- `MediaStream { type, codec, language, displayTitle, index, isDefault, isExternal }`

Value types, no QObject, cheap to copy (implicitly shared Qt containers).

## 5. UI

`MainWindow` owns a `QStackedWidget` with `ServerSelectPage`, `LoginPage`, `HomePage`,
`DetailPage`. Navigation goes through `MainWindow::navigateTo(Page*)`, which keeps a
back stack and cross-fades (see §6).

**Startup:** `Settings` has a stored `UserSession` → validate with
`GET /System/Info/Public` + a cheap authenticated call; on success go straight to
`HomePage`, on 401 drop the token and go to `LoginPage` with the server prefilled.

- **ServerSelectPage** — URL field, "Connect". Normalizes input: adds `https://` when
  no scheme, strips trailing `/`. Probes and shows the server name + version inline on
  success, the error string on failure. Remembers recently used servers.
- **LoginPage** — server name header, username/password, "Remember me" (default on),
  "Change server" back link. Enter submits. Errors shown inline, not in a modal.
- **HomePage** — vertically scrolling column of `MediaRow`s: Continue Watching, Next Up,
  then "Latest in <library>" per view. A row that resolves empty removes itself.
  Top bar: search field, user menu (username → Log out). Log out clears the stored
  session, calls `/Sessions/Logout`, returns to `LoginPage`.
- **DetailPage** — blurred backdrop behind a poster + metadata block (title, year,
  runtime, rating chips, genres, tagline, overview, cast). Primary "Play" button, plus
  "Resume from 12:34" when `playbackPositionTicks > 0`. For a series: season selector
  and an episode list; for an episode: series/season context.

### Widgets

- **`AsyncImage`** — `QLabel`-like. Given item id + image type + tag + target size, it
  paints the BlurHash placeholder immediately, requests the real image through
  `ImageCache`, and cross-fades it in over 180 ms on arrival. Cancels its request on
  destruction or when re-targeted.
- **`PosterCard`** — poster `AsyncImage` + title + subtitle, plus a progress bar strip
  when partially watched. Hover animates a `qreal hoverProgress` 0→1 over 140 ms
  (`OutCubic`); `paintEvent` uses it to lerp a scale of 1.0→1.03 and draw a focus ring
  in `palette().highlight()`. Keyboard focusable, same visual as hover, `Return`
  activates. Emits `activated(MediaItem)`.
- **`MediaRow`** — title label + horizontal `SmoothScrollArea` of `PosterCard`s, with
  chevron buttons that appear on hover when the content overflows.
- **`SmoothScrollArea`** — `QScrollArea` where wheel events accumulate into a target
  offset and a 16 ms timer eases the actual offset toward it (exponential, ~0.22/frame).
  Kills the stepped feel of default Qt scrolling. Must pass through native macOS
  trackpad momentum (`QWheelEvent::phase()`) rather than double-easing it.

### Poster loading discipline

`MediaRow` only requests images for cards intersecting the viewport, plus one screen of
prefetch either side. Cards scrolled far away release their pixmap back to the cache.
This is what keeps a large library flat in RAM.

## 6. Styling and animation rules

- No `qApp->setStyleSheet`. Per-widget stylesheets are allowed **only** for
  border-radius/padding on custom cards, and must not set colors.
- Every color: `palette().color(QPalette::Window | Base | Text | Highlight | …)`.
  Overlay scrims use `palette().shadow()` with alpha. React to
  `QEvent::PaletteChange` / `ApplicationPaletteChange` so a live macOS dark-mode
  toggle repaints correctly.
- Page transitions: 200 ms cross-fade via `QGraphicsOpacityEffect` on the outgoing and
  incoming page. Install the effect for the duration of the animation and **remove it
  afterwards** — a permanent `QGraphicsEffect` forces every repaint through an
  offscreen buffer and will tank scrolling performance.
- Animate only: opacity, scroll offset, and custom `qreal` properties driving
  `paintEvent`. Never animate `setGeometry` on a laid-out widget, never animate
  anything that triggers a relayout of a scroll area's contents.
- Easing: `OutCubic` for entrances/hover, `InOutCubic` for transitions. Durations
  140–220 ms. Anything slower feels sluggish; anything faster reads as a jump.

## 7. Player

### `MpvWidget : QOpenGLWidget`

Embeds libmpv through the **render API** — the only approach that works on both
macOS and Linux from a Qt Widgets host (`--wid` embedding is X11/Windows-only).

- `mpv_create()`, then set options **before** `mpv_initialize()`:
  `vo=libmpv`, `hwdec=auto-safe`, `keep-open=yes`, `force-window=no`,
  `terminal=no`, `ytdl=no`.
- After `initialize`: `mpv_render_context_create` with
  `MPV_RENDER_PARAM_API_TYPE = MPV_RENDER_API_TYPE_OPENGL` and an
  `mpv_opengl_init_params{get_proc_address}` resolving through
  `QOpenGLContext::currentContext()->getProcAddress`.
- `mpv_render_context_set_update_callback` fires **on an mpv thread**. It must not
  touch Qt directly — emit a queued signal whose slot calls `update()`.
- `paintGL()`: `mpv_render_context_render` with `mpv_opengl_fbo{ defaultFramebufferObject(), w*dpr, h*dpr }` and `flip_y = 1`.
- Destruction order matters and is the #1 crash source here:
  `makeCurrent()` → `mpv_render_context_free()` → `doneCurrent()` → `mpv_terminate_destroy()`.
- `mpv_set_wakeup_callback` → queued signal → drain `mpv_wait_event(0)` on the GUI
  thread. Observe `time-pos`, `duration`, `pause`, `core-idle`, `eof-reached`,
  `track-list` via `mpv_observe_property` and re-emit as Qt signals.

`main()` must set the surface format **before** constructing `QApplication`:
OpenGL 3.3, `CoreProfile`, depth 0, stencil 8, `setDefaultFormat`. macOS gives no
usable modern GL context otherwise.

### `PlayerWindow`

Frameless-capable window (not fullscreen-only) hosting `MpvWidget` with a controls
overlay: play/pause, seek slider with hover time preview, volume, audio-track and
subtitle-track menus (populated from mpv's `track-list`), fullscreen toggle.

Controls auto-hide after 2.5 s of mouse stillness and fade back on movement (opacity
animation on the overlay container, not a repaint of the video). The mouse cursor
hides with them.

Keyboard: `Space` play/pause, `←/→` ±5 s, `Shift+←/→` ±1 min, `↑/↓` volume,
`F`/double-click fullscreen, `Esc` leave fullscreen then close, `M` mute,
`J`/`K`/`L` as in the web client.

Start playback with `start=<seconds>` when resuming. Pass the auth header via mpv's
`http-header-fields` option **only** for the transcode URL; the direct-play URL
carries `api_key` in the query string already.

### `PlaybackReporter`

Owns the `/Sessions/Playing*` lifecycle off `MpvWidget` signals, on a 10 s timer.
Reports a final `Stopped` with the exact position on close — that is what makes
"Continue Watching" correct on the next launch and in other clients.

## 8. Persistence

`QSettings` (`cutefin/cutefin`, native backend: plist on macOS, ini on Linux):
`server/url`, `server/recent[]`, `auth/userId`, `auth/userName`, `auth/token`,
`device/id`, `player/volume`, `window/geometry`.

The access token is stored in plaintext by `QSettings`. That is the same posture as
the official Jellyfin desktop clients, but the README must say so plainly, and
"Remember me" off must keep the token in memory only.

Image disk cache: `QStandardPaths::CacheLocation/images`, LRU, **256 MB cap**, pruned
on startup in a `QtConcurrent` job. Memory cache: `QPixmapCache` sized to 64 MB.

## 9. Tests

`QTest` targets, no network dependency in the default suite:

- `tst_models` — `fromJson` against **recorded fixtures** in `tests/fixtures/`
  (real captured payloads: item, latest-array, resume envelope, playbackinfo,
  seasons). Covers the bare-array vs envelope split and tick conversion.
- `tst_blurhash` — decodes a known hash to expected average color.
- `tst_settings` — round-trip and clear-on-logout, against a temp `QSettings` scope.
- `tst_playbackresolver` — direct-play vs transcode URL selection from fixture
  `PlaybackInfo` responses, including the `api_key` query construction.

A separate `tst_live` target, **not** run by default (`ctest -L live`), exercises the
real server from `CUTEFIN_TEST_SERVER`/`USER`/`PASS` env vars. Credentials never get
committed.

## 10. Definition of done

1. `cmake --build build` clean, no new warnings.
2. `ctest` green.
3. App launches, connects to a server, logs in, and the session survives a restart.
4. Home page shows populated rows with posters.
5. A movie plays with audio and video, seeks, and reports progress the server accepts.
6. Log out returns to login and the stored token is gone.
