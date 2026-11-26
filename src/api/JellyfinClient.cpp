#include "JellyfinClient.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSysInfo>
#include <QUrlQuery>

#include <algorithm>

namespace {
constexpr const char *kClientName = "cutefin";
}

JellyfinClient::JellyfinClient(NetworkQueue *networkQueue, QObject *parent)
    : QObject(parent)
    , m_networkQueue(networkQueue)
    , m_deviceName(QSysInfo::machineHostName())
{
}

void JellyfinClient::setDeviceId(const QString &id)
{
    m_deviceId = id;
}

void JellyfinClient::setDeviceName(const QString &name)
{
    m_deviceName = name;
}

void JellyfinClient::setClientVersion(const QString &version)
{
    m_clientVersion = version;
}

void JellyfinClient::setServerUrl(const QString &url)
{
    m_serverUrl = url;
}

QString JellyfinClient::serverUrl() const
{
    return m_serverUrl;
}

void JellyfinClient::setSession(const UserSession &session)
{
    m_serverUrl = session.serverUrl;
    m_userId = session.userId;
    m_accessToken = session.token;
    m_deviceId = session.deviceId;
}

void JellyfinClient::clearSession()
{
    m_userId.clear();
    m_accessToken.clear();
}

QString JellyfinClient::accessToken() const
{
    return m_accessToken;
}

QString JellyfinClient::userId() const
{
    return m_userId;
}

QString JellyfinClient::authorizationHeaderValue(bool includeToken) const
{
    QStringList parts;
    if (includeToken && !m_accessToken.isEmpty())
        parts << QStringLiteral("Token=\"%1\"").arg(m_accessToken);
    parts << QStringLiteral("Client=\"%1\"").arg(QString::fromLatin1(kClientName));
    parts << QStringLiteral("Device=\"%1\"").arg(m_deviceName);
    parts << QStringLiteral("DeviceId=\"%1\"").arg(m_deviceId);
    parts << QStringLiteral("Version=\"%1\"").arg(m_clientVersion);
    return QStringLiteral("MediaBrowser ") + parts.join(QStringLiteral(", "));
}

QUrl JellyfinClient::endpointUrl(const QString &path) const
{
    return QUrl(m_serverUrl + path);
}

QNetworkRequest JellyfinClient::buildRequest(const QString &path, AuthMode mode) const
{
    QNetworkRequest request(endpointUrl(path));
    switch (mode) {
    case AuthMode::None:
        break;
    case AuthMode::Anonymous:
        request.setRawHeader("Authorization", authorizationHeaderValue(false).toUtf8());
        break;
    case AuthMode::Authorized:
        request.setRawHeader("Authorization", authorizationHeaderValue(true).toUtf8());
        break;
    }
    return request;
}

void JellyfinClient::getJson(const QString &path, NetworkQueue::Priority priority, AuthMode mode, QObject *context,
                              const JsonCallback &callback)
{
    const QNetworkRequest request = buildRequest(path, mode);
    m_networkQueue->get(request, priority, context, [callback](QNetworkReply *reply) {
        const QByteArray data = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            if (callback)
                callback(false, QJsonDocument(), reply->errorString());
            return;
        }
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            if (callback)
                callback(false, QJsonDocument(), parseError.errorString());
            return;
        }
        if (callback)
            callback(true, doc, QString());
    });
}

void JellyfinClient::postJson(const QString &path, const QJsonObject &body, NetworkQueue::Priority priority,
                               AuthMode mode, QObject *context, const JsonCallback &callback)
{
    const QNetworkRequest request = buildRequest(path, mode);
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    m_networkQueue->post(request, payload, priority, context, [callback](QNetworkReply *reply) {
        const QByteArray data = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            if (callback)
                callback(false, QJsonDocument(), reply->errorString());
            return;
        }
        if (data.isEmpty()) {
            if (callback)
                callback(true, QJsonDocument(), QString());
            return;
        }
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            if (callback)
                callback(false, QJsonDocument(), parseError.errorString());
            return;
        }
        if (callback)
            callback(true, doc, QString());
    });
}

