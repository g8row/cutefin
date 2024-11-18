#include "jellyfinapi.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>


JellyfinApi::JellyfinApi(QObject *parent)
    : QObject{parent}
{
    manager = new QNetworkAccessManager(this);
}

void JellyfinApi::pingServer(const QString &url){
    QNetworkReply *_reply = manager->get(QNetworkRequest(QUrl(url)));
    connect(_reply, &QNetworkReply::finished, this, [this, _reply](){
        if (_reply->error() == QNetworkReply::NoError) {
            emit pingServerResponse(true);
        } else {
            emit pingServerResponse(false,_reply->errorString());
        }
        _reply->deleteLater();
    });
}

