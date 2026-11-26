#include "app/AppController.h"

#include "app/HomeSectionModel.h"
#include "app/MediaItemModel.h"
#include "core/Formatting.h"
#include "core/ImageCache.h"
#include "core/NetworkQueue.h"
#include "core/Settings.h"
#include "player/PlayerController.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <memory>

namespace {

constexpr const char *kClientVersion = "0.1.0";
constexpr int kLibraryPageSize = 60;
constexpr int kCollectionLimit = 200;
constexpr int kSearchDebounceMs = 250;
constexpr int kQuickConnectPollMs = 2000;
constexpr int kQuickConnectMaxPolls = 150; // 5 minutes
constexpr int kPostPlaybackRefreshMs = 400;
constexpr int kMaxHistory = 64;
constexpr int kMinRecentSearchLength = 2;
constexpr qint64 kDualCoreImageMemoryBytes = 32LL * 1024 * 1024;

// Dynamic QObject properties for small bits of bookkeeping.
constexpr const char *kAuthGenerationProperty = "_authGeneration";
constexpr const char *kLastPlayedProperty = "_lastPlayedItemId";

// Bumping the generation orphans every auth reply still in flight.
int nextAuthGeneration(QObject *owner)
{
    const int generation = owner->property(kAuthGenerationProperty).toInt() + 1;
    owner->setProperty(kAuthGenerationProperty, generation);
    return generation;
}

bool isCurrentAuthGeneration(const QObject *owner, int generation)
{
    return owner->property(kAuthGenerationProperty).toInt() == generation;
}

bool isUnauthorizedError(const QString &error)
{
    return error.contains(QLatin1String("server replied: Unauthorized"), Qt::CaseInsensitive)
        || error.contains(QLatin1String("requires authentication"), Qt::CaseInsensitive);
}

bool isPlayableType(const QString &type)
{
    return type == QLatin1String("Movie") || type == QLatin1String("Episode") || type == QLatin1String("Video")
        || type == QLatin1String("MusicVideo");
}

bool isVideoView(const MediaItem &view)
{
    return view.collectionType.isEmpty() || view.collectionType == QLatin1String("movies")
        || view.collectionType == QLatin1String("tvshows") || view.collectionType == QLatin1String("boxsets");
}

QStringList itemTypesForCollection(const QString &collectionType)
{
    if (collectionType == QLatin1String("movies"))
        return {QStringLiteral("Movie")};
    if (collectionType == QLatin1String("tvshows"))
        return {QStringLiteral("Series")};
    if (collectionType == QLatin1String("boxsets"))
        return {QStringLiteral("BoxSet")};
    if (collectionType.isEmpty())
        return {QStringLiteral("Movie"), QStringLiteral("Series"), QStringLiteral("Video")};
    return {}; // browse everything under the parent
}

// Everything but the paging window.
bool sameQuery(const JellyfinClient::ItemQuery &a, const JellyfinClient::ItemQuery &b)
{
    return a.parentId == b.parentId && a.includeItemTypes == b.includeItemTypes && a.sortBy == b.sortBy
        && a.ascending == b.ascending && a.genres == b.genres && a.officialRatings == b.officialRatings
        && a.years == b.years && a.personId == b.personId && a.searchTerm == b.searchTerm
        && a.nameStartsWith == b.nameStartsWith && a.played == b.played && a.favoritesOnly == b.favoritesOnly;
}

QList<MediaItemModel *> homeItemModels(const HomeSectionModel *sections)
{
    QList<MediaItemModel *> models;
    for (int row = 0; row < sections->rowCount(); ++row) {
        const QVariant value = sections->data(sections->index(row), HomeSectionModel::ItemsModelRole);
        if (auto *model = value.value<MediaItemModel *>())
            models.append(model);
    }
    return models;
}

QList<MediaItemModel *> allItemModels(const AppController *app)
{
    QList<MediaItemModel *> models = homeItemModels(app->homeSectionsModel());
    models << app->librariesModel() << app->libraryModel() << app->seasonsModel() << app->episodesModel()
           << app->searchMoviesModel() << app->searchSeriesModel() << app->searchEpisodesModel()
           << app->searchPeopleModel();
    return models;
}

// Overwrites one item's user data wherever it is listed. The ids do not change,
// so MediaItemModel applies this in place.
void replaceUserData(const AppController *app, const QString &itemId, const UserData &userData)
{
    const QList<MediaItemModel *> models = allItemModels(app);
    for (MediaItemModel *model : models) {
        if (!model->itemById(itemId))
            continue;
        QList<MediaItem> items = model->items();
        for (MediaItem &item : items) {
            if (item.id == itemId)
                item.userData = userData;
        }
        model->setItems(items);
    }
}

// The hero banner: the first item with artwork from Continue Watching, then
// from the Latest rows.
QVariantMap pickHeroItem(const HomeSectionModel *sections, bool resumeOnly)
{
    for (int row = 0; row < sections->rowCount(); ++row) {
        const QModelIndex index = sections->index(row);
        const QString kind = sections->data(index, HomeSectionModel::KindRole).toString();
        if (kind != QLatin1String("resume") && (resumeOnly || kind != QLatin1String("latest")))
            continue;
        auto *model = sections->data(index, HomeSectionModel::ItemsModelRole).value<MediaItemModel *>();
        if (!model)
            continue;
        for (const MediaItem &item : model->items()) {
            if (!item.backdropTags.isEmpty() || !item.parentBackdropTags.isEmpty())
                return MediaItemModel::toVariantMap(item);
        }
    }
    return QVariantMap();
}

// First unwatched regular season, else the first regular one, else whatever
// there is ("Specials" is season 0).
QString pickSeasonId(const QList<MediaItem> &seasons)
{
    for (const MediaItem &season : seasons) {
        if (season.indexNumber != 0 && !season.userData.played)
            return season.id;
    }
    for (const MediaItem &season : seasons) {
        if (season.indexNumber != 0)
            return season.id;
    }
    return seasons.isEmpty() ? QString() : seasons.first().id;
}

QString resolutionBadge(const MediaStream &video)
{
    // Width is checked too so that letterboxed encodes keep their class.
    if (video.width >= 3800 || video.height >= 2100)
        return QStringLiteral("4K");
    if (video.width >= 2500 || video.height >= 1400)
        return QStringLiteral("1440p");
    if (video.width >= 1900 || video.height >= 1000)
        return QStringLiteral("1080p");
    if (video.width >= 1260 || video.height >= 700)
        return QStringLiteral("720p");
    return video.height > 0 ? QStringLiteral("SD") : QString();
}

QString videoCodecBadge(const QString &codec)
{
    const QString lower = codec.toLower();
    if (lower == QLatin1String("h264"))
        return QStringLiteral("H.264");
    if (lower == QLatin1String("mpeg2video"))
        return QStringLiteral("MPEG-2");
    if (lower == QLatin1String("mpeg4"))
        return QStringLiteral("MPEG-4");
    return codec.toUpper();
}

QString audioBadge(const MediaStream &audio)
{
    const QString lower = audio.codec.toLower();
    QString codec = audio.codec.toUpper();
    if (lower == QLatin1String("ac3"))
        codec = QStringLiteral("Dolby Digital");
    else if (lower == QLatin1String("eac3"))
        codec = QStringLiteral("Dolby Digital+");
    else if (lower == QLatin1String("truehd"))
        codec = QStringLiteral("TrueHD");
    else if (lower == QLatin1String("opus"))
        codec = QStringLiteral("Opus");

    QString layout;
    if (audio.channels == 1)
        layout = QStringLiteral("Mono");
    else if (audio.channels == 2)
        layout = QStringLiteral("Stereo");
    else if (audio.channels == 6)
        layout = QStringLiteral("5.1");
    else if (audio.channels == 8)
        layout = QStringLiteral("7.1");
    else if (audio.channels > 0)
        layout = QStringLiteral("%1ch").arg(audio.channels);

    return QStringList({codec, layout}).join(QLatin1Char(' ')).trimmed();
}

QString formatBytes(qint64 bytes)
{
    const double mebibytes = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (mebibytes >= 1024.0)
        return QStringLiteral("%1 GB").arg(mebibytes / 1024.0, 0, 'f', 1);
    if (bytes <= 0)
        return QStringLiteral("0 MB");
    return QStringLiteral("%1 MB").arg(mebibytes, 0, 'f', 1);
}

} // namespace

