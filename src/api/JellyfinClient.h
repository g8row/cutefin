#pragma once

#include "Models.h"
#include "core/NetworkQueue.h"

#include <QJsonDocument>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

// Talks to a single Jellyfin server. Every call is asynchronous: it either
// takes a context QObject + callback (auto-cancelled/never-invoked-after-
// destruction, same contract as NetworkQueue) or, for the pure playback
// resolution logic, is a plain static function with no I/O at all.
//
// All requests go through the shared NetworkQueue so metadata and image
// traffic share the same concurrency cap without starving each other.
class JellyfinClient : public QObject
{
    Q_OBJECT
public:
    explicit JellyfinClient(NetworkQueue *networkQueue, QObject *parent = nullptr);

    // --- Identity used to build the Authorization header on every request.
    void setDeviceId(const QString &id);
    void setDeviceName(const QString &name);
    void setClientVersion(const QString &version);

    // Base URL, e.g. "https://jellyfin.example.com" (no trailing slash).
    // Callers are responsible for normalizing user input before this point.
    void setServerUrl(const QString &url);
    QString serverUrl() const;

    void setSession(const UserSession &session);
    void clearSession();
    QString accessToken() const;
    QString userId() const;

    // --- Probe / auth ---
    using ServerInfoCallback = std::function<void(bool ok, const ServerInfo &info, const QString &error)>;
    void probeServer(const QString &url, QObject *context, const ServerInfoCallback &callback);

    using LoginCallback = std::function<void(bool ok, const UserSession &session, const QString &error)>;
    void login(const QString &username, const QString &password, QObject *context, const LoginCallback &callback);

    void logout(QObject *context, const std::function<void()> &callback = {});

    // --- Library ---
    using ItemListCallback = std::function<void(bool ok, const QList<MediaItem> &items, const QString &error)>;
    void fetchUserViews(QObject *context, const ItemListCallback &callback);
    void fetchResumeItems(QObject *context, const ItemListCallback &callback);
    void fetchNextUp(QObject *context, const ItemListCallback &callback);
    void fetchLatestItems(const QString &parentId, QObject *context, const ItemListCallback &callback);
    void fetchSeasons(const QString &seriesId, QObject *context, const ItemListCallback &callback);
    void fetchEpisodes(const QString &seriesId, const QString &seasonId, QObject *context, const ItemListCallback &callback);
    void searchItems(const QString &query, QObject *context, const ItemListCallback &callback);

    using ItemCallback = std::function<void(bool ok, const MediaItem &item, const QString &error)>;
    void fetchItem(const QString &itemId, QObject *context, const ItemCallback &callback);

    // --- User item state ---
    // Jellyfin models these as set/unset on one path: POST marks, DELETE
    // clears. Both verified to return 200 on 10.11.8. Callers normally update
    // their own copy of UserData optimistically and use the callback only to
    // roll back on failure.
    using ResultCallback = std::function<void(bool ok, const QString &error)>;
    void setPlayed(const QString &itemId, bool played, QObject *context, const ResultCallback &callback = {});
    void setFavorite(const QString &itemId, bool favorite, QObject *context, const ResultCallback &callback = {});

    // --- Images ---
    // type: "Primary", "Backdrop", "Thumb", "Logo".
    QUrl imageUrl(const QString &itemId, const QString &type, const QString &tag, int maxWidth) const;

    // --- Playback resolution ---
    struct PlaybackResolution {
        bool ok = false;
        QString playSessionId;
        QString mediaSourceId;
        QUrl url;
        bool isTranscoding = false;
    };

    using PlaybackInfoCallback = std::function<void(const PlaybackResolution &resolution, const QString &error)>;
    void getPlaybackInfo(const QString &itemId, QObject *context, const PlaybackInfoCallback &callback);

    // Pure decision logic, exposed for unit testing without a network round
    // trip: pick the first source that can direct-play/direct-stream, else
    // fall back to the first with a transcoding URL. `ok` is false if
    // neither is available.
    static PlaybackResolution resolvePlayback(const QString &playSessionId, const QList<MediaSource> &sources,
                                               const QString &serverUrl, const QString &accessToken);

    // --- Progress reporting (ticks are 100ns units) ---
    void reportPlaybackStart(const QString &itemId, const QString &playSessionId, const QString &mediaSourceId,
                              qint64 positionTicks);
    void reportPlaybackProgress(const QString &itemId, const QString &playSessionId, const QString &mediaSourceId,
                                 qint64 positionTicks, bool isPaused);
    void reportPlaybackStopped(const QString &itemId, const QString &playSessionId, const QString &mediaSourceId,
                                qint64 positionTicks);

private:
    enum class AuthMode {
        None,      // no Authorization header at all (e.g. System/Info/Public)
        Anonymous, // Authorization header with client identity, no Token= (login)
        Authorized // Authorization header including Token=
    };

    using JsonCallback = std::function<void(bool ok, const QJsonDocument &doc, const QString &error)>;

    QUrl endpointUrl(const QString &path) const;
    QNetworkRequest buildRequest(const QString &path, AuthMode mode) const;
    QString authorizationHeaderValue(bool includeToken) const;

    void getJson(const QString &path, NetworkQueue::Priority priority, AuthMode mode, QObject *context,
                 const JsonCallback &callback);
    void postJson(const QString &path, const QJsonObject &body, NetworkQueue::Priority priority, AuthMode mode,
                  QObject *context, const JsonCallback &callback);
    void postFireAndForget(const QString &path, const QJsonObject &body);

    // POST when `set`, DELETE when clearing -- the shape every user-item
    // state toggle in the API uses.
    void toggleUserItemState(const QString &path, bool set, QObject *context, const ResultCallback &callback);

    void fetchItemList(const QString &path, QObject *context, const ItemListCallback &callback);

    static QJsonObject deviceProfile();

    NetworkQueue *m_networkQueue;
    QString m_serverUrl;
    QString m_deviceId;
    QString m_deviceName;
    QString m_clientVersion = QStringLiteral("0.1.0");
    QString m_userId;
    QString m_accessToken;
};