void JellyfinClient::postFireAndForget(const QString &path, const QJsonObject &body)
{
    postJson(path, body, NetworkQueue::Priority::Metadata, AuthMode::Authorized, nullptr, {});
}

void JellyfinClient::probeServer(const QString &url, QObject *context, const ServerInfoCallback &callback)
{
    QNetworkRequest request(QUrl(url + QStringLiteral("/System/Info/Public")));
    m_networkQueue->get(request, NetworkQueue::Priority::Metadata, context, [url, callback](QNetworkReply *reply) {
        const QByteArray data = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            if (callback)
                callback(false, ServerInfo(), reply->errorString());
            return;
        }
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            if (callback)
                callback(false, ServerInfo(), parseError.errorString());
            return;
        }
        ServerInfo info = ServerInfo::fromJson(doc.object());
        info.url = url;
        if (callback)
            callback(true, info, QString());
    });
}

void JellyfinClient::login(const QString &username, const QString &password, QObject *context,
                            const LoginCallback &callback)
{
    QJsonObject body;
    body[QStringLiteral("Username")] = username;
    body[QStringLiteral("Pw")] = password;

    postJson(QStringLiteral("/Users/AuthenticateByName"), body, NetworkQueue::Priority::Metadata, AuthMode::Anonymous,
             context, [this, callback](bool ok, const QJsonDocument &doc, const QString &error) {
                 if (!ok) {
                     if (callback)
                         callback(false, UserSession(), error);
                     return;
                 }
                 const QJsonObject obj = doc.object();
                 UserSession session;
                 session.serverUrl = m_serverUrl;
                 session.token = obj.value(QStringLiteral("AccessToken")).toString();
                 session.deviceId = m_deviceId;
                 const QJsonObject user = obj.value(QStringLiteral("User")).toObject();
                 session.userId = user.value(QStringLiteral("Id")).toString();
                 session.userName = user.value(QStringLiteral("Name")).toString();
                 if (callback)
                     callback(session.isValid(), session, session.isValid() ? QString() : QStringLiteral("Malformed login response"));
             });
}

void JellyfinClient::initiateQuickConnect(QObject *context, const QuickConnectInitCallback &callback)
{
    getJson(QStringLiteral("/QuickConnect/Initiate"), NetworkQueue::Priority::Metadata, AuthMode::Anonymous, context,
            [callback](bool ok, const QJsonDocument &doc, const QString &error) {
                if (!ok) {
                    if (callback)
                        callback(false, QString(), QString(), error);
                    return;
                }
                const QJsonObject obj = doc.object();
                const QString secret = obj.value(QStringLiteral("Secret")).toString();
                const QString code = obj.value(QStringLiteral("Code")).toString();
                const bool valid = !secret.isEmpty() && !code.isEmpty();
                if (callback)
                    callback(valid, secret, code, valid ? QString() : QStringLiteral("Quick Connect unavailable"));
            });
}

void JellyfinClient::pollQuickConnect(const QString &secret, QObject *context, const QuickConnectPollCallback &callback)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("secret"), secret);
    getJson(QStringLiteral("/QuickConnect/Connect?%1").arg(q.toString(QUrl::FullyEncoded)),
            NetworkQueue::Priority::Metadata, AuthMode::Anonymous, context,
            [callback](bool ok, const QJsonDocument &doc, const QString &error) {
                if (!ok) {
                    if (callback)
                        callback(false, false, error);
                    return;
                }
                const bool authed = doc.object().value(QStringLiteral("Authenticated")).toBool();
                if (callback)
                    callback(true, authed, QString());
            });
}