AppController::AppController(QObject *parent)
    : QObject(parent)
    , m_settings(new Settings(this))
    , m_networkQueue(new NetworkQueue(this))
    , m_client(new JellyfinClient(m_networkQueue, this))
    , m_imageCache(new ImageCache(m_networkQueue, this))
    , m_player(new PlayerController(m_client, m_networkQueue, m_settings, this))
    , m_quickConnectTimer(new QTimer(this))
    , m_librariesModel(new MediaItemModel(this))
    , m_homeSectionsModel(new HomeSectionModel(this))
    , m_libraryModel(new MediaItemModel(this))
    , m_seasonsModel(new MediaItemModel(this))
    , m_episodesModel(new MediaItemModel(this))
    , m_searchDebounceTimer(new QTimer(this))
    , m_searchMoviesModel(new MediaItemModel(this))
    , m_searchSeriesModel(new MediaItemModel(this))
    , m_searchEpisodesModel(new MediaItemModel(this))
    , m_searchPeopleModel(new MediaItemModel(this))
{
    m_client->setDeviceId(m_settings->deviceId());
    m_client->setClientVersion(QString::fromLatin1(kClientVersion));
    m_imageCache->setMemoryLimitBytes(m_settings->dualCoreMode() ? kDualCoreImageMemoryBytes
                                                                 : ImageCache::kMaxMemoryCacheBytes);

    m_quickConnectTimer->setInterval(kQuickConnectPollMs);
    connect(m_quickConnectTimer, &QTimer::timeout, this, &AppController::pollQuickConnectTick);

    m_searchDebounceTimer->setSingleShot(true);
    m_searchDebounceTimer->setInterval(kSearchDebounceMs);
    connect(m_searchDebounceTimer, &QTimer::timeout, this, [this]() { executeSearch(m_searchQuery.trimmed()); });

    connect(m_player, &PlayerController::itemChanged, this, [this]() {
        if (!m_player->itemId().isEmpty())
            setProperty(kLastPlayedProperty, m_player->itemId());
    });

    connect(m_player, &PlayerController::closed, this, [this]() {
        // Give the server a moment to record the final "stopped" position
        // before asking it what changed.
        QTimer::singleShot(kPostPlaybackRefreshMs, this, [this]() {
            if (!m_authenticated)
                return;
            refreshHome();
            if (m_currentView == QLatin1String("detail") && !m_currentDetailItemId.isEmpty())
                loadDetailItem(m_currentDetailItemId);

            const QString playedId = property(kLastPlayedProperty).toString();
            if (playedId.isEmpty())
                return;
            m_client->fetchItem(playedId, this, [this](bool ok, const MediaItem &item, const QString &) {
                if (ok && m_authenticated)
                    replaceUserData(this, item.id, item.userData);
            });
        });
    });

    if (QCoreApplication *application = QCoreApplication::instance()) {
        connect(application, &QCoreApplication::aboutToQuit, this, [this]() {
            if (!m_player)
                return;
            // Closing the main window has usually stopped playback already.
            if (m_player->isOpen())
                m_player->stopAndClose();
            // Best effort: let the final playback report leave the process.
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 250);
        });
    }
}

AppController::~AppController()
{
    // Tear down in dependency order while this object is still whole: the
    // player and image cache use the settings, client and queue, and deleting
    // the queue discards in-flight replies before their callbacks could run
    // against a half-destroyed controller.
    disconnect(m_player, nullptr, this, nullptr);
    delete m_player;
    m_player = nullptr;
    delete m_imageCache;
    m_imageCache = nullptr;
    delete m_networkQueue;
    m_networkQueue = nullptr;
}

void AppController::start()
{
    m_imageCache->pruneDiskCacheAsync();

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

        m_authenticated = true;
        m_serverUrl = session.serverUrl;
        m_serverName = QUrl(session.serverUrl).host();
        m_userName = session.userName;
        emit authChanged();
        emit serverChanged();

        // No blocking validation round trip: the first Home fetch is the
        // validation, and an Unauthorized reply drops back to the login page.
        pushAndNavigate({QStringLiteral("home"), QStringLiteral("home"), QStringLiteral("Home"), {}, {}}, false);
        refreshHome();
    } else if (!m_settings->serverUrl().isEmpty()) {
        m_serverUrl = m_settings->serverUrl();
        m_serverName = QUrl(m_serverUrl).host();
        m_client->setServerUrl(m_serverUrl);
        m_imageCache->setServerUrl(m_serverUrl);
        emit serverChanged();
        pushAndNavigate({QStringLiteral("login"), QStringLiteral("home"), QStringLiteral("Sign In"), {}, {}}, false);
    } else {
        pushAndNavigate({QStringLiteral("serverSelect"), QStringLiteral("home"), QStringLiteral("Connect"), {}, {}},
                        false);
        return;
    }

    const QString url = m_serverUrl;
    m_client->probeServer(url, this, [this, url](bool ok, const ServerInfo &info, const QString &) {
        if (!ok || url != m_serverUrl)
            return;
        if (!info.name.isEmpty())
            m_serverName = info.name;
        m_serverVersion = info.version;
        emit serverChanged();
    });
}

QStringList AppController::recentServers() const
{
    return m_settings->recentServers();
}

// --- Auth ------------------------------------------------------------------

QString AppController::normalizeServerUrl(const QString &raw)
{
    QString url = raw.trimmed();
    if (url.isEmpty())
        return QString();
    if (!url.contains(QLatin1String("://")))
        url.prepend(QLatin1String("https://"));
    while (url.endsWith(QLatin1Char('/')))
        url.chop(1);

    const QUrl parsed(url);
    if (!parsed.isValid() || parsed.host().isEmpty())
        return QString();
    return url;
}

void AppController::probeServer(const QString &rawUrl)
{
    cancelQuickConnect();
    const int generation = nextAuthGeneration(this);

    const QString url = normalizeServerUrl(rawUrl);
    if (url.isEmpty()) {
        m_authBusy = false;
        m_authError = tr("Enter a valid server address.");
        emit authChanged();
        return;
    }

    m_authBusy = true;
    m_authError.clear();
    emit authChanged();

    m_client->probeServer(url, this, [this, url, generation](bool ok, const ServerInfo &info, const QString &error) {
        if (!isCurrentAuthGeneration(this, generation))
            return;
        m_authBusy = false;
        if (!ok || info.version.isEmpty()) {
            m_authError = ok ? tr("That address does not look like a Jellyfin server.")
                             : tr("Could not reach the server: %1").arg(error);
            emit authChanged();
            return;
        }

        m_serverUrl = url;
        m_serverName = info.name.isEmpty() ? QUrl(url).host() : info.name;
        m_serverVersion = info.version;
        m_client->setServerUrl(url);
        m_imageCache->setServerUrl(url);
        m_settings->setServerUrl(url);
        m_settings->addRecentServer(url);
        emit serverChanged();
        emit authChanged();
        m_history.clear();
        pushAndNavigate({QStringLiteral("login"), QStringLiteral("home"), QStringLiteral("Sign In"), {}, {}}, false);
    });
}

void AppController::login(const QString &username, const QString &password, bool rememberMe)
{
    if (m_serverUrl.isEmpty()) {
        showServerSelect();
        return;
    }

    cancelQuickConnect();
    const int generation = nextAuthGeneration(this);

    const QString user = username.trimmed();
    if (user.isEmpty()) {
        m_authBusy = false;
        m_authError = tr("Enter your username.");
        emit authChanged();
        return;
    }

    m_authBusy = true;
    m_authError.clear();
    emit authChanged();

    m_client->setServerUrl(m_serverUrl);
    m_client->login(user, password, this,
                    [this, rememberMe, generation](bool ok, const UserSession &session, const QString &error) {
                        if (!isCurrentAuthGeneration(this, generation))
                            return;
                        m_authBusy = false;
                        if (!ok) {
                            m_authError = isUnauthorizedError(error) ? tr("Invalid username or password.")
                                                                     : tr("Sign in failed: %1").arg(error);
                            emit authChanged();
                            return;
                        }
                        enterAuthenticatedSession(session, rememberMe);
                    });
}

