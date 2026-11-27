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

    // Quick Connect (verified on Jellyfin 10.11.8: GET /QuickConnect/Initiate,
    // GET /QuickConnect/Connect?secret=..., POST /Users/AuthenticateWithQuickConnect).
    using QuickConnectInitCallback =
        std::function<void(bool ok, const QString &secret, const QString &code, const QString &error)>;
    void initiateQuickConnect(QObject *context, const QuickConnectInitCallback &callback);

    using QuickConnectPollCallback = std::function<void(bool ok, bool authenticated, const QString &error)>;
    void pollQuickConnect(const QString &secret, QObject *context, const QuickConnectPollCallback &callback);

    void authenticateWithQuickConnect(const QString &secret, QObject *context, const LoginCallback &callback);

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

    // --- Paged / sorted / filtered browse (library grid) ---
    // Everything the library page varies, in one value type so callers can
    // hold "the current query", mutate one field and refetch.
    struct ItemQuery {
        QString parentId;                // library view id; empty searches everywhere
        QStringList includeItemTypes;    // "Movie", "Series", "Episode", "BoxSet"...
        QString sortBy = QStringLiteral("SortName");
        bool ascending = true;

        QStringList genres;
        QStringList officialRatings;
        QList<int> years;
        QString personId;                // filmography view
        QString searchTerm;
        QString nameStartsWith;

        // Tri-state: unset means "don't filter on played at all".
        enum class Played { Any, PlayedOnly, UnplayedOnly };
        Played played = Played::Any;
        bool favoritesOnly = false;

        int startIndex = 0;
        int limit = 100;
    };

    struct ItemPage {
        QList<MediaItem> items;
        int totalRecordCount = 0;
        int startIndex = 0;
    };

    using ItemPageCallback = std::function<void(bool ok, const ItemPage &page, const QString &error)>;
    void fetchItems(const ItemQuery &query, QObject *context, const ItemPageCallback &callback);

    // Exposed for unit testing: builds the /Items path + query string for a
    // query without performing any I/O.
    QString buildItemsPath(const ItemQuery &query) const;

    // The set of values a library's filter controls should offer.
    struct LibraryFilters {
        QStringList genres;
        QStringList officialRatings;
        QList<int> years;
    };

    using FiltersCallback = std::function<void(bool ok, const LibraryFilters &filters, const QString &error)>;
    // NB: uses /Items/Filters, not /Items/Filters2. On 10.11.8 Filters2
    // returns only Genres and Tags, while the older endpoint is the only one
    // that also reports OfficialRatings and Years -- which the filter UI needs.
    void fetchLibraryFilters(const QString &parentId, const QStringList &includeItemTypes, QObject *context,
                              const FiltersCallback &callback);
    static LibraryFilters parseLibraryFilters(const QJsonObject &obj);

    // Like fetchItem(), but asks for the heavyweight fields the player needs
    // (Chapters, Trickplay, MediaSources/MediaStreams). Kept separate so
    // browsing never pays for them.
    void fetchItemDetails(const QString &itemId, QObject *context, const ItemCallback &callback);

    // The media sources (and therefore the audio/subtitle stream list) for an
    // item. Used to add Jellyfin's external subtitles to mpv, which cannot
    // see them on its own.
    using MediaSourcesCallback =
        std::function<void(bool ok, const QList<MediaSource> &sources, const QString &error)>;
    void fetchMediaSources(const QString &itemId, QObject *context, const MediaSourcesCallback &callback);

    // Intro/outro regions. An empty list is the normal result on a server
    // with no intro-detection plugin -- not an error.
    using MediaSegmentsCallback =
        std::function<void(bool ok, const QList<MediaSegment> &segments, const QString &error)>;
    void fetchMediaSegments(const QString &itemId, QObject *context, const MediaSegmentsCallback &callback);

    // One trickplay tile sheet.
    //
    // `thumbnailWidth` is the *pixel* width of a single thumbnail (320 on
    // typical servers) -- i.e. TrickplayInfo::width, NOT TrickplayInfo::tileWidth
    // (grid column count). `sheetIndex` is thumbnailIndex / (tileWidth * tileHeight).
    // Callers must fetch this URL using authorizedRequest() rather than ?api_key=.
    QUrl trickplayTileUrl(const QString &itemId, int thumbnailWidth, int sheetIndex) const;

    // Builds a QNetworkRequest carrying the current session's MediaBrowser
    // Authorization header (required by Jellyfin 12+ endpoints such as
    // /Videos/{id}/Trickplay/..., which reject ?api_key= query auth).
    QNetworkRequest authorizedRequest(const QUrl &url) const;

    // A downloadable external subtitle stream, for `sub-add` in mpv.
    QUrl subtitleStreamUrl(const QString &itemId, const QString &mediaSourceId, int streamIndex,
                            const QString &format = QStringLiteral("srt")) const;

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
        QString container;
        qint64 size = 0;
        qint64 bitRate = 0;
        QStringList transcodeReasons;
    };

    using PlaybackInfoCallback = std::function<void(const PlaybackResolution &resolution, const QString &error)>;
    void getPlaybackInfo(const QString &itemId, QObject *context, const PlaybackInfoCallback &callback);
    void getPlaybackInfo(const QString &itemId, qint64 maxBitrateBps, bool forceTranscode, QObject *context,
                         const PlaybackInfoCallback &callback);

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

    static QJsonObject deviceProfile(qint64 maxBitrateBps = 800000000);

    NetworkQueue *m_networkQueue;
    QString m_serverUrl;
    QString m_deviceId;
    QString m_deviceName;
    QString m_clientVersion = QStringLiteral("0.1.0");
    QString m_userId;
    QString m_accessToken;
};