void JellyfinClient::authenticateWithQuickConnect(const QString &secret, QObject *context,
                                                  const LoginCallback &callback)
{
    QJsonObject body;
    body[QStringLiteral("Secret")] = secret;

    postJson(QStringLiteral("/Users/AuthenticateWithQuickConnect"), body, NetworkQueue::Priority::Metadata,
             AuthMode::Anonymous, context,
             [this, callback](bool ok, const QJsonDocument &doc, const QString &error) {
                 if (!ok) {
                     if (callback)
                         callback(false, UserSession(), error);
                     return;
                 }
                 const QJsonObject obj = doc.object();
                 UserSession session;
                 session.serverUrl = m_serverUrl;
                 session.token = obj.value(QStringLiteral("AccessToken")).toString();
                 session.deviceId = m_deviceId;
                 const QJsonObject user = obj.value(QStringLiteral("User")).toObject();
                 session.userId = user.value(QStringLiteral("Id")).toString();
                 session.userName = user.value(QStringLiteral("Name")).toString();
                 if (callback) {
                     callback(session.isValid(), session,
                              session.isValid() ? QString() : QStringLiteral("Malformed Quick Connect login response"));
                 }
             });
}

void JellyfinClient::logout(QObject *context, const std::function<void()> &callback)
{
    postJson(QStringLiteral("/Sessions/Logout"), QJsonObject(), NetworkQueue::Priority::Metadata, AuthMode::Authorized,
             context, [callback](bool, const QJsonDocument &, const QString &) {
                 if (callback)
                     callback();
             });
}

void JellyfinClient::fetchItemList(const QString &path, QObject *context, const ItemListCallback &callback)
{
    getJson(path, NetworkQueue::Priority::Metadata, AuthMode::Authorized, context,
            [callback](bool ok, const QJsonDocument &doc, const QString &error) {
                if (!ok) {
                    if (callback)
                        callback(false, {}, error);
                    return;
                }
                if (callback)
                    callback(true, parseItemList(doc), QString());
            });
}

void JellyfinClient::fetchUserViews(QObject *context, const ItemListCallback &callback)
{
    fetchItemList(QStringLiteral("/UserViews?userId=%1").arg(m_userId), context, callback);
}

void JellyfinClient::fetchResumeItems(QObject *context, const ItemListCallback &callback)
{
    fetchItemList(QStringLiteral("/UserItems/Resume?limit=16&mediaTypes=Video&fields=Overview,MediaSources&userId=%1")
                      .arg(m_userId),
                  context, callback);
}

void JellyfinClient::fetchNextUp(QObject *context, const ItemListCallback &callback)
{
    fetchItemList(QStringLiteral("/Shows/NextUp?userId=%1&limit=16&fields=Overview,MediaSources").arg(m_userId),
                  context, callback);
}

void JellyfinClient::fetchLatestItems(const QString &parentId, QObject *context, const ItemListCallback &callback)
{
    fetchItemList(QStringLiteral("/Items/Latest?parentId=%1&limit=16&fields=Overview,Genres&userId=%2")
                      .arg(parentId, m_userId),
                  context, callback);
}

void JellyfinClient::fetchSeasons(const QString &seriesId, QObject *context, const ItemListCallback &callback)
{
    fetchItemList(QStringLiteral("/Shows/%1/Seasons?userId=%2").arg(seriesId, m_userId), context, callback);
}

void JellyfinClient::fetchEpisodes(const QString &seriesId, const QString &seasonId, QObject *context,
                                    const ItemListCallback &callback)
{
    fetchItemList(QStringLiteral("/Shows/%1/Episodes?seasonId=%2&fields=Overview,MediaSources&userId=%3")
                      .arg(seriesId, seasonId, m_userId),
                  context, callback);
}

void JellyfinClient::searchItems(const QString &query, QObject *context, const ItemListCallback &callback)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("userId"), m_userId);
    q.addQueryItem(QStringLiteral("searchTerm"), query);
    q.addQueryItem(QStringLiteral("recursive"), QStringLiteral("true"));
    q.addQueryItem(QStringLiteral("includeItemTypes"), QStringLiteral("Movie,Series,Episode,Person"));
    q.addQueryItem(QStringLiteral("limit"), QStringLiteral("48"));
    fetchItemList(QStringLiteral("/Items?%1").arg(q.toString(QUrl::FullyEncoded)), context, callback);
}

