#include "loginscreen.h"

#include <QFormLayout>
#include <QLineEdit>

LoginScreen::LoginScreen(QWidget *parent)
    : QWidget{parent}
{
    QLineEdit *usernameField = new QLineEdit();
    QLineEdit *passwordField = new QLineEdit();
    QFormLayout *formLayout = new QFormLayout(this);
    formLayout->addRow(tr("username:"), usernameField);
    formLayout->addRow(tr("pasword:"), passwordField);
    this->setLayout(formLayout);
}
