#ifndef HOMEWINDOW_H
#define HOMEWINDOW_H

#include <QMainWindow>
#include <QSettings>
#include "jellyfinapi.h"
class HomeWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit HomeWindow(QWidget *parent = nullptr);
    ~HomeWindow();
private:
    JellyfinApi* jellyfinApi;
    QSettings *settings;
signals:
};

#endif // HOMEWINDOW_H