void AppController::enterAuthenticatedSession(const UserSession &session, bool rememberMe)
{
    UserSession active = session;
    if (active.deviceId.isEmpty())
        active.deviceId = m_settings->deviceId();

    m_client->setSession(active);
    m_imageCache->setServerUrl(active.serverUrl);
    m_imageCache->setAccessToken(active.token);

    Settings::Session stored;
    stored.serverUrl = active.serverUrl;
    stored.userId = active.userId;
    stored.userName = active.userName;
    stored.token = active.token;
    stored.deviceId = active.deviceId;
    m_settings->saveSession(stored, rememberMe);
    m_settings->addRecentServer(active.serverUrl);

    cancelQuickConnect();
    m_authenticated = true;
    m_authBusy = false;
    m_authError.clear();
    m_serverUrl = active.serverUrl;
    m_userName = active.userName;
    emit authChanged();
    emit serverChanged();

    m_history.clear();
    pushAndNavigate({QStringLiteral("home"), QStringLiteral("home"), QStringLiteral("Home"), {}, {}}, false);
    refreshHome();
}

void AppController::startQuickConnect()
{
    if (m_serverUrl.isEmpty()) {
        showServerSelect();
        return;
    }

    cancelQuickConnect();
    const int generation = nextAuthGeneration(this);

    m_authBusy = false;
    m_authError.clear();
    m_quickConnectActive = true;
    emit authChanged();
    emit quickConnectChanged();

    m_client->setServerUrl(m_serverUrl);
    m_client->initiateQuickConnect(
        this, [this, generation](bool ok, const QString &secret, const QString &code, const QString &error) {
            if (!isCurrentAuthGeneration(this, generation))
                return;
            if (!ok) {
                cancelQuickConnect();
                m_authError = tr("Quick Connect is unavailable: %1").arg(error);
                emit authChanged();
                return;
            }
            m_quickConnectSecret = secret;
            m_quickConnectCode = code;
            m_quickConnectPolls = 0;
            m_quickConnectTimer->start();
            emit quickConnectChanged();
        });
}

void AppController::pollQuickConnectTick()
{
    if (!m_quickConnectActive || m_quickConnectSecret.isEmpty()) {
        m_quickConnectTimer->stop();
        return;
    }

    if (++m_quickConnectPolls > kQuickConnectMaxPolls) {
        cancelQuickConnect();
        m_authError = tr("The Quick Connect code expired. Try again.");
        emit authChanged();
        return;
    }

    const QString secret = m_quickConnectSecret;
    m_client->pollQuickConnect(secret, this, [this, secret](bool ok, bool authenticated, const QString &) {
        // A stopped timer means an earlier poll already succeeded. Failed
        // polls are treated as transient and retried until the timeout.
        if (secret != m_quickConnectSecret || !m_quickConnectTimer->isActive() || !ok || !authenticated)
            return;

        m_quickConnectTimer->stop();
        const int generation = nextAuthGeneration(this);
        m_authBusy = true;
        emit authChanged();

        m_client->authenticateWithQuickConnect(
            secret, this, [this, generation](bool authOk, const UserSession &session, const QString &error) {
                if (!isCurrentAuthGeneration(this, generation))
                    return;
                if (!authOk) {
                    cancelQuickConnect();
                    m_authBusy = false;
                    m_authError = tr("Quick Connect sign in failed: %1").arg(error);
                    emit authChanged();
                    return;
                }
                enterAuthenticatedSession(session, true);
            });
    });
}

void AppController::cancelQuickConnect()
{
    m_quickConnectTimer->stop();
    if (!m_quickConnectActive)
        return;

    nextAuthGeneration(this);
    m_quickConnectActive = false;
    m_quickConnectSecret.clear();
    m_quickConnectCode.clear();
    m_quickConnectPolls = 0;
    if (m_authBusy) {
        m_authBusy = false;
        emit authChanged();
    }
    emit quickConnectChanged();
}

void AppController::showServerSelect()
{
    // A token only belongs to the server that issued it, so switching servers
    // ends the current session.
    if (m_authenticated)
        logout();

    cancelQuickConnect();
    nextAuthGeneration(this);
    m_authBusy = false;
    m_authError.clear();
    emit authChanged();

    m_history.clear();
    pushAndNavigate({QStringLiteral("serverSelect"), QStringLiteral("home"), QStringLiteral("Connect"), {}, {}}, false);
}

void AppController::logout()
{
    cancelQuickConnect();
    nextAuthGeneration(this);

    // Cleared first so that nothing triggered below treats us as signed in.
    m_authenticated = false;
    if (m_player->isOpen())
        m_player->stopAndClose();

    if (!m_client->accessToken().isEmpty())
        m_client->logout(this);
    m_client->clearSession();
    m_settings->clearSession();
    m_imageCache->setAccessToken(QString());

    // Orphan every data request still in flight.
    ++m_homeRefreshToken;
    ++m_libraryRequestToken;
    ++m_libraryFiltersToken;
    ++m_detailRequestToken;
    ++m_searchRequestToken;
    m_searchDebounceTimer->stop();

    m_authBusy = false;
    m_authError.clear();
    m_userName.clear();
    m_history.clear();
    setProperty(kLastPlayedProperty, QString());

    m_homeLoading = false;
    m_heroItem.clear();
    m_libraryLoading = false;
    m_libraryTotalCount = 0;
    m_libraryQuery = JellyfinClient::ItemQuery();
    m_availableGenres.clear();
    m_availableYears.clear();
    m_availableRatings.clear();
    m_detailLoading = false;
    m_detailMediaItem = MediaItem();
    m_detailItem.clear();
    m_currentDetailItemId.clear();
    m_selectedSeasonId.clear();
    m_searchQuery.clear();
    m_searchLoading = false;

    m_homeSectionsModel->clear();
    const QList<MediaItemModel *> models = allItemModels(this);
    for (MediaItemModel *model : models)
        model->clear();

    emit authChanged();
    pushAndNavigate({QStringLiteral("login"), QStringLiteral("home"), QStringLiteral("Sign In"), {}, {}}, false);
    emit homeChanged();
    emit libraryChanged();
    emit libraryFiltersChanged();
    emit detailChanged();
    emit searchChanged();
}

void AppController::handleAuthFailureIfUnauthorized(const QString &error)
{
    if (!m_authenticated || !isUnauthorizedError(error))
        return;

    m_client->clearSession(); // the token is dead; skip the server logout
    logout();
    m_authError = tr("Your session expired. Please sign in again.");
    emit authChanged();
}

// --- Navigation --------------------------------------------------------------

void AppController::pushAndNavigate(const NavEntry &entry, bool pushCurrent)
{
    const bool sameTarget = entry.view == m_currentView && entry.navKey == m_activeNavKey
        && (entry.view != QLatin1String("detail") || entry.itemId == m_currentDetailItemId);
    const bool currentIsSignedOut =
        m_currentView == QLatin1String("serverSelect") || m_currentView == QLatin1String("login");

    if (pushCurrent && !sameTarget && !currentIsSignedOut) {
        NavEntry current{m_currentView, m_activeNavKey, m_pageTitle, {}, m_libraryQuery};
        if (m_currentView == QLatin1String("detail"))
            current.itemId = m_currentDetailItemId;
        m_history.append(current);
        while (m_history.size() > kMaxHistory)
            m_history.removeFirst();
    }

    m_currentView = entry.view;
    m_activeNavKey = entry.navKey;
    m_pageTitle = entry.title;
    emit navigationChanged();
}

void AppController::restoreNavEntry(const NavEntry &entry)
{
    m_currentView = entry.view;
    m_activeNavKey = entry.navKey;
    m_pageTitle = entry.title;

    if (entry.view == QLatin1String("library")) {
        // Keep the loaded pages (and the scroll position) when possible.
        const bool reload = !sameQuery(entry.libraryQuery, m_libraryQuery)
            || (m_libraryModel->rowCount() == 0 && !m_libraryLoading);
        if (reload) {
            m_libraryQuery = entry.libraryQuery;
            m_libraryQuery.startIndex = 0;
        }
        emit navigationChanged();
        if (reload) {
            loadLibraryPage(true);
            loadLibraryFilters();
        }
        return;
    }

    emit navigationChanged();
    if (entry.view == QLatin1String("detail") && entry.itemId != m_currentDetailItemId)
        loadDetailItem(entry.itemId);
}

void AppController::goBack()
{
    if (m_history.isEmpty())
        return;
    const NavEntry entry = m_history.takeLast();
    restoreNavEntry(entry);
}

void AppController::openHome()
{
    if (!m_authenticated)
        return;
    m_history.clear();
    pushAndNavigate({QStringLiteral("home"), QStringLiteral("home"), QStringLiteral("Home"), {}, {}}, false);
    if (!m_homeLoading)
        refreshHome();
}