void JellyfinClient::fetchItem(const QString &itemId, QObject *context, const ItemCallback &callback)
{
    getJson(QStringLiteral("/Items/%1?userId=%2&fields=Overview,Genres,People,Studios,Taglines,MediaSources,MediaStreams")
                .arg(itemId, m_userId),
            NetworkQueue::Priority::Metadata, AuthMode::Authorized, context,
            [callback](bool ok, const QJsonDocument &doc, const QString &error) {
                if (!ok) {
                    if (callback)
                        callback(false, MediaItem(), error);
                    return;
                }
                if (callback)
                    callback(true, MediaItem::fromJson(doc.object()), QString());
            });
}

void JellyfinClient::fetchItemDetails(const QString &itemId, QObject *context, const ItemCallback &callback)
{
    const QString path = QStringLiteral("/Items/%1?userId=%2&fields=%3")
                             .arg(itemId, m_userId,
                                  QStringLiteral("Chapters,Trickplay,MediaSources,MediaStreams,Overview,People,Genres"));
    getJson(path, NetworkQueue::Priority::Metadata, AuthMode::Authorized, context,
            [callback](bool ok, const QJsonDocument &doc, const QString &error) {
                if (!ok) {
                    if (callback)
                        callback(false, MediaItem(), error);
                    return;
                }
                if (callback)
                    callback(true, MediaItem::fromJson(doc.object()), QString());
            });
}

void JellyfinClient::fetchMediaSources(const QString &itemId, QObject *context,
                                        const MediaSourcesCallback &callback)
{
    const QString path =
        QStringLiteral("/Items/%1?userId=%2&fields=MediaSources,MediaStreams").arg(itemId, m_userId);
    getJson(path, NetworkQueue::Priority::Metadata, AuthMode::Authorized, context,
            [callback](bool ok, const QJsonDocument &doc, const QString &error) {
                if (!ok) {
                    if (callback)
                        callback(false, QList<MediaSource>(), error);
                    return;
                }
                QList<MediaSource> sources;
                for (const QJsonValue &v : doc.object().value(QStringLiteral("MediaSources")).toArray())
                    sources.append(MediaSource::fromJson(v.toObject()));
                if (callback)
                    callback(true, sources, QString());
            });
}

void JellyfinClient::fetchMediaSegments(const QString &itemId, QObject *context,
                                         const MediaSegmentsCallback &callback)
{
    getJson(QStringLiteral("/MediaSegments/%1").arg(itemId), NetworkQueue::Priority::Metadata,
            AuthMode::Authorized, context, [callback](bool ok, const QJsonDocument &doc, const QString &error) {
                if (!ok) {
                    if (callback)
                        callback(false, QList<MediaSegment>(), error);
                    return;
                }
                QList<MediaSegment> segments;
                for (const QJsonValue &v : doc.object().value(QStringLiteral("Items")).toArray())
                    segments.append(MediaSegment::fromJson(v.toObject()));
                if (callback)
                    callback(true, segments, QString());
            });
}

QUrl JellyfinClient::trickplayTileUrl(const QString &itemId, int thumbnailWidth, int sheetIndex) const
{
    QUrl url(m_serverUrl
             + QStringLiteral("/Videos/%1/Trickplay/%2/%3.jpg").arg(itemId).arg(thumbnailWidth).arg(sheetIndex));
    QUrlQuery query;
    if (!m_accessToken.isEmpty())
        query.addQueryItem(QStringLiteral("api_key"), m_accessToken);
    url.setQuery(query);
    return url;
}

QUrl JellyfinClient::subtitleStreamUrl(const QString &itemId, const QString &mediaSourceId, int streamIndex,
                                        const QString &format) const
{
    QUrl url(m_serverUrl
             + QStringLiteral("/Videos/%1/%2/Subtitles/%3/Stream.%4")
                   .arg(itemId, mediaSourceId)
                   .arg(streamIndex)
                   .arg(format));
    QUrlQuery query;
    if (!m_accessToken.isEmpty())
        query.addQueryItem(QStringLiteral("api_key"), m_accessToken);
    url.setQuery(query);
    return url;
}

