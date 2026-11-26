# cutefin — full capability spec

Companion to `DESIGN.md` (which holds architecture, module boundaries and the core
API contract). This document defines the **full feature set** and the phase each
feature lands in. Everything here was checked against the live Jellyfin **10.11.8**
test server; endpoints marked ✅ returned real data during design.

Target: feature parity with Jellyfin Media Player, minus its Chromium/web-UI stack —
native widgets, and mpv driven directly rather than through a JS bridge.

---

## A. Reference points

| Client | What we take from it |
|---|---|
| Jellyfin Media Player | mpv backend, audio passthrough/device selection, remembered track choices across episodes, subtitle position/scale/colour, refresh-rate switching |
| Swiftfin | Clean native detail pages, poster-first browsing |
| Findroid | Material-ish library browse with sort/filter, offline marking |
| jellyfin-mpv-shim | Remote-control-as-a-session model, mpv option passthrough |
| mpv `stats.lua` | The debug overlay's content and layout |

---

## B. Server capabilities — verified on 10.11.8

| Feature | Endpoint | Result |
|---|---|---|
| Chapters | `/Items/{id}?fields=Chapters` | ✅ 14 named chapters w/ `StartPositionTicks` on the test movie |
| Trickplay | `/Items/{id}?fields=Trickplay` → `/Videos/{id}/Trickplay/{width}/{index}.jpg` | ✅ 320×132 thumbs, 10×10 tiles, 702 thumbs, 10 s interval |
| Chapter images | `/Items/{id}/Images/Chapter/{index}` | ✅ |
| Media segments (skip intro/credits) | `/MediaSegments/{itemId}` | ✅ endpoint live, but returns **zero segments for every item** on the test server — no intro-detection plugin is configured. Skip-intro can be built against it, but cannot be verified here; do not mark it done on the strength of code review alone. |
| External subtitle fetch | `/Videos/{id}/{msId}/Subtitles/{index}/Stream.srt` | ✅ returns SRT |
| Subtitle search (OpenSubtitles) | `/Items/{id}/RemoteSearch/Subtitles/{lang}` | ✅ returns provider results |
| Quick Connect | **`GET`** `/QuickConnect/Initiate` | ✅ returns `{Secret, Code}` — note: **GET**, not POST |
| Remote control registration | `POST /Sessions/Capabilities/Full` | ✅ 204 |
| Active sessions | `/Sessions` | ✅ |
| SyncPlay | `/SyncPlay/List` | ✅ (deferred, see phase 5) |
| Library filters | `/Items/Filters?parentId=…` | ✅ genres, tags, official ratings, years. **Not `Filters2`** — on 10.11.8 that returns only `Genres` and `Tags`, so the ratings/years filter controls have to come from the older endpoint. |
| Paged/sorted browse | `/Items?parentId=…&sortBy=…&startIndex=…&limit=…` | ✅ `TotalRecordCount: 303` movies |
| Genres / Persons | `/Genres`, `/Persons` | ✅ |

---

## C. Feature set

### C1. Browsing (Phase 2b)

- **Home** — Continue Watching, Next Up, Latest per library, plus a "My Media" row of
  library tiles. Rows that resolve empty remove themselves.
- **Library page** — poster grid over `/Items` with:
  - sort: name, date added, date played, release date, community rating, runtime, random
  - filter: genre, year, official rating, played/unplayed, favourites (from `Filters2`)
  - view modes: poster / thumb / detail-list
  - **infinite scroll with windowed loading** — 303 movies must not mean 303 live
    widgets. Recycle `PosterCard`s; keep at most ~3 screens materialised.
- **Detail pages** — movie, series (season selector + episode list), episode, collection.
  Cast strip links to person pages; genre chips link to filtered library views.
- **Person page** — filmography via `/Items?personIds=`.
- **Collections** — `boxsets` view.
- **Search** — 250 ms debounce, grouped results (Movies / Series / Episodes / People),
  recent-search history.
- **Item actions** (context menu + detail page): play, resume, mark played/unplayed
  (`POST/DELETE /UserPlayedItems/{id}`), favourite (`POST/DELETE /UserFavoriteItems/{id}`),
  go to series/season, show media info.

