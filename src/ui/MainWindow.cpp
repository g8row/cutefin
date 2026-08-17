#include "MainWindow.h"

#include "DetailPage.h"
#include "HomePage.h"
#include "LibraryPage.h"
#include "LoginPage.h"
#include "ServerSelectPage.h"
#include "api/JellyfinClient.h"
#include "core/Formatting.h"
#include "core/ImageCache.h"
#include "core/NetworkQueue.h"
#include "core/Settings.h"

#ifdef CUTEFIN_ENABLE_MPV
#include "player/PlayerWindow.h"
#endif

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QGraphicsOpacityEffect>
#include <QKeyEvent>
#include <QMenu>
#include <QPropertyAnimation>
#include <QStackedWidget>
#include <QStatusBar>

namespace {
constexpr int kPageFadeMs = 200;
constexpr const char *kClientVersion = "0.1.0";
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_settings(new Settings(this))
    , m_networkQueue(new NetworkQueue(this))
    , m_client(new JellyfinClient(m_networkQueue, this))
    , m_imageCache(new ImageCache(m_networkQueue, this))
{
    setWindowTitle(QStringLiteral("cutefin"));

    m_client->setDeviceId(m_settings->deviceId());
    m_client->setClientVersion(QString::fromLatin1(kClientVersion));

    // The disk cache can hold a lot of stale posters from previous sessions;
    // pruning is I/O-heavy, so it runs on a worker thread and must never be
    // on the path between launch and the first frame.
    m_imageCache->pruneDiskCacheAsync();

    buildPages();

    const QByteArray geometry = m_settings->windowGeometry();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);
    else
        resize(1280, 800);
}

MainWindow::~MainWindow() = default;

void MainWindow::buildPages()
{
    m_stack = new QStackedWidget(this);
    setCentralWidget(m_stack);

    m_serverSelectPage = new ServerSelectPage(m_client, m_settings, this);
    m_loginPage = new LoginPage(m_client, m_settings, this);
    m_homePage = new HomePage(m_client, m_imageCache, this);
    m_detailPage = new DetailPage(m_client, m_imageCache, this);
    m_libraryPage = new LibraryPage(m_client, m_imageCache, this);

    m_stack->addWidget(m_serverSelectPage);
    m_stack->addWidget(m_loginPage);
    m_stack->addWidget(m_homePage);
    m_stack->addWidget(m_detailPage);
    m_stack->addWidget(m_libraryPage);

    connect(m_serverSelectPage, &ServerSelectPage::serverAccepted, this, [this](const ServerInfo &info) {
        m_loginPage->setServer(info);
        m_loginPage->setUserName(m_settings->loadPersistedSession().userName);
        navigateTo(m_loginPage);
        m_loginPage->takeFocus();
    });

    connect(m_loginPage, &LoginPage::changeServerRequested, this, [this] {
        navigateTo(m_serverSelectPage);
        m_serverSelectPage->takeFocus();
    });

    connect(m_loginPage, &LoginPage::loggedIn, this, [this](const UserSession &session) {
        m_client->setSession(session);
        m_imageCache->setServerUrl(session.serverUrl);
        m_imageCache->setAccessToken(session.token);
        m_homePage->setUserName(session.userName);
        enterHome();
    });

    connect(m_homePage, &HomePage::itemActivated, this, &MainWindow::showItem);
    connect(m_homePage, &HomePage::itemContextMenuRequested, this, &MainWindow::showItemContextMenu);
    connect(m_homePage, &HomePage::logoutRequested, this, &MainWindow::logout);

    connect(m_detailPage, &DetailPage::itemActivated, this, &MainWindow::showItem);
    connect(m_detailPage, &DetailPage::backRequested, this, &MainWindow::goBack);
    connect(m_detailPage, &DetailPage::playRequested, this, &MainWindow::playItem);

    connect(m_libraryPage, &LibraryPage::itemActivated, this, &MainWindow::showItem);
    connect(m_libraryPage, &LibraryPage::itemContextMenuRequested, this, &MainWindow::showItemContextMenu);
    connect(m_libraryPage, &LibraryPage::backRequested, this, &MainWindow::goBack);
}

