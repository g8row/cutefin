#include "homewindow.h"
#include "setupwindow.h"

#include <QSettings>

HomeWindow::HomeWindow(QWidget *parent)
    : QMainWindow{parent}
{
    QSettings *settings = new QSettings("g8row", "cutefin");
    SetupWindow *setupWindow = nullptr;
    this->show();
    if(!settings->contains("server_name") or !settings->contains("access_token")){
        setupWindow = new SetupWindow(nullptr, settings);
        connect(setupWindow, &SetupWindow::setupComplete, this, [this](){
            this->show();
        });
        this->hide();
    }
}