void AppController::openLibrary(const QString &libraryId, const QString &title, const QString &collectionType)
{
    if (!m_authenticated || libraryId.isEmpty())
        return;
    if (m_currentView == QLatin1String("library") && m_activeNavKey == libraryId)
        return;

    QString type = collectionType;
    if (type.isEmpty()) {
        if (const std::optional<MediaItem> view = m_librariesModel->itemById(libraryId))
            type = view->collectionType;
    }

    pushAndNavigate({QStringLiteral("library"), libraryId, title, {}, {}});

    m_libraryQuery = JellyfinClient::ItemQuery();
    m_libraryQuery.parentId = libraryId;
    m_libraryQuery.includeItemTypes = itemTypesForCollection(type);
    loadLibraryPage(true);
    loadLibraryFilters();
}

void AppController::openFavorites()
{
    if (!m_authenticated)
        return;

    pushAndNavigate({QStringLiteral("library"), QStringLiteral("favorites"), tr("Favorites"), {}, {}});

    m_libraryQuery = JellyfinClient::ItemQuery();
    m_libraryQuery.includeItemTypes = {QStringLiteral("Movie"), QStringLiteral("Series"), QStringLiteral("Episode")};
    m_libraryQuery.favoritesOnly = true;
    loadLibraryPage(true);
    loadLibraryFilters();
}

void AppController::openPerson(const QString &personId, const QString &personName)
{
    if (!m_authenticated || personId.isEmpty())
        return;

    pushAndNavigate({QStringLiteral("library"), QStringLiteral("person:") + personId, personName, {}, {}});

    m_libraryQuery = JellyfinClient::ItemQuery();
    m_libraryQuery.personId = personId;
    m_libraryQuery.includeItemTypes = {QStringLiteral("Movie"), QStringLiteral("Series")};
    // A filmography reads best newest-first.
    m_libraryQuery.sortBy = QStringLiteral("PremiereDate");
    m_libraryQuery.ascending = false;
    loadLibraryPage(true);
    loadLibraryFilters();
}

void AppController::openGenre(const QString &genre)
{
    if (!m_authenticated || genre.isEmpty())
        return;

    pushAndNavigate({QStringLiteral("library"), QStringLiteral("genre:") + genre, genre, {}, {}});

    m_libraryQuery = JellyfinClient::ItemQuery();
    m_libraryQuery.includeItemTypes = {QStringLiteral("Movie"), QStringLiteral("Series")};
    m_libraryQuery.genres = {genre};
    loadLibraryPage(true);
    loadLibraryFilters();
}

void AppController::openDetail(const QString &itemId)
{
    if (!m_authenticated || itemId.isEmpty())
        return;
    if (m_currentView == QLatin1String("detail") && m_currentDetailItemId == itemId)
        return;

    const std::optional<MediaItem> cached = lookupCachedItem(itemId);
    pushAndNavigate(
        {QStringLiteral("detail"), m_activeNavKey, cached ? cached->name : QString(), itemId, {}});
    loadDetailItem(itemId);
}

void AppController::openItem(const QVariantMap &item)
{
    const QString id = item.value(QStringLiteral("id")).toString();
    const QString name = item.value(QStringLiteral("name")).toString();
    const QString type = item.value(QStringLiteral("type")).toString();

    if (type == QLatin1String("CollectionFolder") || type == QLatin1String("UserView")
        || type == QLatin1String("Folder")) {
        openLibrary(id, name, item.value(QStringLiteral("collectionType")).toString());
    } else if (type == QLatin1String("Person")) {
        openPerson(id, name);
    } else if (type == QLatin1String("Genre")) {
        openGenre(name);
    } else {
        openDetail(id);
    }
}

void AppController::openSearch()
{
    if (!m_authenticated)
        return;
    pushAndNavigate({QStringLiteral("search"), QStringLiteral("search"), tr("Search"), {}, {}});
}

void AppController::openSettings()
{
    if (!m_authenticated)
        return;
    pushAndNavigate({QStringLiteral("settings"), QStringLiteral("settings"), tr("Settings"), {}, {}});
    refreshCacheSize();
}

// --- Home ----------------------------------------------------------------------

void AppController::refreshHome()
{
    if (!m_authenticated)
        return;

    const int token = ++m_homeRefreshToken;
    m_homeLoading = true;
    emit homeChanged();

    auto pending = std::make_shared<int>(3); // views, resume, next up
    const auto finishOne = [this, pending]() {
        if (--(*pending) > 0)
            return;
        m_homeLoading = false;
        m_heroItem = pickHeroItem(m_homeSectionsModel, false);
        emit homeChanged();
    };

    // True when the reply belongs to the current refresh and succeeded.
    const auto accept = [this, token, finishOne](bool ok, const QString &error) {
        if (token != m_homeRefreshToken)
            return false;
        if (ok)
            return true;
        handleAuthFailureIfUnauthorized(error);
        if (token == m_homeRefreshToken)
            finishOne(); // keep whatever the row showed before
        return false;
    };

    m_client->fetchUserViews(
        this, [this, token, pending, accept, finishOne](bool ok, const QList<MediaItem> &views, const QString &error) {
            if (!accept(ok, error)) {
                if (token == m_homeRefreshToken)
                    emit toast(tr("Could not load your libraries: %1").arg(error), QStringLiteral("error"));
                return;
            }

            m_librariesModel->setItems(views);

            QList<MediaItem> videoViews;
            QStringList wantedKeys;
            for (const MediaItem &view : views) {
                if (isVideoView(view)) {
                    videoViews.append(view);
                    wantedKeys.append(QStringLiteral("latest:") + view.id);
                }
            }

            QStringList staleKeys;
            for (int row = 0; row < m_homeSectionsModel->rowCount(); ++row) {
                const QString key = m_homeSectionsModel
                                        ->data(m_homeSectionsModel->index(row), HomeSectionModel::KeyRole)
                                        .toString();
                if (key.startsWith(QLatin1String("latest:")) && !wantedKeys.contains(key))
                    staleKeys.append(key);
            }
            for (const QString &key : std::as_const(staleKeys))
                m_homeSectionsModel->removeSection(key);

            *pending += static_cast<int>(videoViews.size());
            for (int i = 0; i < videoViews.size(); ++i) {
                const MediaItem view = videoViews.at(i);
                m_client->fetchLatestItems(
                    view.id, this,
                    [this, view, i, accept, finishOne](bool latestOk, const QList<MediaItem> &items,
                                                       const QString &latestError) {
                        if (!accept(latestOk, latestError))
                            return;
                        m_homeSectionsModel->upsertSection(QStringLiteral("latest:") + view.id,
                                                           tr("Latest in %1").arg(view.name), QStringLiteral("latest"),
                                                           false, view.id, 10 + i, items);
                        finishOne();
                    });
            }
            finishOne();
        });

    m_client->fetchResumeItems(
        this, [this, accept, finishOne](bool ok, const QList<MediaItem> &items, const QString &error) {
            if (!accept(ok, error))
                return;
            m_homeSectionsModel->upsertSection(QStringLiteral("resume"), tr("Continue Watching"),
                                               QStringLiteral("resume"), true, QString(), 0, items);
            // Continue Watching outranks everything, so the banner need not
            // wait for the remaining rows.
            const QVariantMap hero = pickHeroItem(m_homeSectionsModel, true);
            if (!hero.isEmpty()) {
                m_heroItem = hero;
                emit homeChanged();
            }
            finishOne();
        });

    m_client->fetchNextUp(this,
                          [this, accept, finishOne](bool ok, const QList<MediaItem> &items, const QString &error) {
                              if (!accept(ok, error))
                                  return;
                              m_homeSectionsModel->upsertSection(QStringLiteral("nextup"), tr("Next Up"),
                                                                 QStringLiteral("nextup"), true, QString(), 1, items);
                              finishOne();
                          });
}

// --- Library ----------------------------------------------------------------------

bool AppController::libraryHasMore() const
{
    return m_libraryModel->rowCount() < m_libraryTotalCount;
}

QString AppController::libraryGenre() const
{
    return m_libraryQuery.genres.value(0);
}

int AppController::libraryYear() const
{
    return m_libraryQuery.years.value(0);
}

QString AppController::libraryRating() const
{
    return m_libraryQuery.officialRatings.value(0);
}

QString AppController::libraryPlayedFilter() const
{
    switch (m_libraryQuery.played) {
    case JellyfinClient::ItemQuery::Played::PlayedOnly:
        return QStringLiteral("played");
    case JellyfinClient::ItemQuery::Played::UnplayedOnly:
        return QStringLiteral("unplayed");
    case JellyfinClient::ItemQuery::Played::Any:
        break;
    }
    return QStringLiteral("any");
}

