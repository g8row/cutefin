#include "setupwindow.h"
#include "loginscreen.h"
#include "serverselect.h"
#include "jellyfinapi.h"

#include <QVBoxLayout>
#include <QDebug>

SetupWindow::SetupWindow(QWidget *parent)
    : QWidget{parent}
{
    jellyfinApi = new JellyfinApi();

    QVBoxLayout *layout = new QVBoxLayout();
    serverSelect = new ServerSelect(this, jellyfinApi);
    loginScreen = nullptr;

    connect(serverSelect, &ServerSelect::serverSelected, this, [this](){
        serverSelect->hide();
        loginScreen = new LoginScreen(this, jellyfinApi);
        this->layout()->addWidget(loginScreen);
    });

    layout->addWidget(serverSelect);
    //layout->addWidget(loginScreen);
    //loginScreen->hide();
    this->setLayout(layout);
    this->setMinimumSize(300, 300);
    this->setMaximumSize(300,300);
    this->show();
}
