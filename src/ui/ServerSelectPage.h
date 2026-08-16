#pragma once

#include "api/Models.h"

#include <QWidget>

class JellyfinClient;
class Settings;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

// First-run screen: a URL field, a "Connect" button and a list of recently
// used servers. Input is normalized before probing (adds https:// when no
// scheme is present, strips a trailing slash), then GET /System/Info/Public
// confirms the server and its name + version are shown inline. Failures are
// shown inline too -- never in a modal (DESIGN.md §5).
class ServerSelectPage : public QWidget
{
    Q_OBJECT
public:
    ServerSelectPage(JellyfinClient *client, Settings *settings, QWidget *parent = nullptr);

    // Prefills the URL field (e.g. with the last used server on a 401).
    void setServerUrl(const QString &url);

    // Puts keyboard focus where the user is expected to start typing.
    void takeFocus();

    // Turns "jellyfin.example.com/" into "https://jellyfin.example.com".
    // Returns an empty string for input that cannot be a server URL.
    static QString normalizeServerUrl(const QString &input);

signals:
    // The server probed successfully and the user should move on to login.
    void serverAccepted(const ServerInfo &info);

private:
    void connectToServer();
    void setBusy(bool busy);
    void showError(const QString &message);
    void showStatus(const QString &message);
    void reloadRecentServers();

    JellyfinClient *m_client;
    Settings *m_settings;

    QLineEdit *m_urlEdit;
    QPushButton *m_connectButton;
    QLabel *m_statusLabel;
    QListWidget *m_recentList;
    QLabel *m_recentLabel;

    bool m_busy = false;
};
