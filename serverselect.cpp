#include "serverselect.h"

#include <QFormLayout>
#include <QPushButton>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>


ServerSelect::ServerSelect(QWidget *parent)
    : QWidget{parent}
{
    QVBoxLayout *layout = new QVBoxLayout();
    QLineEdit *serverField = new QLineEdit();
    QFormLayout *formLayout = new QFormLayout();
    formLayout->addRow(tr("server name:"), serverField);

    QPushButton *addServerButton = new QPushButton("add server");

    QNetworkAccessManager *manager = new QNetworkAccessManager(this);
    connect(manager, &QNetworkAccessManager::finished, this, &ServerSelect::parseInfo);

    reply=manager->get(QNetworkRequest(QUrl("https://jellyfin.g8row.xyz/System/Info/Public")));

    layout->addLayout(formLayout);
    layout->addWidget(addServerButton);
    this->setLayout(layout);
}

void ServerSelect::parseInfo() {
    if (reply->error() == QNetworkReply::NoError) {
        qInfo("success");
        QByteArray data = reply->readAll();
        QJsonDocument jsonDocument = QJsonDocument::fromJson(data);
        qInfo("%s",jsonDocument["Version"].toString().toStdString().c_str());
        /*for (const auto &res : results) {
            QJsonObject obj = res.toObject();
            QString str = obj["Version"].toString();
            qInfo("%s", str.toStdString().c_str());
        }*/
    }
}
