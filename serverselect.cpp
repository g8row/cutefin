#include "serverselect.h"

#include <QFormLayout>
#include <QPushButton>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QMessageBox>


ServerSelect::ServerSelect(QWidget *parent)
    : QWidget{parent}
{
    manager = new QNetworkAccessManager(this);

    this->setMaximumSize(300,100);
    QVBoxLayout *layout = new QVBoxLayout();
    serverField = new QLineEdit();
    QFormLayout *formLayout = new QFormLayout();
    formLayout->addRow(tr("server name:"), serverField);
    QPushButton *addServerButton = new QPushButton("add server");

    box = new QMessageBox();
    box->setText("Network Error");
    box->setIcon(QMessageBox::Critical);
    box->setDetailedText("Invalid Server Address or Server is offline");

    layout->addLayout(formLayout);
    layout->addWidget(addServerButton);

    connect(addServerButton, &QPushButton::clicked, this, &ServerSelect::pingServer);

    this->setLayout(layout);
}

void ServerSelect::pingServer(){
    connect(manager, &QNetworkAccessManager::finished, this, &ServerSelect::parseInfo);
    reply=manager->get(QNetworkRequest(QUrl(serverField->text())));
}

void ServerSelect::parseInfo() {
    if (reply->error() == QNetworkReply::NoError) {
        qInfo("success");
        QByteArray data = reply->readAll();
        QJsonDocument jsonDocument = QJsonDocument::fromJson(data);
        qInfo("%s",jsonDocument["Version"].toString().toStdString().c_str());
        close();
    } else {
        box->show();
    }
}
