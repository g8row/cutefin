#ifndef SETUPWINDOW_H
#define SETUPWINDOW_H

#include <QWidget>
#include <QSettings>
#include "loginscreen.h"
#include "serverselect.h"
#include "jellyfinapi.h"


class SetupWindow : public QWidget
{
    Q_OBJECT
public:
    explicit SetupWindow(QWidget *parent = nullptr, QSettings *_settings = nullptr);
private:
    ServerSelect *serverSelect = nullptr;
    LoginScreen *loginScreen = nullptr;
    JellyfinApi *jellyfinApi = nullptr;
    QSettings *settings = nullptr;
signals:
    void setupComplete();
};

#endif // SETUPWINDOW_H
