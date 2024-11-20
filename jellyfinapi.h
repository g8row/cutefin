#ifndef JELLYFINAPI_H
#define JELLYFINAPI_H

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
private slots:
signals:
    void pingServerResponse(bool success, const QString &errorMessage = QString());
    void loginResponse(bool success, const QString &accessToken = QString());
private:
    QNetworkAccessManager* manager;
    QString url;
};

#endif // JELLYFINAPI_H