void AppController::setLibraryViewMode(const QString &mode)
{
    if (mode == m_libraryViewMode)
        return;
    m_libraryViewMode = mode;
    emit libraryChanged();
}

void AppController::loadLibraryPage(bool reset)
{
    if (!m_authenticated)
        return;

    if (reset) {
        ++m_libraryRequestToken;
        m_libraryTotalCount = 0;
        m_libraryModel->clear();
    }
    const int token = m_libraryRequestToken;

    JellyfinClient::ItemQuery query = m_libraryQuery;
    query.startIndex = reset ? 0 : m_libraryModel->rowCount();
    query.limit = kLibraryPageSize;

    m_libraryLoading = true;
    emit libraryChanged();

    m_client->fetchItems(
        query, this, [this, token, reset](bool ok, const JellyfinClient::ItemPage &page, const QString &error) {
            if (token != m_libraryRequestToken)
                return;
            m_libraryLoading = false;
            if (!ok) {
                handleAuthFailureIfUnauthorized(error);
                if (token != m_libraryRequestToken)
                    return;
                emit libraryChanged();
                emit toast(tr("Could not load items: %1").arg(error), QStringLiteral("error"));
                return;
            }

            if (reset)
                m_libraryModel->setItems(page.items);
            else
                m_libraryModel->appendItems(page.items);
            // An empty page ends paging even if the reported total disagrees.
            m_libraryTotalCount = page.items.isEmpty() ? m_libraryModel->rowCount() : page.totalRecordCount;
            emit libraryChanged();
        });
}

void AppController::loadMoreLibraryItems()
{
    if (m_libraryLoading || !libraryHasMore())
        return;
    loadLibraryPage(false);
}

void AppController::loadLibraryFilters()
{
    const int token = ++m_libraryFiltersToken;
    m_availableGenres.clear();
    m_availableYears.clear();
    m_availableRatings.clear();
    emit libraryFiltersChanged();

    if (!m_authenticated)
        return;

    m_client->fetchLibraryFilters(
        m_libraryQuery.parentId, m_libraryQuery.includeItemTypes, this,
        [this, token](bool ok, const JellyfinClient::LibraryFilters &filters, const QString &error) {
            if (!m_authenticated || token != m_libraryFiltersToken)
                return;
            if (!ok) {
                handleAuthFailureIfUnauthorized(error);
                return;
            }
            m_availableGenres = filters.genres;
            m_availableRatings = filters.officialRatings;
            m_availableYears.clear();
            for (int year : filters.years)
                m_availableYears.append(year);
            emit libraryFiltersChanged();
        });
}

void AppController::setLibrarySort(const QString &sortBy, bool ascending)
{
    if (sortBy.isEmpty() || (m_libraryQuery.sortBy == sortBy && m_libraryQuery.ascending == ascending))
        return;
    m_libraryQuery.sortBy = sortBy;
    m_libraryQuery.ascending = ascending;
    loadLibraryPage(true);
}

void AppController::setLibraryGenre(const QString &genre)
{
    const QStringList genres = genre.isEmpty() ? QStringList() : QStringList{genre};
    if (m_libraryQuery.genres == genres)
        return;
    m_libraryQuery.genres = genres;
    loadLibraryPage(true);
}

void AppController::setLibraryYear(int year)
{
    const QList<int> years = year > 0 ? QList<int>{year} : QList<int>();
    if (m_libraryQuery.years == years)
        return;
    m_libraryQuery.years = years;
    loadLibraryPage(true);
}

void AppController::setLibraryRating(const QString &rating)
{
    const QStringList ratings = rating.isEmpty() ? QStringList() : QStringList{rating};
    if (m_libraryQuery.officialRatings == ratings)
        return;
    m_libraryQuery.officialRatings = ratings;
    loadLibraryPage(true);
}

void AppController::setLibraryPlayedFilter(const QString &filter)
{
    using Played = JellyfinClient::ItemQuery::Played;
    Played played = Played::Any;
    if (filter == QLatin1String("played"))
        played = Played::PlayedOnly;
    else if (filter == QLatin1String("unplayed"))
        played = Played::UnplayedOnly;

    if (m_libraryQuery.played == played)
        return;
    m_libraryQuery.played = played;
    loadLibraryPage(true);
}

void AppController::setLibraryFavoritesOnly(bool favoritesOnly)
{
    if (m_libraryQuery.favoritesOnly == favoritesOnly)
        return;
    m_libraryQuery.favoritesOnly = favoritesOnly;
    loadLibraryPage(true);
}

void AppController::resetLibraryFilters()
{
    JellyfinClient::ItemQuery query = m_libraryQuery;
    query.genres.clear();
    query.officialRatings.clear();
    query.years.clear();
    query.played = JellyfinClient::ItemQuery::Played::Any;
    // Being favorites-only is what the Favorites page is, not a filter on it.
    query.favoritesOnly = m_activeNavKey == QLatin1String("favorites");

    if (sameQuery(query, m_libraryQuery))
        return;
    m_libraryQuery = query;
    loadLibraryPage(true);
}

// --- Detail ---------------------------------------------------------------------------

QVariantMap AppController::buildDetailVariant(const MediaItem &item)
{
    QVariantMap map = MediaItemModel::toVariantMap(item);

    QVariantList cast;
    cast.reserve(item.people.size());
    for (const Person &person : item.people) {
        cast.append(QVariantMap{
            {QStringLiteral("id"), person.id},
            {QStringLiteral("name"), person.name},
            {QStringLiteral("role"), person.role.isEmpty() ? person.type : person.role},
            {QStringLiteral("primaryTag"), person.primaryImageTag},
        });
    }
    map.insert(QStringLiteral("cast"), cast);

    QStringList studios;
    for (const NameId &studio : item.studios)
        studios.append(studio.name);
    map.insert(QStringLiteral("studios"), studios);

    QString resolution;
    QString videoCodec;
    QString audio;
    QString container;
    QString hdr;
    if (!item.mediaSources.isEmpty()) {
        const MediaSource &source = item.mediaSources.first();
        container = source.container.section(QLatin1Char(','), 0, 0).toUpper();

        const MediaStream *video = nullptr;
        const MediaStream *audioStream = nullptr;
        for (const MediaStream &stream : source.streams) {
            if (stream.type == QLatin1String("Video") && !video) {
                video = &stream;
            } else if (stream.type == QLatin1String("Audio")
                       && (!audioStream || (stream.isDefault && !audioStream->isDefault))) {
                audioStream = &stream;
            }
        }
        if (video) {
            resolution = resolutionBadge(*video);
            videoCodec = videoCodecBadge(video->codec);
            if (video->displayTitle.contains(QLatin1String("Dolby Vision"), Qt::CaseInsensitive))
                hdr = QStringLiteral("Dolby Vision");
            else if (video->videoRange.compare(QLatin1String("HDR"), Qt::CaseInsensitive) == 0)
                hdr = QStringLiteral("HDR");
        }
        if (audioStream)
            audio = audioBadge(*audioStream);
    }
    map.insert(QStringLiteral("resolutionBadge"), resolution);
    map.insert(QStringLiteral("videoCodecBadge"), videoCodec);
    map.insert(QStringLiteral("audioBadge"), audio);
    map.insert(QStringLiteral("containerBadge"), container);
    map.insert(QStringLiteral("hdrBadge"), hdr);
    return map;
}

