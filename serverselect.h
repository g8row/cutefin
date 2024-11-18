#ifndef SERVERSELECT_H
#define SERVERSELECT_H

#include <QWidget>
#include <QNetworkReply>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QMessageBox>
#include "jellyfinapi.h"

class ServerSelect : public QWidget
{
    Q_OBJECT
private:
    QNetworkReply *reply;
    QLineEdit *serverField;
    QMessageBox *box;
    JellyfinApi *jellyfinApi;
public:
    explicit ServerSelect(QWidget *parent = nullptr, JellyfinApi *_jellyfinApi = nullptr);
public slots:
signals:
    void serverSelected(const QString url);
};

#endif // SERVERSELECT_H
