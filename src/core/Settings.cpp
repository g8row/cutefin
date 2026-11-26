#include "Settings.h"

#include <QThread>
#include <QUuid>

namespace {
constexpr const char *kOrganization = "cutefin";
constexpr const char *kApplication = "cutefin";

constexpr const char *kKeyDeviceId = "device/id";
constexpr const char *kKeyServerUrl = "server/url";
constexpr const char *kKeyRecentServers = "server/recent";
constexpr const char *kKeyAuthUserId = "auth/userId";
constexpr const char *kKeyAuthUserName = "auth/userName";
constexpr const char *kKeyAuthToken = "auth/token";
constexpr const char *kKeyPlayerVolume = "player/volume";
constexpr const char *kKeyWindowGeometry = "window/geometry";
constexpr const char *kKeyAutoSkipSegments = "player/autoSkipSegments";
constexpr const char *kKeyAutoPlayNextEpisode = "player/autoPlayNextEpisode";

constexpr const char *kKeySubtitleFontSize = "subtitle/fontSize";
constexpr const char *kKeySubtitleFontFamily = "subtitle/fontFamily";
constexpr const char *kKeySubtitleColor = "subtitle/color";
constexpr const char *kKeySubtitleOutlineColor = "subtitle/outlineColor";
constexpr const char *kKeySubtitleOutlineSize = "subtitle/outlineSize";
constexpr const char *kKeySubtitleShadowOffset = "subtitle/shadowOffset";
constexpr const char *kKeySubtitleBackgroundBoxEnabled = "subtitle/backgroundBoxEnabled";
constexpr const char *kKeySubtitleBackgroundColor = "subtitle/backgroundColor";
constexpr const char *kKeySubtitlePositionPercent = "subtitle/positionPercent";
constexpr const char *kKeySubtitleDelaySeconds = "subtitle/delaySeconds";
constexpr const char *kKeySubtitleAssOverride = "subtitle/assOverride";

constexpr const char *kKeyAudioDelaySeconds = "audio/delaySeconds";
constexpr const char *kKeyAudioDevice = "audio/device";
constexpr const char *kKeyAudioPassthroughEnabled = "audio/passthroughEnabled";
constexpr const char *kKeyAudioNormalizationEnabled = "audio/normalizationEnabled";
constexpr const char *kKeyAudioChannelLayout = "audio/channelLayout";

constexpr const char *kKeyPreferredAudioLanguage = "player/preferredAudioLanguage";
constexpr const char *kKeyPreferredSubtitleLanguage = "player/preferredSubtitleLanguage";

constexpr int kDefaultVolume = 100;
constexpr int kMaxRecentServers = 8;
constexpr bool kDefaultAutoSkipSegments = false;
constexpr bool kDefaultAutoPlayNextEpisode = true;

// mpv defaults (DESIGN.md/FEATURES.md §C2): match these exactly so a user
// who never touches the subtitle/audio settings sees the same rendering
// libmpv would give them standalone -- an app-invented default that differs
// from mpv's own would be a surprise the first time someone compares.
constexpr int kDefaultSubtitleFontSize = 55;
constexpr double kDefaultSubtitleOutlineSize = 1.65;
constexpr double kDefaultSubtitleShadowOffset = 0.0;
constexpr int kDefaultSubtitlePositionPercent = 100;
constexpr double kDefaultSubtitleDelaySeconds = 0.0;
constexpr bool kDefaultSubtitleAssOverride = false;
constexpr double kDefaultAudioDelaySeconds = 0.0;
constexpr bool kDefaultAudioPassthroughEnabled = false;
constexpr bool kDefaultAudioNormalizationEnabled = false;

// "#AARRGGBB": opaque white text, opaque black outline, half-transparent
// black background box -- the same look mpv/libass render by default.
constexpr const char *kDefaultSubtitleColor = "#FFFFFFFF";
constexpr const char *kDefaultSubtitleOutlineColor = "#FF000000";
constexpr const char *kDefaultSubtitleBackgroundColor = "#80000000";
constexpr const char *kDefaultAudioDevice = "auto";
constexpr const char *kDefaultAudioChannelLayout = "auto";
}

Settings::Settings(QObject *parent)
    : QObject(parent)
    , m_settings(QString::fromLatin1(kOrganization), QString::fromLatin1(kApplication))
{
    init();
}

Settings::Settings(const QString &iniFilePath, QObject *parent)
    : QObject(parent)
    , m_settings(iniFilePath, QSettings::IniFormat)
{
    init();
}

