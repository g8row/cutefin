#ifndef SETUPWINDOW_H
#define SETUPWINDOW_H

#include <QWidget>
#include "loginscreen.h"
#include "serverselect.h"
#include "jellyfinapi.h"

class SetupWindow : public QWidget
{
    Q_OBJECT
public:
    explicit SetupWindow(QWidget *parent = nullptr);
private:
    ServerSelect *serverSelect = nullptr;
    LoginScreen *loginScreen = nullptr;
    JellyfinApi *jellyfinApi = nullptr;
signals:
};

#endif // SETUPWINDOW_H
