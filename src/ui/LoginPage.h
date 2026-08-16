#pragma once

#include "api/Models.h"

#include <QWidget>

class JellyfinClient;
class Settings;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

// Username/password against a server already probed by ServerSelectPage.
// "Remember me" defaults on and controls whether the access token is written
// to disk (Settings::saveSession). Enter in either field submits; errors are
// inline, not modal.
class LoginPage : public QWidget
{
    Q_OBJECT
public:
    LoginPage(JellyfinClient *client, Settings *settings, QWidget *parent = nullptr);

    // Sets the header ("Sign in to <name>") and the server this page will
    // authenticate against.
    void setServer(const ServerInfo &info);

    // Prefills the username (e.g. after an expired token for a known user).
    void setUserName(const QString &userName);

    void takeFocus();

signals:
    void loggedIn(const UserSession &session);
    void changeServerRequested();

private:
    void submit();
    void setBusy(bool busy);
    void showError(const QString &message);

    JellyfinClient *m_client;
    Settings *m_settings;
    ServerInfo m_server;

    QLabel *m_headerLabel;
    QLineEdit *m_userEdit;
    QLineEdit *m_passwordEdit;
    QCheckBox *m_rememberCheck;
    QPushButton *m_signInButton;
    QPushButton *m_changeServerButton;
    QLabel *m_statusLabel;

    bool m_busy = false;
};