void AppController::loadDetailItem(const QString &itemId)
{
    if (!m_authenticated || itemId.isEmpty())
        return;

    // Reloading the item already on screen updates it quietly, keeping the
    // lists and the selected season in place.
    const bool refresh = itemId == m_currentDetailItemId && m_detailMediaItem.id == itemId;
    const int token = ++m_detailRequestToken;

    if (!refresh) {
        const std::optional<MediaItem> cached = lookupCachedItem(itemId);
        m_currentDetailItemId = itemId;
        m_detailMediaItem = cached ? *cached : MediaItem();
        m_detailItem = cached ? buildDetailVariant(*cached) : QVariantMap();
        m_selectedSeasonId.clear();
        m_seasonsModel->clear();
        m_episodesModel->clear();
        m_detailLoading = true;
        emit detailChanged();
    }

    m_client->fetchItem(itemId, this, [this, token](bool ok, const MediaItem &item, const QString &error) {
        if (token != m_detailRequestToken)
            return;
        m_detailLoading = false;
        if (!ok) {
            handleAuthFailureIfUnauthorized(error);
            if (token != m_detailRequestToken)
                return;
            emit detailChanged();
            emit toast(tr("Could not load details: %1").arg(error), QStringLiteral("error"));
            return;
        }

        m_detailMediaItem = item;
        m_detailItem = buildDetailVariant(item);
        if (m_currentView == QLatin1String("detail") && m_pageTitle != item.name) {
            m_pageTitle = item.name;
            emit navigationChanged();
        }
        emit detailChanged();

        if (item.type == QLatin1String("Series") || item.type == QLatin1String("Season")) {
            const bool isSeason = item.type == QLatin1String("Season");
            const QString seriesId = isSeason ? item.seriesId : item.id;
            const QString preferred = isSeason ? item.id : QString();
            m_client->fetchSeasons(
                seriesId, this,
                [this, token, preferred](bool seasonsOk, const QList<MediaItem> &seasons, const QString &seasonsError) {
                    if (token != m_detailRequestToken)
                        return;
                    if (!seasonsOk) {
                        handleAuthFailureIfUnauthorized(seasonsError);
                        if (token == m_detailRequestToken)
                            emit toast(tr("Could not load seasons: %1").arg(seasonsError), QStringLiteral("error"));
                        return;
                    }
                    m_seasonsModel->setItems(seasons);

                    QString target = m_selectedSeasonId; // kept across a refresh
                    if (target.isEmpty() || !m_seasonsModel->itemById(target))
                        target = preferred.isEmpty() ? pickSeasonId(seasons) : preferred;
                    selectSeason(target);
                });
        } else if (item.type == QLatin1String("BoxSet")) {
            // A collection's members are listed through the episodes model.
            JellyfinClient::ItemQuery query;
            query.parentId = item.id;
            query.includeItemTypes = {QStringLiteral("Movie"), QStringLiteral("Series"), QStringLiteral("Video")};
            query.sortBy = QStringLiteral("PremiereDate");
            query.limit = kCollectionLimit;
            m_client->fetchItems(
                query, this,
                [this, token](bool pageOk, const JellyfinClient::ItemPage &page, const QString &pageError) {
                    if (token != m_detailRequestToken)
                        return;
                    if (!pageOk) {
                        handleAuthFailureIfUnauthorized(pageError);
                        if (token == m_detailRequestToken)
                            emit toast(tr("Could not load the collection: %1").arg(pageError),
                                       QStringLiteral("error"));
                        return;
                    }
                    m_episodesModel->setItems(page.items);
                });
        }
    });
}

void AppController::selectSeason(const QString &seasonId)
{
    if (!m_authenticated || seasonId.isEmpty())
        return;

    const QString seriesId = m_detailMediaItem.type == QLatin1String("Series") ? m_detailMediaItem.id
                                                                              : m_detailMediaItem.seriesId;
    if (seriesId.isEmpty())
        return;

    if (m_selectedSeasonId != seasonId) {
        m_selectedSeasonId = seasonId;
        m_episodesModel->clear();
        emit detailChanged();
    }

    const int token = m_detailRequestToken;
    m_client->fetchEpisodes(
        seriesId, seasonId, this,
        [this, token, seasonId](bool ok, const QList<MediaItem> &episodes, const QString &error) {
            if (token != m_detailRequestToken || seasonId != m_selectedSeasonId)
                return;
            if (!ok) {
                handleAuthFailureIfUnauthorized(error);
                if (token == m_detailRequestToken)
                    emit toast(tr("Could not load episodes: %1").arg(error), QStringLiteral("error"));
                return;
            }
            m_episodesModel->setItems(episodes);
        });
}

// --- Search ----------------------------------------------------------------------------

QStringList AppController::recentSearches() const
{
    return m_settings->recentSearches();
}

void AppController::setSearchQuery(const QString &query)
{
    if (query == m_searchQuery)
        return;

    const bool sameTerm = query.trimmed() == m_searchQuery.trimmed();
    m_searchQuery = query;
    if (sameTerm) {
        emit searchChanged();
        return;
    }

    ++m_searchRequestToken; // results for the old term are no longer wanted
    if (query.trimmed().isEmpty()) {
        m_searchDebounceTimer->stop();
        m_searchLoading = false;
        m_searchMoviesModel->clear();
        m_searchSeriesModel->clear();
        m_searchEpisodesModel->clear();
        m_searchPeopleModel->clear();
    } else {
        m_searchLoading = true;
        m_searchDebounceTimer->start();
    }
    emit searchChanged();
}

void AppController::executeSearch(const QString &term)
{
    if (!m_authenticated || term.isEmpty())
        return;

    const int token = ++m_searchRequestToken;
    m_searchLoading = true;
    emit searchChanged();

    m_client->searchItems(term, this, [this, token, term](bool ok, const QList<MediaItem> &items, const QString &error) {
        if (token != m_searchRequestToken)
            return;
        m_searchLoading = false;
        if (!ok) {
            handleAuthFailureIfUnauthorized(error);
            if (token != m_searchRequestToken)
                return;
            emit searchChanged();
            emit toast(tr("Search failed: %1").arg(error), QStringLiteral("error"));
            return;
        }

        QList<MediaItem> movies;
        QList<MediaItem> series;
        QList<MediaItem> episodes;
        QList<MediaItem> people;
        for (const MediaItem &item : items) {
            if (item.type == QLatin1String("Movie"))
                movies.append(item);
            else if (item.type == QLatin1String("Series"))
                series.append(item);
            else if (item.type == QLatin1String("Episode"))
                episodes.append(item);
            else if (item.type == QLatin1String("Person"))
                people.append(item);
        }
        m_searchMoviesModel->setItems(movies);
        m_searchSeriesModel->setItems(series);
        m_searchEpisodesModel->setItems(episodes);
        m_searchPeopleModel->setItems(people);

        if (!items.isEmpty() && term.size() >= kMinRecentSearchLength)
            m_settings->addRecentSearch(term);
        emit searchChanged();
    });
}

void AppController::clearRecentSearches()
{
    m_settings->clearRecentSearches();
    emit searchChanged();
}

// --- Item actions ------------------------------------------------------------------------

std::optional<MediaItem> AppController::lookupCachedItem(const QString &itemId) const
{
    if (itemId.isEmpty())
        return std::nullopt;
    if (m_detailMediaItem.id == itemId)
        return m_detailMediaItem;

    for (const MediaItemModel *model : {m_episodesModel, m_seasonsModel}) {
        if (std::optional<MediaItem> item = model->itemById(itemId))
            return item;
    }
    if (std::optional<MediaItem> item = m_homeSectionsModel->findItemById(itemId))
        return item;
    for (const MediaItemModel *model : {m_libraryModel, m_searchMoviesModel, m_searchSeriesModel,
                                        m_searchEpisodesModel, m_searchPeopleModel, m_librariesModel}) {
        if (std::optional<MediaItem> item = model->itemById(itemId))
            return item;
    }
    return std::nullopt;
}

void AppController::propagateUserState(const QString &itemId, bool played, bool isFavorite)
{
    m_librariesModel->updateUserState(itemId, played, isFavorite);
    m_homeSectionsModel->updateUserState(itemId, played, isFavorite);
    for (MediaItemModel *model : {m_libraryModel, m_seasonsModel, m_episodesModel, m_searchMoviesModel,
                                  m_searchSeriesModel, m_searchEpisodesModel, m_searchPeopleModel}) {
        model->updateUserState(itemId, played, isFavorite);
    }

    if (m_detailMediaItem.id == itemId) {
        UserData &data = m_detailMediaItem.userData;
        if (data.played != played) {
            data.playbackPositionTicks = 0;
            data.playedPercentage = 0.0;
        }
        data.played = played;
        data.isFavorite = isFavorite;
        m_detailItem = buildDetailVariant(m_detailMediaItem);
        emit detailChanged();
    }

    if (m_heroItem.value(QStringLiteral("id")).toString() == itemId) {
        if (m_heroItem.value(QStringLiteral("played")).toBool() != played) {
            m_heroItem.insert(QStringLiteral("playbackPositionTicks"), 0);
            m_heroItem.insert(QStringLiteral("playedPercentage"), 0.0);
            m_heroItem.insert(QStringLiteral("resumePositionText"), QString());
            m_heroItem.insert(QStringLiteral("remainingText"), QString());
        }
        m_heroItem.insert(QStringLiteral("played"), played);
        m_heroItem.insert(QStringLiteral("isFavorite"), isFavorite);
        emit homeChanged();
    }
}

