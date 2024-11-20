#include "loginscreen.h"
#include "jellyfinapi.h"

#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QMessageBox>

LoginScreen::LoginScreen(QWidget *parent, JellyfinApi *_jellyfinApi)
    : QWidget{parent}
{
    jellyfinApi = _jellyfinApi;
    this->setMaximumSize(300,150);

    QVBoxLayout *layout = new QVBoxLayout();
    QLineEdit *usernameField = new QLineEdit();
    QLineEdit *passwordField = new QLineEdit();
    passwordField->setEchoMode(QLineEdit::Password);

    QFormLayout *formLayout = new QFormLayout();
    formLayout->addRow(tr("username:"), usernameField);
    formLayout->addRow(tr("password:"), passwordField);

    QPushButton *login = new QPushButton("Login", this);

    QMessageBox *box = new QMessageBox();
    box->setText("Login Error");
    box->setIcon(QMessageBox::Critical);
    box->setDetailedText("Invalid Credentials or network error.");

    connect(login, &QPushButton::clicked, this, [this, usernameField, passwordField](){
        jellyfinApi->login(usernameField->text(), passwordField->text());
    });

    connect(jellyfinApi, &JellyfinApi::loginResponse, this, [this, box](bool success, const QString &accessToken){
        if (success) {
            emit loginComplete(accessToken);
        } else {
            box->show();
        }
    });

    layout->addLayout(formLayout);
    layout->addWidget(login);
    this->setLayout(layout);
}
