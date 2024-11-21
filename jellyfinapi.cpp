#include "jellyfinapi.h"
#include "mainconfig.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>
#include <QSysInfo>


JellyfinApi::JellyfinApi(QObject *parent)
    : QObject{parent}
{
    manager = new QNetworkAccessManager(this);
    QSysInfo *info = new QSysInfo();
    deviceId = info->machineUniqueId();
    device = info->machineHostName();
    version = QString::number(cutefin_VERSION_MAJOR) + "." + QString::number(cutefin_VERSION_MINOR);
    delete info;
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
    url = QString(_url);
}

void JellyfinApi::setAccessToken(const QString &_accessToken){
    accessToken = QString(_accessToken);
}

const QString& JellyfinApi::getUrl() const {
    return url;
}
const QString& JellyfinApi::getAccessToken() const {
    return accessToken;
}

void JellyfinApi::getLatestMovies(){
    QString endpoint = "/Items?Recursive=true&SortBy=DateCreated&SortOrder=Descending&IncludeItemTypes=Movie&Limit=15";
    QNetworkReply* reply = manager->get(getAuthenticatedRequest(endpoint));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            // Parse the response
            QByteArray data = reply->readAll();
            QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
            QJsonObject jsonObj = jsonDoc.object();
            QJsonArray items = jsonObj["Items"].toArray();

            QList<Movie> movies;
            for (const QJsonValue& value : items) {
                QJsonObject item = value.toObject();
                Movie movie = Movie(item);
                movies.append(movie);
            }

            emit latestMoviesResponse(movies, true);
        } else {
            // Error handling
            emit latestMoviesResponse({}, false, reply->errorString());
        }

        reply->deleteLater();
    });
}

QNetworkRequest JellyfinApi::getAuthenticatedRequest(const QString& endpoint) {
    QNetworkRequest request = QNetworkRequest(QUrl(url + endpoint));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QString authHeader = QString("MediaBrowser Token=\"%1\", Client=\"%2\", Device=\"%3\", DeviceId=\"%4\", Version=\"%5\"")
                             .arg(accessToken)
                             .arg("cutefin")
                             .arg(device)
                             .arg(deviceId)
                             .arg(version);
    request.setRawHeader("Authorization", authHeader.toUtf8());
    return request;
}

void JellyfinApi::login(const QString &user, const QString &password){
    QNetworkRequest request = QNetworkRequest(QUrl(url + "/Users/AuthenticateByName"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QString authHeader = QString("MediaBrowser Client=\"%1\", Device=\"%2\", DeviceId=\"%3\", Version=\"%4\"")
                             .arg("cutefin") // Replace with your app name
                             .arg("PC")           // Replace with your device name
                             .arg("g8row-debian") // Replace with a unique device ID
                             .arg("0.0.1");       // Replace with your app version
    request.setRawHeader("Authorization", authHeader.toUtf8());

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