void AppController::togglePlayed(const QString &itemId, bool currentlyPlayed)
{
    if (!m_authenticated || itemId.isEmpty())
        return;

    const std::optional<MediaItem> before = lookupCachedItem(itemId);
    const bool favorite = before ? before->userData.isFavorite
                                 : m_heroItem.value(QStringLiteral("isFavorite")).toBool();
    const bool played = !currentlyPlayed;
    propagateUserState(itemId, played, favorite);

    m_client->setPlayed(itemId, played, this,
                        [this, itemId, currentlyPlayed, before](bool ok, const QString &error) {
        if (!m_authenticated)
            return;
        if (ok) {
            // Continue Watching, Next Up and season/episode state all follow
            // from this flag, so ask the server for them again.
            refreshHome();
            if (m_currentView == QLatin1String("detail"))
                loadDetailItem(m_currentDetailItemId);
            return;
        }

        const std::optional<MediaItem> now = lookupCachedItem(itemId);
        const bool favoriteNow = now && now->userData.isFavorite;
        propagateUserState(itemId, currentlyPlayed, favoriteNow);

        // Put the resume point back too.
        if (before && before->userData.playbackPositionTicks > 0) {
            UserData restored = before->userData;
            restored.isFavorite = favoriteNow;
            replaceUserData(this, itemId, restored);
            if (m_detailMediaItem.id == itemId) {
                m_detailMediaItem.userData = restored;
                m_detailItem = buildDetailVariant(m_detailMediaItem);
                emit detailChanged();
            }
            if (m_heroItem.value(QStringLiteral("id")).toString() == itemId) {
                MediaItem hero = *before;
                hero.userData = restored;
                m_heroItem = MediaItemModel::toVariantMap(hero);
                emit homeChanged();
            }
        }

        handleAuthFailureIfUnauthorized(error);
        emit toast(tr("Could not update watched state: %1").arg(error), QStringLiteral("error"));
    });
}

void AppController::toggleFavorite(const QString &itemId, bool currentlyFavorite)
{
    if (!m_authenticated || itemId.isEmpty())
        return;

    const std::optional<MediaItem> before = lookupCachedItem(itemId);
    const bool played = before ? before->userData.played : m_heroItem.value(QStringLiteral("played")).toBool();
    const bool favorite = !currentlyFavorite;
    propagateUserState(itemId, played, favorite);

    m_client->setFavorite(itemId, favorite, this,
                          [this, itemId, currentlyFavorite, played](bool ok, const QString &error) {
        if (!m_authenticated || ok)
            return;
        const std::optional<MediaItem> now = lookupCachedItem(itemId);
        propagateUserState(itemId, now ? now->userData.played : played, currentlyFavorite);
        handleAuthFailureIfUnauthorized(error);
        emit toast(tr("Could not update favorites: %1").arg(error), QStringLiteral("error"));
    });
}

// --- Playback -------------------------------------------------------------------------------

void AppController::playItem(const QString &itemId, bool resume)
{
    if (!m_authenticated || itemId.isEmpty())
        return;

    const auto start = [this, resume](const MediaItem &item) {
        setProperty(kLastPlayedProperty, item.id);
        m_player->startItemPlayback(item, resume);
    };

    const auto reportFailure = [this](const QString &error) {
        handleAuthFailureIfUnauthorized(error);
        emit toast(tr("Cannot start playback: %1").arg(error), QStringLiteral("error"));
    };

    // Series and seasons play their first unwatched episode.
    const auto playFromSeason = [this, start, reportFailure](const QString &seriesId, const QString &seasonId) {
        m_client->fetchEpisodes(
            seriesId, seasonId, this,
            [this, start, reportFailure](bool ok, const QList<MediaItem> &episodes, const QString &error) {
                if (!m_authenticated)
                    return;
                if (!ok) {
                    reportFailure(error);
                    return;
                }
                if (episodes.isEmpty()) {
                    emit toast(tr("There are no episodes to play."), QStringLiteral("info"));
                    return;
                }
                for (const MediaItem &episode : episodes) {
                    if (!episode.userData.played) {
                        start(episode);
                        return;
                    }
                }
                start(episodes.first());
            });
    };

    const auto dispatch = [this, start, reportFailure, playFromSeason](const MediaItem &item) {
        if (isPlayableType(item.type)) {
            start(item);
        } else if (item.type == QLatin1String("Season")) {
            playFromSeason(item.seriesId, item.id);
        } else if (item.type == QLatin1String("Series")) {
            const QString seriesId = item.id;
            m_client->fetchSeasons(
                seriesId, this,
                [this, seriesId, reportFailure, playFromSeason](bool ok, const QList<MediaItem> &seasons,
                                                                const QString &error) {
                    if (!m_authenticated)
                        return;
                    if (!ok) {
                        reportFailure(error);
                        return;
                    }
                    const QString seasonId = pickSeasonId(seasons);
                    if (seasonId.isEmpty()) {
                        emit toast(tr("There are no episodes to play."), QStringLiteral("info"));
                        return;
                    }
                    playFromSeason(seriesId, seasonId);
                });
        } else {
            emit toast(tr("This item cannot be played."), QStringLiteral("info"));
        }
    };

    if (const std::optional<MediaItem> cached = lookupCachedItem(itemId)) {
        dispatch(*cached);
        return;
    }

    m_client->fetchItem(itemId, this,
                        [this, dispatch, reportFailure](bool ok, const MediaItem &item, const QString &error) {
                            if (!m_authenticated)
                                return;
                            if (!ok) {
                                reportFailure(error);
                                return;
                            }
                            dispatch(item);
                        });
}

void AppController::playDirectUrl(const QUrl &url, qint64 startSeconds, const QString &title)
{
    m_player->playDirectUrl(url, startSeconds, title);
}

// --- Settings & appearance ----------------------------------------------------------------------

QString AppController::themeMode() const
{
    return m_settings->themeMode();
}

void AppController::setThemeMode(const QString &mode)
{
    if (mode == themeMode())
        return;
    m_settings->setThemeMode(mode);
    emit settingsChanged();
}

QString AppController::accentColor() const
{
    return m_settings->accentColor();
}

void AppController::setAccentColor(const QString &hex)
{
    if (hex == accentColor())
        return;
    m_settings->setAccentColor(hex);
    emit settingsChanged();
}

bool AppController::reduceMotion() const
{
    return m_settings->reduceMotion();
}

void AppController::setReduceMotion(bool enabled)
{
    if (enabled == reduceMotion())
        return;
    m_settings->setReduceMotion(enabled);
    emit settingsChanged();
}

bool AppController::dualCoreMode() const
{
    return m_settings->dualCoreMode();
}

void AppController::setDualCoreMode(bool enabled)
{
    if (enabled == dualCoreMode())
        return;
    m_settings->setDualCoreMode(enabled);
    m_imageCache->setMemoryLimitBytes(enabled ? kDualCoreImageMemoryBytes : ImageCache::kMaxMemoryCacheBytes);
    m_player->applyPresentationSettings();
    emit settingsChanged();
}

int AppController::cpuCoreCount() const
{
    return QThread::idealThreadCount();
}

QString AppController::hwdecMode() const
{
    return m_settings->hwdecMode();
}

void AppController::setHwdecMode(const QString &mode)
{
    if (mode == hwdecMode())
        return;
    m_settings->setHwdecMode(mode);
    m_player->applyPresentationSettings();
    emit settingsChanged();
}

QString AppController::videoProfile() const
{
    return m_settings->videoProfile();
}

void AppController::setVideoProfile(const QString &profile)
{
    if (profile == videoProfile())
        return;
    m_settings->setVideoProfile(profile);
    m_player->applyPresentationSettings();
    emit settingsChanged();
}

int AppController::maxBitrateMbps() const
{
    return m_settings->maxBitrateMbps();
}

void AppController::setMaxBitrateMbps(int mbps)
{
    const int clamped = qMax(0, mbps);
    const bool changed = clamped != maxBitrateMbps();
    if (changed)
        m_settings->setMaxBitrateMbps(clamped);
    // The quality menu can move the player away from the stored value, so it is
    // synced even when the setting did not change (a no-op if already equal).
    m_player->setMaxBitrateMbps(clamped);
    if (changed)
        emit settingsChanged();
}

bool AppController::autoSkipSegments() const
{
    return m_settings->autoSkipSegments();
}