void Settings::init()
{
    ensureDeviceId();
    m_currentSession = loadPersistedSession();
}

QString Settings::ensureDeviceId()
{
    QString id = m_settings.value(QString::fromLatin1(kKeyDeviceId)).toString();
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_settings.setValue(QString::fromLatin1(kKeyDeviceId), id);
    }
    return id;
}

QString Settings::deviceId() const
{
    return m_settings.value(QString::fromLatin1(kKeyDeviceId)).toString();
}

QString Settings::serverUrl() const
{
    return m_settings.value(QString::fromLatin1(kKeyServerUrl)).toString();
}

void Settings::setServerUrl(const QString &url)
{
    m_settings.setValue(QString::fromLatin1(kKeyServerUrl), url);
}

QStringList Settings::recentServers() const
{
    return m_settings.value(QString::fromLatin1(kKeyRecentServers)).toStringList();
}

void Settings::addRecentServer(const QString &url)
{
    QStringList servers = recentServers();
    servers.removeAll(url);
    servers.prepend(url);
    while (servers.size() > kMaxRecentServers)
        servers.removeLast();
    m_settings.setValue(QString::fromLatin1(kKeyRecentServers), servers);
}

void Settings::saveSession(const Session &session, bool rememberMe)
{
    m_currentSession = session;

    setServerUrl(session.serverUrl);
    m_settings.setValue(QString::fromLatin1(kKeyAuthUserId), session.userId);
    m_settings.setValue(QString::fromLatin1(kKeyAuthUserName), session.userName);

    if (rememberMe)
        m_settings.setValue(QString::fromLatin1(kKeyAuthToken), session.token);
    else
        m_settings.remove(QString::fromLatin1(kKeyAuthToken));
}

Settings::Session Settings::currentSession() const
{
    return m_currentSession;
}

Settings::Session Settings::loadPersistedSession() const
{
    Session session;
    session.serverUrl = serverUrl();
    session.userId = m_settings.value(QString::fromLatin1(kKeyAuthUserId)).toString();
    session.userName = m_settings.value(QString::fromLatin1(kKeyAuthUserName)).toString();
    session.token = m_settings.value(QString::fromLatin1(kKeyAuthToken)).toString();
    session.deviceId = deviceId();
    return session;
}

bool Settings::hasPersistedSession() const
{
    return loadPersistedSession().isValid();
}

void Settings::clearSession()
{
    m_currentSession = Session();
    m_settings.remove(QString::fromLatin1(kKeyAuthUserId));
    m_settings.remove(QString::fromLatin1(kKeyAuthUserName));
    m_settings.remove(QString::fromLatin1(kKeyAuthToken));
}

int Settings::playerVolume() const
{
    return m_settings.value(QString::fromLatin1(kKeyPlayerVolume), kDefaultVolume).toInt();
}

void Settings::setPlayerVolume(int volume)
{
    m_settings.setValue(QString::fromLatin1(kKeyPlayerVolume), volume);
}

QByteArray Settings::windowGeometry() const
{
    return m_settings.value(QString::fromLatin1(kKeyWindowGeometry)).toByteArray();
}

void Settings::setWindowGeometry(const QByteArray &geometry)
{
    m_settings.setValue(QString::fromLatin1(kKeyWindowGeometry), geometry);
}

bool Settings::autoSkipSegments() const
{
    return m_settings.value(QString::fromLatin1(kKeyAutoSkipSegments), kDefaultAutoSkipSegments).toBool();
}

void Settings::setAutoSkipSegments(bool enabled)
{
    m_settings.setValue(QString::fromLatin1(kKeyAutoSkipSegments), enabled);
}

bool Settings::autoPlayNextEpisode() const
{
    return m_settings.value(QString::fromLatin1(kKeyAutoPlayNextEpisode), kDefaultAutoPlayNextEpisode).toBool();
}

void Settings::setAutoPlayNextEpisode(bool enabled)
{
    m_settings.setValue(QString::fromLatin1(kKeyAutoPlayNextEpisode), enabled);
}

int Settings::subtitleFontSize() const
{
    return m_settings.value(QString::fromLatin1(kKeySubtitleFontSize), kDefaultSubtitleFontSize).toInt();
}

void Settings::setSubtitleFontSize(int points)
{
    m_settings.setValue(QString::fromLatin1(kKeySubtitleFontSize), points);
}

QString Settings::subtitleFontFamily() const
{
    return m_settings.value(QString::fromLatin1(kKeySubtitleFontFamily)).toString();
}

