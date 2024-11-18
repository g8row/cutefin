#ifndef JELLYFINAPI_H
#define JELLYFINAPI_H

#include <QObject>
#include <QNetworkAccessManager>

class JellyfinApi : public QObject
{
    Q_OBJECT
public:
    JellyfinApi(QObject *parent = nullptr);
    void pingServer(const QString &url);

private slots:
signals:
    void pingServerResponse(bool success, const QString &errorMessage = QString());
private:
    QNetworkAccessManager* manager;
};

#endif // JELLYFINAPI_H
