#pragma once

#include "TrackSelection.h"

#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>

// Thin wrapper around QSettings (native backend: plist on macOS, ini on
// Linux) holding everything cutefin needs to persist: the stable device id,
// recently used servers, and the logged-in session.
//
// "Remember me" semantics: saveSession() always keeps the session in memory
// for the running process, but only *persists* the access token to disk when
// `rememberMe` is true. A session saved with `rememberMe == false` is gone
// the next time the app starts (hasPersistedSession() returns false), so the
// user is sent back to LoginPage.
class Settings : public QObject
{
    Q_OBJECT
public:
    // Normal app usage: native QSettings backend scoped to org/app "cutefin".
    explicit Settings(QObject *parent = nullptr);

    // Test usage: an explicit ini file so tests don't touch the real user
    // preferences and can be run in isolation / cleaned up afterwards.
    explicit Settings(const QString &iniFilePath, QObject *parent = nullptr);

    // Stable device identifier (persisted QUuid), generated once and reused
    // across runs. Jellyfin ties playback sessions to this value.
    QString deviceId() const;

    // --- Server ---
    QString serverUrl() const;
    void setServerUrl(const QString &url);

    QStringList recentServers() const;
    void addRecentServer(const QString &url);

    // --- Session ---
    struct Session {
        QString serverUrl;
        QString userId;
        QString userName;
        QString token;
        QString deviceId;

        bool isValid() const { return !serverUrl.isEmpty() && !userId.isEmpty() && !token.isEmpty(); }
    };

    // Stores the session for the current process. Only writes the token to
    // disk when `rememberMe` is true; userId/userName/serverUrl are always
    // persisted so the UI can prefill (e.g. showing a "log back in" screen),
    // but without a persisted token, hasPersistedSession() is false.
    void saveSession(const Session &session, bool rememberMe);

    // The in-memory session for the current run (valid immediately after
    // saveSession(), regardless of rememberMe).
    Session currentSession() const;

    // What was found on disk at startup, if anything. Call once at launch.
    Session loadPersistedSession() const;
    bool hasPersistedSession() const;

    // Clears both the in-memory and the persisted session (logout).
    void clearSession();

    // --- Misc ---
    int playerVolume() const;
    void setPlayerVolume(int volume);

    QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray &geometry);

    // --- Playback navigation (Phase 3b, FEATURES.md §C2/§C5) ---
    // These are the two stored preferences; Phase 4 owns the settings
    // window UI that lets a user actually flip them.

    // Silently seeks past an Intro/Outro segment instead of just showing
    // the skip button. Off by default: an unannounced seek is more
    // surprising than a button the user can ignore, and unlike
    // auto-play-next there is no confirmation step to soften it.
    bool autoSkipSegments() const;
    void setAutoSkipSegments(bool enabled);

    // Starts the next episode automatically once the next-episode countdown
    // reaches zero. On by default: every reference client in FEATURES.md §A
    // (Jellyfin Media Player, Findroid, ...) defaults this on, and it is
    // safe to default on here because the countdown card is always shown
    // first with a Cancel button -- the action is never silent the way
    // auto-skip's seek is.
    bool autoPlayNextEpisode() const;
    void setAutoPlayNextEpisode(bool enabled);

    // --- Subtitle presentation (Phase 3b, FEATURES.md §C2/§C5) ---
    // Global, not per-series or per-file: unlike the remembered *track*
    // choices below, these are "how subtitles look", which every reference
    // client in FEATURES.md §A treats as a one-time preference, set once and
    // applied to whatever plays next. Colors are stored as "#AARRGGBB" hex
    // strings (QColor::name(QColor::HexArgb)) -- the exact format
    // --sub-color/--sub-back-color/--sub-outline-color accept directly, so
    // no translation is needed at the mpv boundary (DESIGN.md §7 already
    // routes all such options through MpvWidget::setPropertyChecked()).
    //
    // FEATURES.md §C6 carves out one deliberate exception to "palette colors
    // only": the user explicitly choosing a subtitle color is a real color
    // choice, not app chrome, so these are free-form and not palette-derived.
    int subtitleFontSize() const;
    void setSubtitleFontSize(int points);

    QString subtitleFontFamily() const; // empty = mpv's own default (embedded ASS font or fallback)
    void setSubtitleFontFamily(const QString &family);

    QString subtitleColor() const; // "#AARRGGBB"
    void setSubtitleColor(const QString &argbHex);

    QString subtitleOutlineColor() const; // "#AARRGGBB"
    void setSubtitleOutlineColor(const QString &argbHex);

    double subtitleOutlineSize() const; // --sub-outline-size, mpv default 1.65
    void setSubtitleOutlineSize(double size);

    double subtitleShadowOffset() const; // --sub-shadow-offset
    void setSubtitleShadowOffset(double offset);

    // Toggles --sub-border-style between "background-box" (on) and mpv's own
    // default "outline-and-shadow" (off) -- see MpvWidget/PlayerWindow for
    // why this is a style enum rather than a plain on/off property.
    bool subtitleBackgroundBoxEnabled() const;
    void setSubtitleBackgroundBoxEnabled(bool enabled);

    QString subtitleBackgroundColor() const; // "#AARRGGBB", feeds --sub-back-color
    void setSubtitleBackgroundColor(const QString &argbHex);

    int subtitlePositionPercent() const; // --sub-pos, 0-150, mpv default 100 (bottom)
    void setSubtitlePositionPercent(int percent);

    // --sub-delay. FEATURES.md calls out "±100ms steps" for the in-player
    // keybinding that adjusts this, not for the value's own granularity --
    // the stored value is a plain double in seconds.
    double subtitleDelaySeconds() const;
    void setSubtitleDelaySeconds(double seconds);

    // --sub-ass-override toggle. false -> "scale" (mpv's own default:
    // respect an ASS subtitle's authored style, just apply --sub-scale on
    // top), true -> "force" (apply every --sub-* override in this list even
    // to ASS-styled subtitles). See MpvWidget.cpp for why a plain bool maps
    // onto mpv's 5-way enum this way.
    bool subtitleAssOverride() const;
    void setSubtitleAssOverride(bool enabled);

    // --- Audio (Phase 3b, FEATURES.md §C2/§C5) ---
    // Also global for the same reason as the subtitle options above.
    double audioDelaySeconds() const; // --audio-delay
    void setAudioDelaySeconds(double seconds);

    QString audioDevice() const; // --audio-device value, e.g. "auto" or a driver/id string from audio-device-list
    void setAudioDevice(const QString &device);

    // Whether AC3/DTS/E-AC3 passthrough (--audio-spdif) should be requested
    // when the current track's codec supports it -- see
    // TrackSelection::spdifOptionValue()/canPassthrough() for why this is
    // conditional rather than a blanket switch.
    bool audioPassthroughEnabled() const;
    void setAudioPassthroughEnabled(bool enabled);

    // Volume normalization via the dynaudnorm audio filter (FEATURES.md's
    // "volume normalisation").
    bool audioNormalizationEnabled() const;
    void setAudioNormalizationEnabled(bool enabled);

    // --audio-channels value: "auto" (mpv default, no forced downmix),
    // "stereo", "5.1", "7.1", ...
    QString audioChannelLayout() const;
    void setAudioChannelLayout(const QString &layout);

    // --- Preferred languages (Phase 3b, FEATURES.md §C2) ---
    // The fallback tier in TrackSelection::pickTrack() when no remembered
    // per-series choice matches (or none exists yet). ISO-639-ish language
    // codes, matched exactly as mpv/Jellyfin report them; empty means "no
    // preference, leave mpv's default alone".
    QString preferredAudioLanguage() const;
    void setPreferredAudioLanguage(const QString &language);

    QString preferredSubtitleLanguage() const;
    void setPreferredSubtitleLanguage(const QString &language);

    // --- Remembered track choices, per series (Phase 3b, FEATURES.md §C2) ---
    // Unlike everything above, these are keyed by seriesId and have a
    // different lifetime: a presentation option is "how I like things to
    // look/sound", set once for the whole app; a remembered track choice is
    // "which language/edition of *this show* I watch", which must follow the
    // series from S1E1 to S1E2 but should not leak into an unrelated show
    // that happens to also have a Japanese dub. A movie (no seriesId) simply
    // never calls these -- PlayerWindow only does for item.type == "Episode".
    //
    // Storage is intentionally by attribute, not by mpv track id/index (see
    // TrackSelection.h's header comment for why); the accessor returns
    // RememberedChoice::hasChoice == false when nothing has been picked yet
    // for that series and kind, which pickTrack() treats as "fall through to
    // the preferred-language setting".
    TrackSelection::RememberedChoice rememberedAudioChoice(const QString &seriesId) const;
    void setRememberedAudioChoice(const QString &seriesId, const TrackSelection::RememberedChoice &choice);

    TrackSelection::RememberedChoice rememberedSubtitleChoice(const QString &seriesId) const;
    void setRememberedSubtitleChoice(const QString &seriesId, const TrackSelection::RememberedChoice &choice);

    // The secondary subtitle track (mpv's secondary-sid, FEATURES.md §C2)
    // gets its own remembered slot: a user running dual subtitles (e.g.
    // target-language + native-language) picks the two independently, and
    // they are not required to be "the same choice as primary, minus one".
    TrackSelection::RememberedChoice rememberedSecondarySubtitleChoice(const QString &seriesId) const;
    void setRememberedSecondarySubtitleChoice(const QString &seriesId, const TrackSelection::RememberedChoice &choice);

    // --- Appearance & Performance (Qt Quick UI + Dual-Core mode) ---
    QString themeMode() const; // "dark" (default), "light", "system"
    void setThemeMode(const QString &mode);

    QString accentColor() const; // "#AA5CC3" default (Jellyfin purple)
    void setAccentColor(const QString &hex);

    bool reduceMotion() const;
    void setReduceMotion(bool enabled);

    // Auto-defaults to true on <= 2 CPU cores when not explicitly configured.
    bool dualCoreMode() const;
    void setDualCoreMode(bool enabled);

    // --- Video / Transcode / Advanced ---
    QString hwdecMode() const; // "auto-safe", "no", "videotoolbox", "vaapi", "nvdec", "d3d11va"
    void setHwdecMode(const QString &mode);

    QString videoProfile() const; // "fast", "balanced", "high-quality"
    void setVideoProfile(const QString &profile);

    int maxBitrateMbps() const; // 0 = Auto (Direct Play)
    void setMaxBitrateMbps(int mbps);

    QString customMpvOptions() const;
    void setCustomMpvOptions(const QString &options);

    QString mpvLogLevel() const;
    void setMpvLogLevel(const QString &level);

    // --- Recent searches & QML window geometry ---
    QStringList recentSearches() const;
    void addRecentSearch(const QString &term);
    void clearRecentSearches();

    int windowWidth() const;
    int windowHeight() const;
    bool windowMaximized() const;
    void setWindowState(int width, int height, bool maximized);

private:
    void init();
    QString ensureDeviceId();

    // Shared implementation for the three rememberedXChoice()/setRememberedXChoice()
    // pairs above -- `kind` is a fixed literal ("audio"/"subtitle"/"secondarySubtitle")
    // folded into the QSettings key alongside seriesId.
    TrackSelection::RememberedChoice loadRememberedChoice(const QString &seriesId, const char *kind) const;
    void saveRememberedChoice(const QString &seriesId, const char *kind, const TrackSelection::RememberedChoice &choice);

    QSettings m_settings;
    Session m_currentSession;
};