void Settings::setSubtitleFontFamily(const QString &family)
{
    m_settings.setValue(QString::fromLatin1(kKeySubtitleFontFamily), family);
}

QString Settings::subtitleColor() const
{
    return m_settings.value(QString::fromLatin1(kKeySubtitleColor), QString::fromLatin1(kDefaultSubtitleColor))
        .toString();
}

void Settings::setSubtitleColor(const QString &argbHex)
{
    m_settings.setValue(QString::fromLatin1(kKeySubtitleColor), argbHex);
}

QString Settings::subtitleOutlineColor() const
{
    return m_settings
        .value(QString::fromLatin1(kKeySubtitleOutlineColor), QString::fromLatin1(kDefaultSubtitleOutlineColor))
        .toString();
}

void Settings::setSubtitleOutlineColor(const QString &argbHex)
{
    m_settings.setValue(QString::fromLatin1(kKeySubtitleOutlineColor), argbHex);
}

double Settings::subtitleOutlineSize() const
{
    return m_settings.value(QString::fromLatin1(kKeySubtitleOutlineSize), kDefaultSubtitleOutlineSize).toDouble();
}

void Settings::setSubtitleOutlineSize(double size)
{
    m_settings.setValue(QString::fromLatin1(kKeySubtitleOutlineSize), size);
}

double Settings::subtitleShadowOffset() const
{
    return m_settings.value(QString::fromLatin1(kKeySubtitleShadowOffset), kDefaultSubtitleShadowOffset).toDouble();
}

void Settings::setSubtitleShadowOffset(double offset)
{
    m_settings.setValue(QString::fromLatin1(kKeySubtitleShadowOffset), offset);
}

bool Settings::subtitleBackgroundBoxEnabled() const
{
    return m_settings.value(QString::fromLatin1(kKeySubtitleBackgroundBoxEnabled), false).toBool();
}

void Settings::setSubtitleBackgroundBoxEnabled(bool enabled)
{
    m_settings.setValue(QString::fromLatin1(kKeySubtitleBackgroundBoxEnabled), enabled);
}

QString Settings::subtitleBackgroundColor() const
{
    return m_settings
        .value(QString::fromLatin1(kKeySubtitleBackgroundColor), QString::fromLatin1(kDefaultSubtitleBackgroundColor))
        .toString();
}

void Settings::setSubtitleBackgroundColor(const QString &argbHex)
{
    m_settings.setValue(QString::fromLatin1(kKeySubtitleBackgroundColor), argbHex);
}

int Settings::subtitlePositionPercent() const
{
    return m_settings.value(QString::fromLatin1(kKeySubtitlePositionPercent), kDefaultSubtitlePositionPercent)
        .toInt();
}

void Settings::setSubtitlePositionPercent(int percent)
{
    m_settings.setValue(QString::fromLatin1(kKeySubtitlePositionPercent), percent);
}

double Settings::subtitleDelaySeconds() const
{
    return m_settings.value(QString::fromLatin1(kKeySubtitleDelaySeconds), kDefaultSubtitleDelaySeconds).toDouble();
}

void Settings::setSubtitleDelaySeconds(double seconds)
{
    m_settings.setValue(QString::fromLatin1(kKeySubtitleDelaySeconds), seconds);
}

bool Settings::subtitleAssOverride() const
{
    return m_settings.value(QString::fromLatin1(kKeySubtitleAssOverride), kDefaultSubtitleAssOverride).toBool();
}

void Settings::setSubtitleAssOverride(bool enabled)
{
    m_settings.setValue(QString::fromLatin1(kKeySubtitleAssOverride), enabled);
}

double Settings::audioDelaySeconds() const
{
    return m_settings.value(QString::fromLatin1(kKeyAudioDelaySeconds), kDefaultAudioDelaySeconds).toDouble();
}

void Settings::setAudioDelaySeconds(double seconds)
{
    m_settings.setValue(QString::fromLatin1(kKeyAudioDelaySeconds), seconds);
}

QString Settings::audioDevice() const
{
    return m_settings.value(QString::fromLatin1(kKeyAudioDevice), QString::fromLatin1(kDefaultAudioDevice))
        .toString();
}

void Settings::setAudioDevice(const QString &device)
{
    m_settings.setValue(QString::fromLatin1(kKeyAudioDevice), device);
}

bool Settings::audioPassthroughEnabled() const
{
    return m_settings.value(QString::fromLatin1(kKeyAudioPassthroughEnabled), kDefaultAudioPassthroughEnabled)
        .toBool();
}

