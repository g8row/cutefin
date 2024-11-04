#ifndef SERVERSELECT_H
#define SERVERSELECT_H

#include <QWidget>
#include <QNetworkReply>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QMessageBox>

class ServerSelect : public QWidget
{
    Q_OBJECT
private:
    QNetworkReply *reply;
    QLineEdit *serverField;
    QNetworkAccessManager *manager;
    QMessageBox *box;
public:
    explicit ServerSelect(QWidget *parent = nullptr);
public slots:
    void parseInfo();
    void pingServer();
signals:
};

#endif // SERVERSELECT_H
