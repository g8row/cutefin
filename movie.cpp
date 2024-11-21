#include "movie.h"

Movie::Movie(const QJsonObject &json) {
    id = json.value("Id").toString();
    name = json.value("Name").toString();
    overview = json.value("Overview").toString();
    runTimeTicks = json.value("RunTimeTicks").toVariant().toLongLong();
    mediaType = json.value("MediaType").toString();
}

QString Movie::getId() const {
    return id;
}

QString Movie::getName() const {
    return name;
}

QString Movie::getOverview() const {
    return overview;
}

int Movie::getRunTimeMinutes() const {
    // Convert ticks to minutes (1 tick = 100 nanoseconds)
    return static_cast<int>(runTimeTicks / (10000 * 1000 * 60));
}

QString Movie::getMediaType() const {
    return mediaType;
}

Movie Movie::fromJson(const QJsonObject &json) {
    return Movie(json);
}

QJsonObject Movie::toJson() const {
    QJsonObject json;
    json["Id"] = id;
    json["Name"] = name;
    json["Overview"] = overview;
    json["RunTimeTicks"] = QString::number(runTimeTicks);
    json["MediaType"] = mediaType;
    return json;
}