QString JellyfinClient::buildItemsPath(const ItemQuery &query) const
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("userId"), m_userId);
    q.addQueryItem(QStringLiteral("recursive"), QStringLiteral("true"));
    q.addQueryItem(QStringLiteral("startIndex"), QString::number(query.startIndex));
    q.addQueryItem(QStringLiteral("limit"), QString::number(query.limit));
    q.addQueryItem(QStringLiteral("sortBy"), query.sortBy);
    q.addQueryItem(QStringLiteral("sortOrder"),
                   query.ascending ? QStringLiteral("Ascending") : QStringLiteral("Descending"));

    if (!query.parentId.isEmpty())
        q.addQueryItem(QStringLiteral("parentId"), query.parentId);
    if (!query.includeItemTypes.isEmpty())
        q.addQueryItem(QStringLiteral("includeItemTypes"), query.includeItemTypes.join(QLatin1Char(',')));

    // Jellyfin expects these multi-valued filters pipe-separated in a single
    // parameter, not repeated -- repeating them silently keeps only the last.
    if (!query.genres.isEmpty())
        q.addQueryItem(QStringLiteral("genres"), query.genres.join(QLatin1Char('|')));
    if (!query.officialRatings.isEmpty())
        q.addQueryItem(QStringLiteral("officialRatings"), query.officialRatings.join(QLatin1Char('|')));
    if (!query.years.isEmpty()) {
        QStringList years;
        years.reserve(query.years.size());
        for (int year : query.years)
            years << QString::number(year);
        q.addQueryItem(QStringLiteral("years"), years.join(QLatin1Char(',')));
    }

    if (!query.personId.isEmpty())
        q.addQueryItem(QStringLiteral("personIds"), query.personId);
    if (!query.searchTerm.isEmpty())
        q.addQueryItem(QStringLiteral("searchTerm"), query.searchTerm);
    if (!query.nameStartsWith.isEmpty())
        q.addQueryItem(QStringLiteral("nameStartsWith"), query.nameStartsWith);

    if (query.played != ItemQuery::Played::Any) {
        q.addQueryItem(QStringLiteral("isPlayed"), query.played == ItemQuery::Played::PlayedOnly
                                                       ? QStringLiteral("true")
                                                       : QStringLiteral("false"));
    }
    if (query.favoritesOnly)
        q.addQueryItem(QStringLiteral("isFavorite"), QStringLiteral("true"));

    return QStringLiteral("/Items?%1").arg(q.toString(QUrl::FullyEncoded));
}

void JellyfinClient::fetchItems(const ItemQuery &query, QObject *context, const ItemPageCallback &callback)
{
    const int startIndex = query.startIndex;
    getJson(buildItemsPath(query), NetworkQueue::Priority::Metadata, AuthMode::Authorized, context,
            [callback, startIndex](bool ok, const QJsonDocument &doc, const QString &error) {
                if (!ok) {
                    if (callback)
                        callback(false, ItemPage(), error);
                    return;
                }
                ItemPage page;
                page.startIndex = startIndex;
                page.items = parseItemList(doc);
                // A bare-array response (some endpoints) has no count of its
                // own; the page we just parsed is all there is.
                page.totalRecordCount = doc.isObject()
                    ? doc.object().value(QStringLiteral("TotalRecordCount")).toInt(page.items.size())
                    : page.items.size();
                if (callback)
                    callback(true, page, QString());
            });
}

