#ifndef MOVIE_H
#define MOVIE_H

#include <QString>
#include <QJsonObject>

class Movie {
public:
    // Constructors
    Movie() = default;
    explicit Movie(const QJsonObject &json);

    // Accessor Methods
    QString getId() const;
    QString getName() const;
    QString getOverview() const;
    int getRunTimeMinutes() const;
    QString getMediaType() const;

    // Utility Methods
    static Movie fromJson(const QJsonObject &json);
    QJsonObject toJson() const;

private:
    // Member Variables
    QString id;
    QString name;
    QString overview;
    qint64 runTimeTicks; // Jellyfin uses ticks (100 nanoseconds per tick)
    QString mediaType;
};

#endif // MOVIE_H