void MainWindow::start()
{
    const Settings::Session persisted = m_settings->loadPersistedSession();

    if (persisted.isValid()) {
        UserSession session;
        session.serverUrl = persisted.serverUrl;
        session.userId = persisted.userId;
        session.userName = persisted.userName;
        session.token = persisted.token;
        session.deviceId = persisted.deviceId.isEmpty() ? m_settings->deviceId() : persisted.deviceId;

        m_client->setSession(session);
        m_imageCache->setServerUrl(session.serverUrl);
        m_imageCache->setAccessToken(session.token);
        m_homePage->setUserName(session.userName);

        // Go straight to Home rather than blocking on a validation round
        // trip: the first row fetch is itself the validation. If the token
        // has been revoked those fetches fail and the user is dropped back to
        // LoginPage with the server prefilled (DESIGN.md §5).
        navigateTo(m_homePage, false);
        m_homePage->refresh();
        return;
    }

    const QString lastServer = m_settings->serverUrl();
    if (!lastServer.isEmpty()) {
        m_client->setServerUrl(lastServer);
        ServerInfo info;
        info.url = lastServer;
        info.name = QUrl(lastServer).host();
        m_loginPage->setServer(info);
        m_loginPage->setUserName(persisted.userName);
        navigateTo(m_loginPage, false);
        m_loginPage->takeFocus();
        return;
    }

    m_serverSelectPage->setServerUrl(lastServer);
    navigateTo(m_serverSelectPage, false);
    m_serverSelectPage->takeFocus();
}

void MainWindow::enterHome()
{
    m_history.clear();
    navigateTo(m_homePage, false);
    m_homePage->refresh();
}

