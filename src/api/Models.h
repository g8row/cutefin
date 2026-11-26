#pragma once

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

// Plain value types mirroring the Jellyfin REST payloads. Every type has a
// static fromJson(const QJsonObject &) and is cheap to copy (Qt containers
// are implicitly shared). None of these are QObjects.

struct ServerInfo {
    QString name;
    QString version;
    QString id;
    QString url;

    static ServerInfo fromJson(const QJsonObject &obj);
};

struct UserSession {
    QString serverUrl;
    QString userId;
    QString userName;
    QString token;
    QString deviceId;

    bool isValid() const { return !serverUrl.isEmpty() && !userId.isEmpty() && !token.isEmpty(); }
};

struct UserData {
    bool played = false;
    double playedPercentage = 0.0;
    qint64 playbackPositionTicks = 0;
    bool isFavorite = false;

    static UserData fromJson(const QJsonObject &obj);
};

struct NameId {
    QString name;
    QString id;

    static NameId fromJson(const QJsonObject &obj);
};

struct Person {
    QString name;
    QString id;
    QString role;
    QString type;
    QString primaryImageTag;

    static Person fromJson(const QJsonObject &obj);
};

// A named chapter marker. `startTicks` is 100ns units like every other
// position in the API.
struct Chapter {
    QString name;
    qint64 startTicks = 0;
    QString imageTag; // empty when the chapter has no extracted image

    static Chapter fromJson(const QJsonObject &obj);
};

// Describes one trickplay tile sheet resolution for a media source. Sheets are
// `tileWidth` x `tileHeight` grids of `width` x `height` thumbnails, one
// thumbnail every `intervalMs`. To find thumbnail i for position ms:
//   i = ms / intervalMs
//   sheet   = i / (tileWidth * tileHeight)
//   row     = (i / tileWidth) % tileHeight
//   column  = i % tileWidth
struct TrickplayInfo {
    int width = 0;
    int height = 0;
    int tileWidth = 0;
    int tileHeight = 0;
    int thumbnailCount = 0;
    int intervalMs = 0;

    bool isValid() const { return width > 0 && tileWidth > 0 && tileHeight > 0 && intervalMs > 0; }
    int thumbnailsPerSheet() const { return tileWidth * tileHeight; }

    static TrickplayInfo fromJson(const QJsonObject &obj);
};

// An intro/outro/recap region, from /MediaSegments/{itemId}. Servers without
// an intro-detection plugin return none of these, so every consumer must
// treat an empty list as normal rather than as an error.
struct MediaSegment {
    QString type; // "Intro", "Outro", "Recap", "Preview", "Commercial"
    qint64 startTicks = 0;
    qint64 endTicks = 0;

    static MediaSegment fromJson(const QJsonObject &obj);
};

struct MediaStream {
    QString type; // "Video", "Audio", "Subtitle"
    QString codec;
    QString language;
    QString displayTitle;
    int index = -1;
    bool isDefault = false;
    bool isExternal = false;
    int width = 0;
    int height = 0;
    int channels = 0;
    QString channelLayout;
    qint64 bitRate = 0;
    QString videoRange;

    static MediaStream fromJson(const QJsonObject &obj);
};

struct MediaSource {
    QString id;
    QString container;
    bool supportsDirectPlay = false;
    bool supportsDirectStream = false;
    QString transcodingUrl;
    qint64 size = 0;
    qint64 bitRate = 0;
    QList<MediaStream> streams;

    static MediaSource fromJson(const QJsonObject &obj);
};

// The workhorse type: movies, series, seasons, episodes, and library views
// (UserViews) all come back through this one shape.
struct MediaItem {
    QString id;
    QString name;
    QString type; // "Movie", "Series", "Season", "Episode", "CollectionFolder", ...
    QString collectionType; // set on UserViews entries: "movies", "tvshows", "music", "boxsets"
    QString overview;
    QString tagline;
    int productionYear = 0;
    qint64 runTimeTicks = 0;
    double communityRating = -1.0;
    double criticRating = -1.0;
    QString officialRating;
    QStringList genres;
    QList<NameId> studios;
    QList<Person> people;

    // Keyed by image type ("Primary", "Thumb", "Logo", ...) -> tag.
    QHash<QString, QString> imageTags;
    QStringList backdropTags;
    QString parentBackdropItemId;
    QStringList parentBackdropTags;
    // Keyed by image type -> (tag -> blurhash).
    QHash<QString, QHash<QString, QString>> blurHashes;

    QString seriesId;
    QString seriesName;
    QString seasonId;
    QString seasonName;
    int indexNumber = -1;
    int parentIndexNumber = -1;

    UserData userData;

    // Only populated when the request asked for them via `fields`; an item
    // from a list endpoint has these empty.
    QList<Chapter> chapters;
    // mediaSourceId -> (tile width -> info). The width key is what goes in
    // the /Videos/{id}/Trickplay/{width}/{index}.jpg path.
    QHash<QString, QHash<int, TrickplayInfo>> trickplay;
    QList<MediaSource> mediaSources;

    static MediaItem fromJson(const QJsonObject &obj);
};

// Handles both response shapes the Jellyfin API returns for item lists:
// a bare JSON array (e.g. /Items/Latest) and the usual {"Items":[...]}
// envelope (almost everything else).
QList<MediaItem> parseItemList(const QJsonDocument &doc);