void Settings::setAudioPassthroughEnabled(bool enabled)
{
    m_settings.setValue(QString::fromLatin1(kKeyAudioPassthroughEnabled), enabled);
}

bool Settings::audioNormalizationEnabled() const
{
    return m_settings.value(QString::fromLatin1(kKeyAudioNormalizationEnabled), kDefaultAudioNormalizationEnabled)
        .toBool();
}

void Settings::setAudioNormalizationEnabled(bool enabled)
{
    m_settings.setValue(QString::fromLatin1(kKeyAudioNormalizationEnabled), enabled);
}

QString Settings::audioChannelLayout() const
{
    return m_settings.value(QString::fromLatin1(kKeyAudioChannelLayout), QString::fromLatin1(kDefaultAudioChannelLayout))
        .toString();
}

void Settings::setAudioChannelLayout(const QString &layout)
{
    m_settings.setValue(QString::fromLatin1(kKeyAudioChannelLayout), layout);
}

QString Settings::preferredAudioLanguage() const
{
    return m_settings.value(QString::fromLatin1(kKeyPreferredAudioLanguage)).toString();
}

void Settings::setPreferredAudioLanguage(const QString &language)
{
    m_settings.setValue(QString::fromLatin1(kKeyPreferredAudioLanguage), language);
}

QString Settings::preferredSubtitleLanguage() const
{
    return m_settings.value(QString::fromLatin1(kKeyPreferredSubtitleLanguage)).toString();
}

void Settings::setPreferredSubtitleLanguage(const QString &language)
{
    m_settings.setValue(QString::fromLatin1(kKeyPreferredSubtitleLanguage), language);
}

// Stored as a 4-element QStringList under "tracks/<seriesId>/<kind>":
// [language, codec, forced ("1"/"0"), hearingImpaired ("1"/"0")]. Presence of
// the key (not any particular value inside it) is what hasChoice reflects --
// contains() is checked rather than trusting an empty-list default, so a
// choice with an empty language/codec (unusual, but not impossible for a
// stream with no language tag) still round-trips as hasChoice == true.
TrackSelection::RememberedChoice Settings::loadRememberedChoice(const QString &seriesId, const char *kind) const
{
    const QString key = QStringLiteral("tracks/%1/%2").arg(seriesId, QString::fromLatin1(kind));
    TrackSelection::RememberedChoice choice;
    if (!m_settings.contains(key))
        return choice; // hasChoice stays false

    const QStringList parts = m_settings.value(key).toStringList();
    if (parts.size() != 4)
        return choice; // corrupt/foreign value -- treat as "no choice" rather than crash on parts[i]

    choice.language = parts.at(0);
    choice.codec = parts.at(1);
    choice.forced = parts.at(2) == QLatin1String("1");
    choice.hearingImpaired = parts.at(3) == QLatin1String("1");
    choice.hasChoice = true;
    return choice;
}

void Settings::saveRememberedChoice(const QString &seriesId, const char *kind,
                                     const TrackSelection::RememberedChoice &choice)
{
    const QString key = QStringLiteral("tracks/%1/%2").arg(seriesId, QString::fromLatin1(kind));
    if (!choice.hasChoice) {
        m_settings.remove(key); // explicit "forget this series' choice"
        return;
    }
    const QStringList parts = {choice.language, choice.codec, choice.forced ? QStringLiteral("1") : QStringLiteral("0"),
                                choice.hearingImpaired ? QStringLiteral("1") : QStringLiteral("0")};
    m_settings.setValue(key, parts);
}

TrackSelection::RememberedChoice Settings::rememberedAudioChoice(const QString &seriesId) const
{
    return loadRememberedChoice(seriesId, "audio");
}

void Settings::setRememberedAudioChoice(const QString &seriesId, const TrackSelection::RememberedChoice &choice)
{
    saveRememberedChoice(seriesId, "audio", choice);
}

TrackSelection::RememberedChoice Settings::rememberedSubtitleChoice(const QString &seriesId) const
{
    return loadRememberedChoice(seriesId, "subtitle");
}

void Settings::setRememberedSubtitleChoice(const QString &seriesId, const TrackSelection::RememberedChoice &choice)
{
    saveRememberedChoice(seriesId, "subtitle", choice);
}

TrackSelection::RememberedChoice Settings::rememberedSecondarySubtitleChoice(const QString &seriesId) const
{
    return loadRememberedChoice(seriesId, "secondarySubtitle");
}

