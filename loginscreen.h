#ifndef LOGINSCREEN_H
#define LOGINSCREEN_H

#include <QWidget>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QMessageBox>
#include "jellyfinapi.h"

class LoginScreen : public QWidget
{
    Q_OBJECT
public:
    explicit LoginScreen(QWidget *parent = nullptr, JellyfinApi *_jellyfinApi = nullptr);
    ~LoginScreen();
signals:
    void loginComplete(const QString accessToken);
private:
    JellyfinApi *jellyfinApi = nullptr;
    QVBoxLayout *layout;
    QLineEdit *usernameField;
    QLineEdit *passwordField;
    QFormLayout *formLayout;
    QPushButton *login;
    QMessageBox *box;
};

#endif // LOGINSCREEN_H