JellyfinClient::LibraryFilters JellyfinClient::parseLibraryFilters(const QJsonObject &obj)
{
    LibraryFilters filters;
    for (const QJsonValue &v : obj.value(QStringLiteral("Genres")).toArray())
        filters.genres << v.toString();
    for (const QJsonValue &v : obj.value(QStringLiteral("OfficialRatings")).toArray())
        filters.officialRatings << v.toString();
    for (const QJsonValue &v : obj.value(QStringLiteral("Years")).toArray())
        filters.years << v.toInt();

    filters.genres.removeAll(QString());
    filters.officialRatings.removeAll(QString());
    std::sort(filters.years.begin(), filters.years.end(), std::greater<int>());
    return filters;
}

void JellyfinClient::fetchLibraryFilters(const QString &parentId, const QStringList &includeItemTypes,
                                          QObject *context, const FiltersCallback &callback)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("userId"), m_userId);
    if (!parentId.isEmpty())
        q.addQueryItem(QStringLiteral("parentId"), parentId);
    if (!includeItemTypes.isEmpty())
        q.addQueryItem(QStringLiteral("includeItemTypes"), includeItemTypes.join(QLatin1Char(',')));

    getJson(QStringLiteral("/Items/Filters?%1").arg(q.toString(QUrl::FullyEncoded)),
            NetworkQueue::Priority::Metadata, AuthMode::Authorized, context,
            [callback](bool ok, const QJsonDocument &doc, const QString &error) {
                if (!ok) {
                    if (callback)
                        callback(false, LibraryFilters(), error);
                    return;
                }
                if (callback)
                    callback(true, parseLibraryFilters(doc.object()), QString());
            });
}

void JellyfinClient::toggleUserItemState(const QString &path, bool set, QObject *context,
                                          const ResultCallback &callback)
{
    const QNetworkRequest request = buildRequest(path, AuthMode::Authorized);
    const auto handle = [callback](QNetworkReply *reply) {
        if (!callback)
            return;
        if (reply->error() != QNetworkReply::NoError)
            callback(false, reply->errorString());
        else
            callback(true, QString());
    };

    if (set)
        m_networkQueue->post(request, QByteArray(), NetworkQueue::Priority::Metadata, context, handle);
    else
        m_networkQueue->deleteResource(request, NetworkQueue::Priority::Metadata, context, handle);
}

void JellyfinClient::setPlayed(const QString &itemId, bool played, QObject *context, const ResultCallback &callback)
{
    toggleUserItemState(QStringLiteral("/UserPlayedItems/%1?userId=%2").arg(itemId, m_userId), played, context,
                        callback);
}

void JellyfinClient::setFavorite(const QString &itemId, bool favorite, QObject *context,
                                  const ResultCallback &callback)
{
    toggleUserItemState(QStringLiteral("/UserFavoriteItems/%1?userId=%2").arg(itemId, m_userId), favorite, context,
                        callback);
}

QUrl JellyfinClient::imageUrl(const QString &itemId, const QString &type, const QString &tag, int maxWidth) const
{
    QUrl url(m_serverUrl + QStringLiteral("/Items/%1/Images/%2").arg(itemId, type));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("tag"), tag);
    query.addQueryItem(QStringLiteral("maxWidth"), QString::number(maxWidth));
    query.addQueryItem(QStringLiteral("quality"), QStringLiteral("90"));
    if (!m_accessToken.isEmpty())
        query.addQueryItem(QStringLiteral("api_key"), m_accessToken);
    url.setQuery(query);
    return url;
}

