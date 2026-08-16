#include "Models.h"

#include <QJsonArray>
#include <QJsonValue>

namespace {

QStringList toStringList(const QJsonArray &array)
{
    QStringList list;
    list.reserve(array.size());
    for (const QJsonValue &v : array)
        list.append(v.toString());
    return list;
}

// Jellyfin returns -1/absent for "no rating"; keep that convention.
double optionalDouble(const QJsonObject &obj, const QString &key, double fallback = -1.0)
{
    const QJsonValue v = obj.value(key);
    return v.isDouble() ? v.toDouble() : fallback;
}

int optionalInt(const QJsonObject &obj, const QString &key, int fallback)
{
    const QJsonValue v = obj.value(key);
    return v.isDouble() ? v.toInt() : fallback;
}

} // namespace

ServerInfo ServerInfo::fromJson(const QJsonObject &obj)
{
    ServerInfo info;
    info.name = obj.value(QStringLiteral("ServerName")).toString();
    info.version = obj.value(QStringLiteral("Version")).toString();
    info.id = obj.value(QStringLiteral("Id")).toString();
    info.url = obj.value(QStringLiteral("LocalAddress")).toString();
    return info;
}

UserData UserData::fromJson(const QJsonObject &obj)
{
    UserData data;
    data.played = obj.value(QStringLiteral("Played")).toBool();
    data.playedPercentage = optionalDouble(obj, QStringLiteral("PlayedPercentage"), 0.0);
    data.playbackPositionTicks = static_cast<qint64>(obj.value(QStringLiteral("PlaybackPositionTicks")).toDouble());
    data.isFavorite = obj.value(QStringLiteral("IsFavorite")).toBool();
    return data;
}

NameId NameId::fromJson(const QJsonObject &obj)
{
    NameId n;
    n.name = obj.value(QStringLiteral("Name")).toString();
    n.id = obj.value(QStringLiteral("Id")).toString();
    return n;
}

Person Person::fromJson(const QJsonObject &obj)
{
    Person p;
    p.name = obj.value(QStringLiteral("Name")).toString();
    p.id = obj.value(QStringLiteral("Id")).toString();
    p.role = obj.value(QStringLiteral("Role")).toString();
    p.type = obj.value(QStringLiteral("Type")).toString();
    p.primaryImageTag = obj.value(QStringLiteral("PrimaryImageTag")).toString();
    return p;
}

MediaItem MediaItem::fromJson(const QJsonObject &obj)
{
    MediaItem item;
    item.id = obj.value(QStringLiteral("Id")).toString();
    item.name = obj.value(QStringLiteral("Name")).toString();
    item.type = obj.value(QStringLiteral("Type")).toString();
    item.collectionType = obj.value(QStringLiteral("CollectionType")).toString();
    item.overview = obj.value(QStringLiteral("Overview")).toString();

    const QJsonArray taglines = obj.value(QStringLiteral("Taglines")).toArray();
    if (!taglines.isEmpty())
        item.tagline = taglines.first().toString();

    item.productionYear = optionalInt(obj, QStringLiteral("ProductionYear"), 0);
    item.runTimeTicks = static_cast<qint64>(obj.value(QStringLiteral("RunTimeTicks")).toDouble());
    item.communityRating = optionalDouble(obj, QStringLiteral("CommunityRating"));
    item.criticRating = optionalDouble(obj, QStringLiteral("CriticRating"));
    item.officialRating = obj.value(QStringLiteral("OfficialRating")).toString();
    item.genres = toStringList(obj.value(QStringLiteral("Genres")).toArray());

    const QJsonArray studios = obj.value(QStringLiteral("Studios")).toArray();
    for (const QJsonValue &v : studios)
        item.studios.append(NameId::fromJson(v.toObject()));

    const QJsonArray people = obj.value(QStringLiteral("People")).toArray();
    for (const QJsonValue &v : people)
        item.people.append(Person::fromJson(v.toObject()));

    const QJsonObject imageTags = obj.value(QStringLiteral("ImageTags")).toObject();
    for (auto it = imageTags.constBegin(); it != imageTags.constEnd(); ++it)
        item.imageTags.insert(it.key(), it.value().toString());

    item.backdropTags = toStringList(obj.value(QStringLiteral("BackdropImageTags")).toArray());

    const QJsonObject blurHashes = obj.value(QStringLiteral("ImageBlurHashes")).toObject();
    for (auto typeIt = blurHashes.constBegin(); typeIt != blurHashes.constEnd(); ++typeIt) {
        const QJsonObject tagMap = typeIt.value().toObject();
        QHash<QString, QString> tagsForType;
        for (auto tagIt = tagMap.constBegin(); tagIt != tagMap.constEnd(); ++tagIt)
            tagsForType.insert(tagIt.key(), tagIt.value().toString());
        item.blurHashes.insert(typeIt.key(), tagsForType);
    }

    item.seriesId = obj.value(QStringLiteral("SeriesId")).toString();
    item.seriesName = obj.value(QStringLiteral("SeriesName")).toString();
    item.seasonId = obj.value(QStringLiteral("SeasonId")).toString();
    item.seasonName = obj.value(QStringLiteral("SeasonName")).toString();
    item.indexNumber = optionalInt(obj, QStringLiteral("IndexNumber"), -1);
    item.parentIndexNumber = optionalInt(obj, QStringLiteral("ParentIndexNumber"), -1);

    item.userData = UserData::fromJson(obj.value(QStringLiteral("UserData")).toObject());

    return item;
}

QList<MediaItem> parseItemList(const QJsonDocument &doc)
{
    QJsonArray array;
    if (doc.isArray()) {
        array = doc.array();
    } else if (doc.isObject()) {
        array = doc.object().value(QStringLiteral("Items")).toArray();
    }

    QList<MediaItem> items;
    items.reserve(array.size());
    for (const QJsonValue &v : array)
        items.append(MediaItem::fromJson(v.toObject()));
    return items;
}

MediaStream MediaStream::fromJson(const QJsonObject &obj)
{
    MediaStream stream;
    stream.type = obj.value(QStringLiteral("Type")).toString();
    stream.codec = obj.value(QStringLiteral("Codec")).toString();
    stream.language = obj.value(QStringLiteral("Language")).toString();
    stream.displayTitle = obj.value(QStringLiteral("DisplayTitle")).toString();
    stream.index = optionalInt(obj, QStringLiteral("Index"), -1);
    stream.isDefault = obj.value(QStringLiteral("IsDefault")).toBool();
    stream.isExternal = obj.value(QStringLiteral("IsExternal")).toBool();
    return stream;
}

MediaSource MediaSource::fromJson(const QJsonObject &obj)
{
    MediaSource source;
    source.id = obj.value(QStringLiteral("Id")).toString();
    source.container = obj.value(QStringLiteral("Container")).toString();
    source.supportsDirectPlay = obj.value(QStringLiteral("SupportsDirectPlay")).toBool();
    source.supportsDirectStream = obj.value(QStringLiteral("SupportsDirectStream")).toBool();
    source.transcodingUrl = obj.value(QStringLiteral("TranscodingUrl")).toString();

    const QJsonArray streams = obj.value(QStringLiteral("MediaStreams")).toArray();
    for (const QJsonValue &v : streams)
        source.streams.append(MediaStream::fromJson(v.toObject()));

    return source;
}
