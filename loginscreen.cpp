#include "loginscreen.h"

LoginScreen::LoginScreen(QWidget *parent, JellyfinApi *_jellyfinApi)
    : QWidget{parent}
{
    jellyfinApi = _jellyfinApi;
    this->setMaximumSize(300,150);

    layout = new QVBoxLayout();
    usernameField = new QLineEdit();
    passwordField = new QLineEdit();
    passwordField->setEchoMode(QLineEdit::Password);

    formLayout = new QFormLayout();
    formLayout->addRow(tr("username:"), usernameField);
    formLayout->addRow(tr("password:"), passwordField);

    login = new QPushButton("Login", this);

    box = new QMessageBox();
    box->setText("Login Error");
    box->setIcon(QMessageBox::Critical);
    box->setDetailedText("Invalid Credentials or network error.");

    connect(login, &QPushButton::clicked, this, [this](){
        jellyfinApi->login(usernameField->text(), passwordField->text());
    });

    connect(jellyfinApi, &JellyfinApi::loginResponse, this, [this](bool success, const QString &accessToken){
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

LoginScreen::~LoginScreen(){
    delete usernameField;
    delete passwordField;
    delete formLayout;
    delete login;
    delete box;
    delete layout;
}