void AppController::setAutoSkipSegments(bool enabled)
{
    if (enabled == autoSkipSegments())
        return;
    m_settings->setAutoSkipSegments(enabled);
    emit settingsChanged();
}

bool AppController::autoPlayNextEpisode() const
{
    return m_settings->autoPlayNextEpisode();
}

void AppController::setAutoPlayNextEpisode(bool enabled)
{
    if (enabled == autoPlayNextEpisode())
        return;
    m_settings->setAutoPlayNextEpisode(enabled);
    emit settingsChanged();
}

QString AppController::preferredAudioLanguage() const
{
    return m_settings->preferredAudioLanguage();
}

void AppController::setPreferredAudioLanguage(const QString &lang)
{
    if (lang == preferredAudioLanguage())
        return;
    m_settings->setPreferredAudioLanguage(lang);
    emit settingsChanged();
}

QString AppController::preferredSubtitleLanguage() const
{
    return m_settings->preferredSubtitleLanguage();
}

void AppController::setPreferredSubtitleLanguage(const QString &lang)
{
    if (lang == preferredSubtitleLanguage())
        return;
    m_settings->setPreferredSubtitleLanguage(lang);
    emit settingsChanged();
}

int AppController::subtitleFontSize() const
{
    return m_settings->subtitleFontSize();
}

void AppController::setSubtitleFontSize(int size)
{
    if (size == subtitleFontSize())
        return;
    m_settings->setSubtitleFontSize(size);
    m_player->applyPresentationSettings();
    emit settingsChanged();
}

QString AppController::subtitleFontFamily() const
{
    return m_settings->subtitleFontFamily();
}

void AppController::setSubtitleFontFamily(const QString &family)
{
    if (family == subtitleFontFamily())
        return;
    m_settings->setSubtitleFontFamily(family);
    m_player->applyPresentationSettings();
    emit settingsChanged();
}

QString AppController::subtitleColor() const
{
    return m_settings->subtitleColor();
}

void AppController::setSubtitleColor(const QString &argbHex)
{
    if (argbHex == subtitleColor())
        return;
    m_settings->setSubtitleColor(argbHex);
    m_player->applyPresentationSettings();
    emit settingsChanged();
}

QString AppController::subtitleOutlineColor() const
{
    return m_settings->subtitleOutlineColor();
}

void AppController::setSubtitleOutlineColor(const QString &argbHex)
{
    if (argbHex == subtitleOutlineColor())
        return;
    m_settings->setSubtitleOutlineColor(argbHex);
    m_player->applyPresentationSettings();
    emit settingsChanged();
}

double AppController::subtitleOutlineSize() const
{
    return m_settings->subtitleOutlineSize();
}

void AppController::setSubtitleOutlineSize(double size)
{
    if (size == subtitleOutlineSize())
        return;
    m_settings->setSubtitleOutlineSize(size);
    m_player->applyPresentationSettings();
    emit settingsChanged();
}

double AppController::subtitleShadowOffset() const
{
    return m_settings->subtitleShadowOffset();
}

void AppController::setSubtitleShadowOffset(double offset)
{
    if (offset == subtitleShadowOffset())
        return;
    m_settings->setSubtitleShadowOffset(offset);
    m_player->applyPresentationSettings();
    emit settingsChanged();
}

bool AppController::subtitleBackgroundBoxEnabled() const
{
    return m_settings->subtitleBackgroundBoxEnabled();
}

void AppController::setSubtitleBackgroundBoxEnabled(bool enabled)
{
    if (enabled == subtitleBackgroundBoxEnabled())
        return;
    m_settings->setSubtitleBackgroundBoxEnabled(enabled);
    m_player->applyPresentationSettings();
    emit settingsChanged();
}

QString AppController::subtitleBackgroundColor() const
{
    return m_settings->subtitleBackgroundColor();
}

void AppController::setSubtitleBackgroundColor(const QString &argbHex)
{
    if (argbHex == subtitleBackgroundColor())
        return;
    m_settings->setSubtitleBackgroundColor(argbHex);
    m_player->applyPresentationSettings();
    emit settingsChanged();
}

int AppController::subtitlePositionPercent() const
{
    return m_settings->subtitlePositionPercent();
}

void AppController::setSubtitlePositionPercent(int percent)
{
    if (percent == subtitlePositionPercent())
        return;
    m_settings->setSubtitlePositionPercent(percent);
    m_player->applyPresentationSettings();
    emit settingsChanged();
}

bool AppController::subtitleAssOverride() const
{
    return m_settings->subtitleAssOverride();
}

void AppController::setSubtitleAssOverride(bool enabled)
{
    if (enabled == subtitleAssOverride())
        return;
    m_settings->setSubtitleAssOverride(enabled);
    m_player->applyPresentationSettings();
    emit settingsChanged();
}

QString AppController::audioDevice() const
{
    return m_settings->audioDevice();
}

void AppController::setAudioDevice(const QString &device)
{
    if (device == audioDevice())
        return;
    m_settings->setAudioDevice(device);
    m_player->applyPresentationSettings();
    emit settingsChanged();
}

bool AppController::audioPassthroughEnabled() const
{
    return m_settings->audioPassthroughEnabled();
}

void AppController::setAudioPassthroughEnabled(bool enabled)
{
    if (enabled == audioPassthroughEnabled())
        return;
    m_settings->setAudioPassthroughEnabled(enabled);
    m_player->applyPresentationSettings();
    emit settingsChanged();
}

bool AppController::audioNormalizationEnabled() const
{
    return m_settings->audioNormalizationEnabled();
}

void AppController::setAudioNormalizationEnabled(bool enabled)
{
    if (enabled == audioNormalizationEnabled())
        return;
    m_settings->setAudioNormalizationEnabled(enabled);
    m_player->applyPresentationSettings();
    emit settingsChanged();
}

QString AppController::audioChannelLayout() const
{
    return m_settings->audioChannelLayout();
}

void AppController::setAudioChannelLayout(const QString &layout)
{
    if (layout == audioChannelLayout())
        return;
    m_settings->setAudioChannelLayout(layout);
    m_player->applyPresentationSettings();
    emit settingsChanged();
}

QString AppController::customMpvOptions() const
{
    return m_settings->customMpvOptions();
}

void AppController::setCustomMpvOptions(const QString &options)
{
    if (options == customMpvOptions())
        return;
    m_settings->setCustomMpvOptions(options);
    // Without a video item there is no mpv instance to validate against; the
    // stored text is applied when playback starts.
    if (m_player->videoItem()) {
        const QStringList rejected = m_player->applyCustomMpvOptions(options);
        if (!rejected.isEmpty()) {
            emit toast(tr("Rejected mpv options: %1").arg(rejected.join(QStringLiteral(", "))),
                       QStringLiteral("error"));
        }
    }
    emit settingsChanged();
}

QString AppController::mpvLogLevel() const
{
    return m_settings->mpvLogLevel();
}

void AppController::setMpvLogLevel(const QString &level)
{
    if (level == mpvLogLevel())
        return;
    m_settings->setMpvLogLevel(level);
    m_player->applyPresentationSettings();
    emit settingsChanged();
}

QString AppController::cacheSizeText() const
{
    return formatBytes(m_cachedDiskBytes);
}

void AppController::clearImageCache()
{
    m_imageCache->clearCacheAsync(this, [this]() {
        refreshCacheSize();
        emit toast(tr("Image cache cleared."), QStringLiteral("success"));
    });
}

void AppController::refreshCacheSize()
{
    const int token = ++m_cacheSizeToken;
    m_imageCache->diskCacheSizeBytesAsync(this, [this, token](qint64 bytes) {
        if (token != m_cacheSizeToken)
            return; // a newer scan is on its way
        m_cachedDiskBytes = bytes;
        emit cacheSizeChanged();
    });
}

// --- Window persistence & helpers ------------------------------------------------------------

int AppController::savedWindowWidth() const
{
    return m_settings->windowWidth();
}

int AppController::savedWindowHeight() const
{
    return m_settings->windowHeight();
}

bool AppController::savedWindowMaximized() const
{
    return m_settings->windowMaximized();
}

void AppController::saveWindowState(int width, int height, bool maximized)
{
    m_settings->setWindowState(width, height, maximized);
}

QString AppController::formatDuration(qint64 seconds) const
{
    return Formatting::formatPosition(seconds);
}

// Compiled here rather than standalone because the property types that
// AppController.h only forward-declares must be complete for moc.
#include "moc_AppController.cpp"
