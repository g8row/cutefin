#ifndef SERVERSELECT_H
#define SERVERSELECT_H

#include <QWidget>
#include <QNetworkReply>

class ServerSelect : public QWidget
{
    Q_OBJECT
private:
    QNetworkReply *reply;
public:
    explicit ServerSelect(QWidget *parent = nullptr);
public slots:
    void parseInfo();
signals:
};

#endif // SERVERSELECT_H
