#pragma once

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

private:
    void init();
    QString ensureDeviceId();

    QSettings m_settings;
    Session m_currentSession;
};
