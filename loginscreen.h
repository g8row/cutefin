#ifndef LOGINSCREEN_H
#define LOGINSCREEN_H

#include <QWidget>
#include "jellyfinapi.h"

class LoginScreen : public QWidget
{
    Q_OBJECT
public:
    explicit LoginScreen(QWidget *parent = nullptr, JellyfinApi *_jellyfinApi = nullptr);
signals:
    void loginComplete(const QString accessToken);
private:
    JellyfinApi *jellyfinApi = nullptr;
};

#endif // LOGINSCREEN_H
