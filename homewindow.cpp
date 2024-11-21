#include "homewindow.h"
#include "setupwindow.h"
#include "mainconfig.h"

#include <QSettings>

HomeWindow::HomeWindow(QWidget *parent)
    : QMainWindow{parent}
{
    QSettings *settings = new QSettings("g8row", "cutefin");
    SetupWindow *setupWindow = nullptr;
    JellyfinApi* jellyfinApi = new JellyfinApi(this);
    qDebug() << cutefin_VERSION_MAJOR << "." << cutefin_VERSION_MINOR;
    this->show();
    if(!settings->contains("server_name") or !settings->contains("access_token")){
        setupWindow = new SetupWindow(settings, jellyfinApi, this);
        connect(setupWindow, &SetupWindow::setupComplete, this, [this](){
            this->show();
        });
        this->hide();
    } else {
        jellyfinApi->setUrl(settings->value("server_name").toString());
        jellyfinApi->setAccessToken(settings->value("access_token").toString());
    }

    connect(jellyfinApi, &JellyfinApi::latestMoviesResponse, this, [this](QList<Movie> movies, bool success, const QString &errorMessage = QString()){
        if (success){
            for (auto& i:movies){
                qDebug() << i.getName() << "\n";
            }
        }
    });
    jellyfinApi->getLatestMovies();
}
