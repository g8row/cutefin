#ifndef SERVERSELECT_H
#define SERVERSELECT_H

#include <QWidget>
#include <QLineEdit>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include "jellyfinapi.h"

class ServerSelect : public QWidget
{
    Q_OBJECT
private:
    QLineEdit *serverField;
    QMessageBox *box;
    JellyfinApi *jellyfinApi;
    QVBoxLayout *layout;
    QFormLayout *formLayout;
    QPushButton *addServerButton;
public:
    explicit ServerSelect(QWidget *parent = nullptr, JellyfinApi *_jellyfinApi = nullptr);
    ~ServerSelect();
public slots:
signals:
    void serverSelected(const QString url);
};

#endif // SERVERSELECT_H
