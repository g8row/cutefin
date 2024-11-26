#ifndef HOMEWINDOW_H
#define HOMEWINDOW_H

#include <QMainWindow>
#include <QSettings>
#include <QScrollArea>
#include <QStackedWidget>
#include "jellyfinapi.h"
#include "homewidget.h"
#include "mediawidget.h"

class HomeWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit HomeWindow(QWidget *parent = nullptr);
    ~HomeWindow();
private:
    JellyfinApi* jellyfinApi;
    QSettings *settings;
    QScrollArea *mainArea;
    QStackedWidget *mainWidget;
    HomeWidget *homeWidget;

    QWidget *previousWidget;

    void createHomeWidget();
signals:
public slots:
    void setCurrentWidget(MediaWidget* widget);
};

#endif // HOMEWINDOW_H