### C2. Playback (Phase 2a core, Phase 3 advanced)

**Core (2a)** — direct play, seek, pause, volume, fullscreen, progress reporting.

**Tracks (3)**
- Audio + subtitle track menus. **On direct play mpv already sees every embedded
  track** — mpv's `track-list` is the source of truth. External Jellyfin subtitles
  (`IsExternal: true`) are added with `sub-add <url> auto <title> <lang>` at load and
  appear in the same menu.
- Preferred audio/subtitle language settings; **remember the choice across episodes**
  of a series (match by language + codec + forced/SDH flags, not by index — indices
  differ between files).
- Secondary subtitle track (`secondary-sid`).

**Subtitle presentation (3)** — size, colour, outline/shadow, background box, font,
position offset, delay (`sub-delay`, ±100 ms steps), ASS override toggle
(`sub-ass-override`). Persisted globally.

**Audio (3)** — delay (`audio-delay`), device selection (`audio-device` from
`audio-device-list`), exclusive/passthrough toggle (`audio-spdif` for AC3/DTS/E-AC3),
volume normalisation (`af=dynaudnorm`), channel downmix.

**Navigation (3)**
- **Chapters** — markers drawn on the seek bar, chapter list menu, prev/next chapter.
- **Trickplay** — hover the seek bar → thumbnail preview. Fetch the tile sheet for the
  hovered index (`floor(ms / Interval)`), crop with
  `row = (i / TileWidth) % TileHeight`, `col = i % TileWidth`. **Cache decoded sheets**
  (each is ~800 KB) — cap at ~8 in memory, drop on stop. Fall back to chapter images,
  then to nothing.
- **Skip intro / skip credits** — `/MediaSegments/{itemId}`; show a corner button while
  inside an `Intro`/`Outro` segment, with an auto-skip setting.
- **Next episode** — countdown card over the final 20 s (from `Outro` segment when
  present, else last 30 s), "Play now" / "Cancel", auto-play-next setting.

**Video (3)** — playback speed 0.25–4×, aspect/zoom/pan, rotate, deinterlace,
hwdec selection (`auto-safe` / `no` / `videotoolbox` / `vaapi` / `nvdec`), screenshot
(`screenshot-to-file`), frame step.

**Transcode control (3)** — quality selector (max bitrate ladder: 120/40/20/10/6/3/1.5 Mbps
and "Auto"), forcing transcode when direct play fails, showing *why* the server chose
to transcode (`TranscodeReasons`).

**System integration (4)** — screensaver/sleep inhibition while playing, media keys and
"Now Playing" on macOS, **MPRIS** on Linux (`org.mpris.MediaPlayer2`), refresh-rate
matching where the platform allows, remember window geometry and fullscreen state.

### C3. Debug / stats overlay (Phase 3)

Toggle with `I` (mpv convention) or `Ctrl+Shift+D` — note that Qt maps
`Qt::ControlModifier` to **Command** on macOS, so that binding is Cmd+Shift+D
there, which is the right platform convention. Same for the Ctrl+Shift+C
"copy diagnostics" binding. A translucent panel over the video,
palette-derived, monospace, updating at 2 Hz — never per-frame.

**Video** — codec + profile, decoder in use, **hwdec active** (`hwdec-current`),
resolution → display resolution, pixel format, FPS (container `container-fps` vs
estimated `estimated-vf-fps`), video bitrate, colour space / primaries / transfer,
HDR metadata (`video-params/sig-peak`).

**Audio** — codec, channel layout, sample rate, bitrate, current `audio-device`,
passthrough active or not.

**Performance** — dropped frames (`frame-drop-count`, `decoder-frame-drop-count`),
A/V sync (`avsync`), `vo-delayed-frame-count`, frame timing
(`render-time-avg` / `-peak`), display FPS vs video FPS.

**Network / cache** — `demuxer-cache-duration`, `demuxer-cache-state` (bytes, ranges),
`cache-speed`, seekable ranges, download rate.

**Jellyfin** — direct play vs transcode **and `TranscodeReasons`**, `PlaySessionId`,
`MediaSourceId`, container, file size, total bitrate, server name + version,
current transcode throttle position.