QJsonObject JellyfinClient::deviceProfile(qint64 maxBitrateBps)
{
    const qint64 bitrate = maxBitrateBps > 0 ? maxBitrateBps : 800000000LL;
    // Permissive profile: mpv/ffmpeg is the real decoder, so we advertise
    // wide container/codec support to make the server pick direct play
    // whenever possible instead of transcoding.
    QJsonObject directPlay;
    directPlay[QStringLiteral("Container")] = QStringLiteral("mkv,mp4,avi,webm,mov,ts,m2ts,flv,ogv");
    directPlay[QStringLiteral("Type")] = QStringLiteral("Video");
    directPlay[QStringLiteral("VideoCodec")] =
        QStringLiteral("h264,hevc,av1,vp9,vp8,mpeg4,mpeg2video,vc1,theora");
    directPlay[QStringLiteral("AudioCodec")] =
        QStringLiteral("aac,ac3,eac3,dts,truehd,flac,opus,mp3,vorbis,pcm,alac");

    QJsonObject transcodeProfile;
    transcodeProfile[QStringLiteral("Container")] = QStringLiteral("ts");
    transcodeProfile[QStringLiteral("Type")] = QStringLiteral("Video");
    transcodeProfile[QStringLiteral("VideoCodec")] = QStringLiteral("h264");
    transcodeProfile[QStringLiteral("AudioCodec")] = QStringLiteral("aac,ac3,opus");
    transcodeProfile[QStringLiteral("Protocol")] = QStringLiteral("hls");
    transcodeProfile[QStringLiteral("Context")] = QStringLiteral("Streaming");

    QJsonObject profile;
    profile[QStringLiteral("MaxStreamingBitrate")] = bitrate;
    profile[QStringLiteral("MaxStaticBitrate")] = bitrate;
    profile[QStringLiteral("MusicStreamingTranscodingBitrate")] = 384000;
    profile[QStringLiteral("DirectPlayProfiles")] = QJsonArray{directPlay};
    profile[QStringLiteral("TranscodingProfiles")] = QJsonArray{transcodeProfile};
    profile[QStringLiteral("CodecProfiles")] = QJsonArray{};
    profile[QStringLiteral("SubtitleProfiles")] = QJsonArray{
        QJsonObject{{QStringLiteral("Format"), QStringLiteral("srt")}, {QStringLiteral("Method"), QStringLiteral("External")}},
        QJsonObject{{QStringLiteral("Format"), QStringLiteral("ass")}, {QStringLiteral("Method"), QStringLiteral("External")}},
        QJsonObject{{QStringLiteral("Format"), QStringLiteral("subrip")}, {QStringLiteral("Method"), QStringLiteral("External")}},
    };
    return profile;
}

void JellyfinClient::getPlaybackInfo(const QString &itemId, QObject *context, const PlaybackInfoCallback &callback)
{
    getPlaybackInfo(itemId, 0, false, context, callback);
}

void JellyfinClient::getPlaybackInfo(const QString &itemId, qint64 maxBitrateBps, bool forceTranscode, QObject *context,
                                     const PlaybackInfoCallback &callback)
{
    QJsonObject body;
    body[QStringLiteral("DeviceProfile")] = deviceProfile(maxBitrateBps);
    body[QStringLiteral("EnableDirectPlay")] = !forceTranscode;
    body[QStringLiteral("EnableDirectStream")] = !forceTranscode;
    body[QStringLiteral("EnableTranscoding")] = true;
    body[QStringLiteral("AutoOpenLiveStream")] = true;
    if (maxBitrateBps > 0)
        body[QStringLiteral("MaxStreamingBitrate")] = maxBitrateBps;

    const QString path = QStringLiteral("/Items/%1/PlaybackInfo?userId=%2").arg(itemId, m_userId);
    postJson(path, body, NetworkQueue::Priority::Metadata, AuthMode::Authorized, context,
             [this, callback](bool ok, const QJsonDocument &doc, const QString &error) {
                 if (!ok) {
                     if (callback)
                         callback(PlaybackResolution(), error);
                     return;
                 }
                 const QJsonObject obj = doc.object();
                 const QString playSessionId = obj.value(QStringLiteral("PlaySessionId")).toString();
                 QList<MediaSource> sources;
                 const QJsonArray sourcesArray = obj.value(QStringLiteral("MediaSources")).toArray();
                 for (const QJsonValue &v : sourcesArray)
                     sources.append(MediaSource::fromJson(v.toObject()));

                 PlaybackResolution resolution = resolvePlayback(playSessionId, sources, m_serverUrl, m_accessToken);
                 if (resolution.ok && resolution.isTranscoding) {
                     const QUrlQuery q(resolution.url);
                     const QString reasons = q.queryItemValue(QStringLiteral("TranscodeReasons"));
                     if (!reasons.isEmpty())
                         resolution.transcodeReasons = reasons.split(QLatin1Char(','), Qt::SkipEmptyParts);
                 }
                 if (callback)
                     callback(resolution, resolution.ok ? QString() : QStringLiteral("No playable media source"));
             });
}

