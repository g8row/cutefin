#include "homewindow.h"
#include "setupwindow.h"

HomeWindow::HomeWindow(QWidget *parent)
    : QMainWindow{parent}
{
    settings = new QSettings("g8row", "cutefin");
    jellyfinApi = new JellyfinApi(this);

    SetupWindow *setupWindow = nullptr;

    mainArea = new QScrollArea(this);
    mainArea->setWidgetResizable(true);
    mainWidget = new QStackedWidget();
    mainArea->setWidget(mainWidget);

    this->setMinimumSize(600,500);
    this->setCentralWidget(mainArea);
    this->show();

    if(!settings->contains("server_name") or !settings->contains("access_token")){
        setupWindow = new SetupWindow(settings, jellyfinApi, nullptr);
        connect(setupWindow, &SetupWindow::setupComplete, this, [this, setupWindow](){
            this->show();
            delete setupWindow;
            createHomeWidget();
        });
        this->hide();
    } else {
        jellyfinApi->setUrl(settings->value("server_name").toString());
        jellyfinApi->setAccessToken(settings->value("access_token").toString());
        createHomeWidget();
    }
}


void HomeWindow::createHomeWidget(){
    homeWidget = new HomeWidget(jellyfinApi, mainWidget);
    connect(homeWidget, &HomeWidget::mediaSelected, this, &HomeWindow::setCurrentWidget);
    mainWidget->addWidget(homeWidget);
}

void HomeWindow::setCurrentWidget(MediaWidget* widget){
    mainWidget->addWidget(widget);
    previousWidget = mainWidget->currentWidget();
    connect(widget, &MediaWidget::done, this, [this, widget](){
        mainWidget->setCurrentWidget(previousWidget);
        delete widget;
    });
    mainWidget->setCurrentWidget(widget);
}


HomeWindow::~HomeWindow(){
    delete jellyfinApi;
    delete settings;
    delete mainWidget;
    delete mainArea;
    delete homeWidget;
}
