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
    // Keyed by image type -> (tag -> blurhash).
    QHash<QString, QHash<QString, QString>> blurHashes;

    QString seriesId;
    QString seriesName;
    QString seasonId;
    QString seasonName;
    int indexNumber = -1;
    int parentIndexNumber = -1;

    UserData userData;

    static MediaItem fromJson(const QJsonObject &obj);
};

// Handles both response shapes the Jellyfin API returns for item lists:
// a bare JSON array (e.g. /Items/Latest) and the usual {"Items":[...]}
// envelope (almost everything else).
QList<MediaItem> parseItemList(const QJsonDocument &doc);

struct MediaStream {
    QString type; // "Video", "Audio", "Subtitle"
    QString codec;
    QString language;
    QString displayTitle;
    int index = -1;
    bool isDefault = false;
    bool isExternal = false;

    static MediaStream fromJson(const QJsonObject &obj);
};

struct MediaSource {
    QString id;
    QString container;
    bool supportsDirectPlay = false;
    bool supportsDirectStream = false;
    QString transcodingUrl;
    QList<MediaStream> streams;

    static MediaSource fromJson(const QJsonObject &obj);
};
