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

void JellyfinApi::setUrl(const QString &_url){
    url = _url;
}

void JellyfinApi::login(const QString &user, const QString &password){
    QNetworkRequest request = QNetworkRequest(QUrl(url + "/Users/AuthenticateByName"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QString authHeader = QString("MediaBrowser Client=\"%1\", Device=\"%2\", DeviceId=\"%3\", Version=\"%4\"")
                             .arg("cutefin") // Replace with your app name
                             .arg("PC")           // Replace with your device name
                             .arg("g8row-debian") // Replace with a unique device ID
                             .arg("0.0.1");       // Replace with your app version
    request.setRawHeader("X-Emby-Authorization", authHeader.toUtf8());

    QJsonObject jsonPayload;
    jsonPayload["Username"] = user;
    jsonPayload["Pw"] = password;
    QByteArray requestBody = QJsonDocument(jsonPayload).toJson();

    QNetworkReply *_reply = manager->post(request, requestBody);
    connect(_reply, &QNetworkReply::finished, this, [this, _reply](){
        if (_reply->error() == QNetworkReply::NoError) {
            QByteArray responseData = _reply->readAll();

            QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData);
            if (!jsonDoc.isNull() && jsonDoc.isObject()) {
                QJsonObject jsonObject = jsonDoc.object();
                QString accessToken = jsonObject["AccessToken"].toString();
                QJsonObject user = jsonObject["User"].toObject();
                //QJsonArray jsonArray = jsonObject["ArrayField"].toArray();
                /*for (const QJsonValue &value : jsonArray) {
                    qDebug() << "Array item:" << value.toString();
                }*/
                //QString name = user["Name"].toString();
                emit loginResponse(true, accessToken);
            } else {
                //qDebug() << "Failed to parse JSON";
                emit loginResponse(false);
            }
        } else {
            emit loginResponse(false);
            //qDebug() << "Error:" << _reply->errorString();
        }
        _reply->deleteLater();
    });
}