**Log tail** — last 20 mpv log lines at the configured `msg-level`, plus a
"copy diagnostics to clipboard" button producing a paste-ready report. This is the
single most useful thing for the user when a file misbehaves, so it must be one
keystroke away and copyable.

### C4. Session & server (Phase 4)

- **Server select** — recent servers list, per-server saved users, connection probe
  showing name + version, http→https normalisation.
- **Login** — password, plus **Quick Connect**: `GET /QuickConnect/Initiate` → show the
  6-digit code → poll `GET /QuickConnect/Connect?secret=` → on `Authenticated`,
  `POST /Users/AuthenticateWithQuickConnect {Secret}`. Poll at 2 s, give up after 5 min.
- **Multi-user / multi-server** switching without re-entering credentials.
- **WebSocket** (`/socket?api_key=…&deviceId=…`) — keepalive plus inbound remote-control
  commands. With `POST /Sessions/Capabilities/Full` this makes cutefin controllable
  from the Jellyfin phone app: `Play`, `PlayState` (pause/seek/stop), `SetAudioStreamIndex`,
  `SetSubtitleStreamIndex`, `DisplayMessage`, volume. Reconnect with exponential backoff.
- **Logout** — `POST /Sessions/Logout`, clear stored token, return to login.

### C5. Settings (Phase 4)

A proper preferences window (`QDialog`, native `QTabWidget`; on macOS a toolbar-style
preferences window):

- **Appearance** — follow system theme, poster size, **reduce motion** (disables all
  animations; must also honour the platform's own reduce-motion setting).
- **Playback** — preferred languages, auto-play next, auto-skip intro/credits, hwdec,
  max bitrate, resume behaviour.
- **Subtitles** — all of C2's presentation options with a live preview.
- **Audio** — device, passthrough, normalisation.
- **Network** — request timeout, transcode limits, TLS-certificate-trust prompt for
  self-signed servers (explicit, per-server opt-in — never blanket-disable verification).
- **Advanced** — **custom mpv options** (a free-text `mpv.conf`-style box applied via
  `mpv_set_option_string`, invalid keys reported not swallowed), cache size + clear,
  log level, "copy diagnostics".

### C6. Cross-cutting

- **Keyboard-first**: every screen fully navigable, visible focus rings, `/` focuses
  search, `Esc` goes back, `Space` plays the focused item.
- **Accessibility**: `QAccessible` names on custom widgets, honour system font scaling,
  never encode meaning in colour alone.
- **Error handling**: inline non-modal errors with a retry affordance; a toast for
  background failures; offline detection with a banner. Never a silent failure.
- **Empty states**: every list has a designed empty state.

---

## D. Phasing

| Phase | Content | Status |
|---|---|---|
| 1 | Build system, core, API, tests | done |
| 2a | mpv player core | done |
| 2b | UI shell, home, detail, search | done |
| 2c | Library page (paged grid, sort/filter), person pages, collections | done |
| 3a | Player advanced: tracks, external subs, chapters, trickplay, speed, stats overlay | done |
| 3b | Skip intro/credits, next-episode countdown, subtitle/audio presentation, remembered track choices | next |
| 4 | Settings, WebSocket remote control, Quick Connect, MPRIS/media keys, multi-server | |
| 5 | README, packaging (AppImage/Flatpak, .app bundle), live test target, polish | |

Deferred, explicitly out of scope for now: SyncPlay, Live TV/DVR, music playback and
mixed-audio libraries beyond basic browsing, offline downloads, admin/server management.
Each is a coherent later addition; none blocks the above.

---

## E. Performance budget

Non-negotiable, because "fully featured" must not mean "heavy":

- Idle RAM after browsing 300+ items: **< 250 MB RSS**
- Scrolling the library grid: **60 fps**, no frame over 16 ms on the GUI thread
- Poster grid: at most ~3 screens of live widgets, recycled thereafter
- Trickplay sheets: ≤ 8 cached, freed on playback stop
- Stats overlay: 2 Hz property polling, zero cost when hidden
- No network or disk I/O on the GUI thread, ever
