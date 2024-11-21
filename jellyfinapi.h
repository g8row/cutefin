#ifndef JELLYFINAPI_H
#define JELLYFINAPI_H

#include "movie.h"

#include <QObject>
#include <QNetworkAccessManager>

class JellyfinApi : public QObject
{
    Q_OBJECT
public:
    JellyfinApi(QObject *parent = nullptr);
    void pingServer(const QString &_url);
    void login(const QString &_user, const QString &_password);
    void setUrl(const QString &_url);
    void setAccessToken(const QString &_accessToken);
    const QString& getUrl() const;
    const QString& getAccessToken() const;
    void getLatestMovies();

private slots:
signals:
    void pingServerResponse(bool success, const QString &errorMessage = QString());
    void loginResponse(bool success, const QString &accessToken = QString());
    void latestMoviesResponse(QList<Movie> movies, bool success, const QString &errorMessage = QString());
private:
    QNetworkAccessManager* manager;
    QString url;
    QString accessToken;
    QString device;
    QString deviceId;
    QString version;

    QNetworkRequest getAuthenticatedRequest(const QString& endpoint);
};

#endif // JELLYFINAPI_H
