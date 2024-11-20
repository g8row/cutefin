#include "setupwindow.h"
#include "loginscreen.h"
#include "serverselect.h"
#include "jellyfinapi.h"

#include <QStackedWidget>
#include <QVBoxLayout>
#include <QDebug>
#include <QSettings>

SetupWindow::SetupWindow(QWidget *parent, QSettings *_settings)
    : QWidget{parent}
{
    jellyfinApi = new JellyfinApi();
    settings = _settings;

    QVBoxLayout *layout = new QVBoxLayout();
    QStackedWidget *stack = new QStackedWidget();
    stack->setMaximumSize(300,100);

    loginScreen = new LoginScreen(this, jellyfinApi);
    serverSelect = new ServerSelect(this, jellyfinApi);
    stack->addWidget(serverSelect);
    stack->addWidget(loginScreen);
    if (!settings->contains("server_name")){
        stack->setCurrentWidget(serverSelect);
    }else{
        jellyfinApi->setUrl(settings->value("server_name").toString());
        stack->setCurrentWidget(loginScreen);
    }
    connect(serverSelect, &ServerSelect::serverSelected, this, [this, stack](const QString &url){
        settings->setValue("server_name", url);
        jellyfinApi->setUrl(url);
        stack->setCurrentWidget(loginScreen);
    });
    connect(loginScreen, &LoginScreen::loginComplete, this, [this](const QString &accessToken){
        settings->setValue("access_token", accessToken);
        this->close();
        emit setupComplete();
    });

    layout->addWidget(stack);
    this->setLayout(layout);
    this->setMinimumSize(300, 300);
    this->setMaximumSize(300, 300);
    this->show();
}
