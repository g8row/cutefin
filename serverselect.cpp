#include "serverselect.h"

#include <QFormLayout>
#include <QPushButton>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QDebug>
#include <QMessageBox>
#include "jellyfinapi.h"


ServerSelect::ServerSelect(QWidget *parent, JellyfinApi *_jellyfinApi)
    : QWidget{parent}
{

    jellyfinApi = _jellyfinApi;

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

    connect(jellyfinApi, &JellyfinApi::pingServerResponse, this, [this](bool status, const QString &message = ""){
        if (status) {
            emit serverSelected(serverField->text());
        } else {
            box->show();
        }
    });

    connect(addServerButton, &QPushButton::clicked, this, [this](){
        jellyfinApi->pingServer(serverField->text() + "/System/Info/Public");
    });

    this->setLayout(layout);
}