void MainWindow::navigateTo(QWidget *page, bool pushHistory)
{
    if (!page || m_stack->currentWidget() == page)
        return;

    if (pushHistory)
        m_history.append(m_stack->currentWidget());

    m_stack->setCurrentWidget(page);

    // Only the incoming page is faded. A true two-sided cross-fade would need
    // both pages mapped and stacked simultaneously, which QStackedWidget does
    // not do, and the offscreen buffer a QGraphicsEffect forces is expensive
    // enough that it must not outlive the animation (DESIGN.md §6) -- hence
    // the deleteLater/removal in the finished handler below.
    if (m_animating)
        return;

    auto *effect = new QGraphicsOpacityEffect(page);
    effect->setOpacity(0.0);
    page->setGraphicsEffect(effect);

    auto *animation = new QPropertyAnimation(effect, "opacity", this);
    animation->setDuration(kPageFadeMs);
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    animation->setEasingCurve(QEasingCurve::InOutCubic);

    m_animating = true;
    connect(animation, &QPropertyAnimation::finished, this, [this, page] {
        page->setGraphicsEffect(nullptr);
        m_animating = false;
    });
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::goBack()
{
    if (m_history.isEmpty())
        return;
    QWidget *previous = m_history.takeLast();
    navigateTo(previous, false);
}

void MainWindow::showItem(const MediaItem &item)
{
    if (item.type == QStringLiteral("CollectionFolder") || item.type == QStringLiteral("UserView")) {
        showLibrary(item);
        return;
    }
    if (item.type == QStringLiteral("Person")) {
        showPerson(item);
        return;
    }
    showDetail(item);
}

void MainWindow::showDetail(const MediaItem &item)
{
    m_detailPage->setItem(item);
    navigateTo(m_detailPage);
}

void MainWindow::showLibrary(const MediaItem &view)
{
    m_libraryPage->showLibrary(view);
    navigateTo(m_libraryPage);
}

void MainWindow::showPerson(const MediaItem &person)
{
    JellyfinClient::ItemQuery query;
    query.personId = person.id;
    query.includeItemTypes = {QStringLiteral("Movie"), QStringLiteral("Series")};
    // A filmography reads best newest-first; alphabetical buries recent work.
    query.sortBy = QStringLiteral("PremiereDate");
    query.ascending = false;

    m_libraryPage->showQuery(person.name, query);
    navigateTo(m_libraryPage);
}

void MainWindow::playItem(const MediaItem &item, bool resume)
{
    const qint64 startSeconds = resume ? Formatting::ticksToSeconds(item.userData.playbackPositionTicks) : 0;

#ifdef CUTEFIN_ENABLE_MPV
    m_client->getPlaybackInfo(item.id, this,
                              [this, item, startSeconds](const JellyfinClient::PlaybackResolution &resolution,
                                                          const QString &error) {
                                  if (!resolution.ok) {
                                      statusBar()->showMessage(
                                          tr("Cannot play %1: %2").arg(item.name, error), 5000);
                                      return;
                                  }

                                  if (!m_player)
                                      m_player = new PlayerWindow(m_client, nullptr);

                                  m_player->setAttribute(Qt::WA_DeleteOnClose);
                                  m_player->setWindowTitle(item.name);
                                  m_player->resize(1280, 720);
                                  m_player->playItem(item.id, resolution.playSessionId, resolution.mediaSourceId,
                                                     resolution.url, startSeconds);
                                  m_player->show();
                                  m_player->raise();
                                  m_player->activateWindow();

                                  // Whatever was watched changes Continue Watching and the
                                  // item's own resume position, so both pages are stale the
                                  // moment the player closes.
                                  connect(m_player, &QObject::destroyed, this, [this] {
                                      m_homePage->refresh();
                                  });
                              });
#else
    Q_UNUSED(startSeconds);
    statusBar()->showMessage(tr("This build has no player: rebuild with libmpv available."), 5000);
#endif
}

void MainWindow::showItemContextMenu(const MediaItem &item, const QPoint &globalPos)
{
    QMenu menu(this);

    const bool playable = item.type == QStringLiteral("Movie") || item.type == QStringLiteral("Episode");
    if (playable) {
        menu.addAction(tr("Play"), this, [this, item] { playItem(item, false); });
        if (item.userData.playbackPositionTicks > 0) {
            const qint64 seconds = Formatting::ticksToSeconds(item.userData.playbackPositionTicks);
            menu.addAction(tr("Resume from %1").arg(Formatting::formatPosition(seconds)), this,
                            [this, item] { playItem(item, true); });
        }
        menu.addSeparator();
    }

    menu.addAction(tr("Show details"), this, [this, item] { showDetail(item); });

    if (!item.seriesId.isEmpty()) {
        menu.addAction(tr("Go to series"), this, [this, item] {
            m_client->fetchItem(item.seriesId, this, [this](bool ok, const MediaItem &series, const QString &) {
                if (ok)
                    showDetail(series);
            });
        });
    }

    menu.exec(globalPos);
}

void MainWindow::logout()
{
    m_client->logout(this);
    m_client->clearSession();
    m_settings->clearSession();
    m_imageCache->setAccessToken(QString());

    m_history.clear();

    ServerInfo info;
    info.url = m_settings->serverUrl();
    info.name = QUrl(info.url).host();
    m_loginPage->setServer(info);
    navigateTo(m_loginPage, false);
    m_loginPage->takeFocus();
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    // App-wide shortcuts (FEATURES.md C6). Escape only goes back when there
    // is somewhere to go, so it still reaches child widgets otherwise.
    if (event->key() == Qt::Key_Escape && !m_history.isEmpty()) {
        goBack();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Slash && m_stack->currentWidget() == m_homePage) {
        m_homePage->focusSearch();
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_settings->setWindowGeometry(saveGeometry());
#ifdef CUTEFIN_ENABLE_MPV
    // Closing the player is what makes it report a final Stopped position;
    // leaving it open would strand the server-side session.
    if (m_player)
        m_player->close();
#endif
    QMainWindow::closeEvent(event);
}