void Settings::setRememberedSecondarySubtitleChoice(const QString &seriesId,
                                                      const TrackSelection::RememberedChoice &choice)
{
    saveRememberedChoice(seriesId, "secondarySubtitle", choice);
}

QString Settings::themeMode() const
{
    return m_settings.value(QStringLiteral("ui/themeMode"), QStringLiteral("dark")).toString();
}

void Settings::setThemeMode(const QString &mode)
{
    m_settings.setValue(QStringLiteral("ui/themeMode"), mode);
}

QString Settings::accentColor() const
{
    return m_settings.value(QStringLiteral("ui/accentColor"), QStringLiteral("#AA5CC3")).toString();
}

void Settings::setAccentColor(const QString &hex)
{
    m_settings.setValue(QStringLiteral("ui/accentColor"), hex);
}

bool Settings::reduceMotion() const
{
    return m_settings.value(QStringLiteral("ui/reduceMotion"), false).toBool();
}

void Settings::setReduceMotion(bool enabled)
{
    m_settings.setValue(QStringLiteral("ui/reduceMotion"), enabled);
}

bool Settings::dualCoreMode() const
{
    const QString key = QStringLiteral("performance/dualCoreMode");
    if (!m_settings.contains(key))
        return QThread::idealThreadCount() <= 2;
    return m_settings.value(key).toBool();
}

void Settings::setDualCoreMode(bool enabled)
{
    m_settings.setValue(QStringLiteral("performance/dualCoreMode"), enabled);
}

QString Settings::hwdecMode() const
{
    return m_settings.value(QStringLiteral("player/hwdec"), QStringLiteral("auto-safe")).toString();
}

void Settings::setHwdecMode(const QString &mode)
{
    m_settings.setValue(QStringLiteral("player/hwdec"), mode);
}

QString Settings::videoProfile() const
{
    const QString defaultProfile = dualCoreMode() ? QStringLiteral("fast") : QStringLiteral("balanced");
    return m_settings.value(QStringLiteral("player/videoProfile"), defaultProfile).toString();
}

void Settings::setVideoProfile(const QString &profile)
{
    m_settings.setValue(QStringLiteral("player/videoProfile"), profile);
}

int Settings::maxBitrateMbps() const
{
    return m_settings.value(QStringLiteral("player/maxBitrateMbps"), 0).toInt();
}

void Settings::setMaxBitrateMbps(int mbps)
{
    m_settings.setValue(QStringLiteral("player/maxBitrateMbps"), qMax(0, mbps));
}

QString Settings::customMpvOptions() const
{
    return m_settings.value(QStringLiteral("player/customMpvOptions")).toString();
}

void Settings::setCustomMpvOptions(const QString &options)
{
    m_settings.setValue(QStringLiteral("player/customMpvOptions"), options);
}

QString Settings::mpvLogLevel() const
{
    return m_settings.value(QStringLiteral("player/mpvLogLevel"), QStringLiteral("warn")).toString();
}

void Settings::setMpvLogLevel(const QString &level)
{
    m_settings.setValue(QStringLiteral("player/mpvLogLevel"), level);
}

QStringList Settings::recentSearches() const
{
    return m_settings.value(QStringLiteral("ui/recentSearches")).toStringList();
}

void Settings::addRecentSearch(const QString &term)
{
    const QString cleaned = term.trimmed();
    if (cleaned.isEmpty())
        return;
    QStringList list = recentSearches();
    list.removeAll(cleaned);
    list.prepend(cleaned);
    while (list.size() > 10)
        list.removeLast();
    m_settings.setValue(QStringLiteral("ui/recentSearches"), list);
}

void Settings::clearRecentSearches()
{
    m_settings.remove(QStringLiteral("ui/recentSearches"));
}

int Settings::windowWidth() const
{
    return qBound(800, m_settings.value(QStringLiteral("window/width"), 1340).toInt(), 7680);
}

int Settings::windowHeight() const
{
    return qBound(540, m_settings.value(QStringLiteral("window/height"), 840).toInt(), 4320);
}

bool Settings::windowMaximized() const
{
    return m_settings.value(QStringLiteral("window/maximized"), false).toBool();
}

void Settings::setWindowState(int width, int height, bool maximized)
{
    if (!maximized && width >= 640 && height >= 400) {
        m_settings.setValue(QStringLiteral("window/width"), width);
        m_settings.setValue(QStringLiteral("window/height"), height);
    }
    m_settings.setValue(QStringLiteral("window/maximized"), maximized);
}
