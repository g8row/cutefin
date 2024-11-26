#ifndef MEDIAWIDGET_H
#define MEDIAWIDGET_H

#include <QWidget>

class MediaWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MediaWidget(QWidget* parent = nullptr) : QWidget{parent} {}
signals:
    void done();
};

#endif // MEDIAWIDGET_H
