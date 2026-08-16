#include "Settings.h"

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

constexpr int kDefaultVolume = 100;
constexpr int kMaxRecentServers = 8;
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
