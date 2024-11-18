#include "loginscreen.h"
#include "jellyfinapi.h"

#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>

LoginScreen::LoginScreen(QWidget *parent, JellyfinApi *jellyfinApi)
    : QWidget{parent}
{
    this->setMaximumSize(300,150);

    QVBoxLayout *layout = new QVBoxLayout();
    QLineEdit *usernameField = new QLineEdit();
    QLineEdit *passwordField = new QLineEdit();

    QFormLayout *formLayout = new QFormLayout();
    formLayout->addRow(tr("username:"), usernameField);
    formLayout->addRow(tr("password:"), passwordField);

    QPushButton *login = new QPushButton("Login", this);

    layout->addLayout(formLayout);
    layout->addWidget(login);
    this->setLayout(layout);
}
