#pragma once

#include "api/Models.h"

#include <QList>
#include <QMainWindow>
#include <QPointer>

class DetailPage;
class HomePage;
class ImageCache;
class JellyfinClient;
class LibraryPage;
class LoginPage;
class NetworkQueue;
class PlayerWindow;
class ServerSelectPage;
class Settings;
class QStackedWidget;

// Owns the app's single NetworkQueue / JellyfinClient / ImageCache / Settings
// and the QStackedWidget of pages, and routes navigation between them.
//
// navigateTo() cross-fades over 200ms by installing a QGraphicsOpacityEffect
// on the outgoing and incoming pages for the duration and *removing it
// afterwards* -- a permanent QGraphicsEffect forces every repaint through an
// offscreen buffer and would wreck scrolling performance (DESIGN.md §6).
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Decides the launch destination: a persisted session goes straight to
    // HomePage (revalidating in the background), otherwise ServerSelectPage,
    // or LoginPage when a server is known but the token is not.
    void start();

public slots:
    void navigateTo(QWidget *page, bool pushHistory = true);
    void goBack();
    // Routes an activated item to whatever page can show it: a library view
    // opens the grid, a person opens their filmography, everything else opens
    // the detail page.
    void showItem(const MediaItem &item);
    void showDetail(const MediaItem &item);
    void showLibrary(const MediaItem &view);
    void showPerson(const MediaItem &person);
    // `resume` starts from item.userData.playbackPositionTicks.
    void playItem(const MediaItem &item, bool resume);
    void logout();

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void buildPages();
    void applySession(const UserSession &session, bool rememberMe);
    void showItemContextMenu(const MediaItem &item, const QPoint &globalPos);
    void enterHome();

    Settings *m_settings;
    NetworkQueue *m_networkQueue;
    JellyfinClient *m_client;
    ImageCache *m_imageCache;

    QStackedWidget *m_stack;
    ServerSelectPage *m_serverSelectPage;
    LoginPage *m_loginPage;
    HomePage *m_homePage;
    DetailPage *m_detailPage;
    LibraryPage *m_libraryPage;

    QList<QWidget *> m_history;
    QPointer<PlayerWindow> m_player;
    bool m_animating = false;
};