JellyfinClient::PlaybackResolution JellyfinClient::resolvePlayback(const QString &playSessionId,
                                                                     const QList<MediaSource> &sources,
                                                                     const QString &serverUrl,
                                                                     const QString &accessToken)
{
    PlaybackResolution resolution;

    for (const MediaSource &source : sources) {
        if (source.supportsDirectPlay || source.supportsDirectStream) {
            QUrl url(serverUrl + QStringLiteral("/Videos/%1/stream").arg(source.id));
            QUrlQuery query;
            query.addQueryItem(QStringLiteral("static"), QStringLiteral("true"));
            query.addQueryItem(QStringLiteral("mediaSourceId"), source.id);
            query.addQueryItem(QStringLiteral("api_key"), accessToken);
            url.setQuery(query);

            resolution.ok = true;
            resolution.playSessionId = playSessionId;
            resolution.mediaSourceId = source.id;
            resolution.url = url;
            resolution.isTranscoding = false;
            resolution.container = source.container;
            resolution.size = source.size;
            resolution.bitRate = source.bitRate;
            return resolution;
        }
    }

    for (const MediaSource &source : sources) {
        if (!source.transcodingUrl.isEmpty()) {
            // TranscodingUrl already carries its own query string and key.
            resolution.ok = true;
            resolution.playSessionId = playSessionId;
            resolution.mediaSourceId = source.id;
            resolution.url = QUrl(serverUrl + source.transcodingUrl);
            resolution.isTranscoding = true;
            resolution.container = source.container;
            resolution.size = source.size;
            resolution.bitRate = source.bitRate;
            return resolution;
        }
    }

    return resolution;
}

void JellyfinClient::reportPlaybackStart(const QString &itemId, const QString &playSessionId,
                                          const QString &mediaSourceId, qint64 positionTicks)
{
    QJsonObject body;
    body[QStringLiteral("ItemId")] = itemId;
    body[QStringLiteral("PlaySessionId")] = playSessionId;
    body[QStringLiteral("MediaSourceId")] = mediaSourceId;
    body[QStringLiteral("PositionTicks")] = positionTicks;
    body[QStringLiteral("IsPaused")] = false;
    body[QStringLiteral("CanSeek")] = true;
    postFireAndForget(QStringLiteral("/Sessions/Playing"), body);
}

void JellyfinClient::reportPlaybackProgress(const QString &itemId, const QString &playSessionId,
                                             const QString &mediaSourceId, qint64 positionTicks, bool isPaused)
{
    QJsonObject body;
    body[QStringLiteral("ItemId")] = itemId;
    body[QStringLiteral("PlaySessionId")] = playSessionId;
    body[QStringLiteral("MediaSourceId")] = mediaSourceId;
    body[QStringLiteral("PositionTicks")] = positionTicks;
    body[QStringLiteral("IsPaused")] = isPaused;
    body[QStringLiteral("CanSeek")] = true;
    postFireAndForget(QStringLiteral("/Sessions/Playing/Progress"), body);
}

void JellyfinClient::reportPlaybackStopped(const QString &itemId, const QString &playSessionId,
                                            const QString &mediaSourceId, qint64 positionTicks)
{
    QJsonObject body;
    body[QStringLiteral("ItemId")] = itemId;
    body[QStringLiteral("PlaySessionId")] = playSessionId;
    body[QStringLiteral("MediaSourceId")] = mediaSourceId;
    body[QStringLiteral("PositionTicks")] = positionTicks;
    body[QStringLiteral("CanSeek")] = true;
    postFireAndForget(QStringLiteral("/Sessions/Playing/Stopped"), body);
}
