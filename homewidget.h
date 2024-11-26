#ifndef HOMEWIDGET_H
#define HOMEWIDGET_H

#include <QWidget>
#include "mediawidget.h"
#include "jellyfinapi.h"

class HomeWidget : public QWidget
{
    Q_OBJECT
public:
    explicit HomeWidget(JellyfinApi* jellyfinApi, QWidget *parent = nullptr);
    ~HomeWidget();
private:
    JellyfinApi* jellyfinApi;
    void showMovieWidget(QWidget *movieWidget);
signals:
    void mediaSelected(MediaWidget *widget);
};

#endif // HOMEWIDGET_H
