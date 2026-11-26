#pragma once

#include "api/JellyfinClient.h"
#include "api/Models.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <optional>

class HomeSectionModel;
class ImageCache;
class MediaItemModel;
class NetworkQueue;
class PlayerController;
class Settings;
class QTimer;

// Central QML-exposed controller for the entire single-window Cutefin app:
// auth (password + Quick Connect), navigation stack, Home, Library browse with
// infinite paging & sort/filter, Detail (Movie/Series/Episode/Collection/Person),
// Search, Settings, and integrated Playback launch.
class AppController : public QObject
{
    Q_OBJECT

    // --- Auth & Session ---
    Q_PROPERTY(bool authenticated READ isAuthenticated NOTIFY authChanged)
    Q_PROPERTY(bool authBusy READ isAuthBusy NOTIFY authChanged)
    Q_PROPERTY(QString authError READ authError NOTIFY authChanged)
    Q_PROPERTY(QString serverUrl READ serverUrl NOTIFY serverChanged)
    Q_PROPERTY(QString serverName READ serverName NOTIFY serverChanged)
    Q_PROPERTY(QString serverVersion READ serverVersion NOTIFY serverChanged)
    Q_PROPERTY(QString userName READ userName NOTIFY authChanged)
    Q_PROPERTY(QStringList recentServers READ recentServers NOTIFY serverChanged)
    Q_PROPERTY(QString quickConnectCode READ quickConnectCode NOTIFY quickConnectChanged)
    Q_PROPERTY(bool quickConnectActive READ quickConnectActive NOTIFY quickConnectChanged)

    // --- Navigation ---
    Q_PROPERTY(QString currentView READ currentView NOTIFY navigationChanged)
    Q_PROPERTY(QString activeNavKey READ activeNavKey NOTIFY navigationChanged)
    Q_PROPERTY(QString pageTitle READ pageTitle NOTIFY navigationChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY navigationChanged)

    // --- Home ---
    Q_PROPERTY(bool homeLoading READ homeLoading NOTIFY homeChanged)
    Q_PROPERTY(QVariantMap heroItem READ heroItem NOTIFY homeChanged)
    Q_PROPERTY(MediaItemModel *librariesModel READ librariesModel CONSTANT)
    Q_PROPERTY(HomeSectionModel *homeSectionsModel READ homeSectionsModel CONSTANT)

    // --- Library ---
    Q_PROPERTY(MediaItemModel *libraryModel READ libraryModel CONSTANT)
    Q_PROPERTY(bool libraryLoading READ libraryLoading NOTIFY libraryChanged)
    Q_PROPERTY(int libraryTotalCount READ libraryTotalCount NOTIFY libraryChanged)
    Q_PROPERTY(bool libraryHasMore READ libraryHasMore NOTIFY libraryChanged)
    Q_PROPERTY(QString librarySortBy READ librarySortBy NOTIFY libraryChanged)
    Q_PROPERTY(bool libraryAscending READ libraryAscending NOTIFY libraryChanged)
    Q_PROPERTY(QString libraryGenre READ libraryGenre NOTIFY libraryChanged)
    Q_PROPERTY(int libraryYear READ libraryYear NOTIFY libraryChanged)
    Q_PROPERTY(QString libraryRating READ libraryRating NOTIFY libraryChanged)
    Q_PROPERTY(QString libraryPlayedFilter READ libraryPlayedFilter NOTIFY libraryChanged)
    Q_PROPERTY(bool libraryFavoritesOnly READ libraryFavoritesOnly NOTIFY libraryChanged)
    Q_PROPERTY(QString libraryViewMode READ libraryViewMode WRITE setLibraryViewMode NOTIFY libraryChanged)
    Q_PROPERTY(QStringList availableGenres READ availableGenres NOTIFY libraryFiltersChanged)
    Q_PROPERTY(QVariantList availableYears READ availableYears NOTIFY libraryFiltersChanged)
    Q_PROPERTY(QStringList availableRatings READ availableRatings NOTIFY libraryFiltersChanged)

    // --- Detail ---
    Q_PROPERTY(bool detailLoading READ detailLoading NOTIFY detailChanged)
    Q_PROPERTY(QVariantMap detailItem READ detailItem NOTIFY detailChanged)
    Q_PROPERTY(MediaItemModel *seasonsModel READ seasonsModel CONSTANT)
    Q_PROPERTY(MediaItemModel *episodesModel READ episodesModel CONSTANT)
    Q_PROPERTY(QString selectedSeasonId READ selectedSeasonId NOTIFY detailChanged)

    // --- Search ---
    Q_PROPERTY(QString searchQuery READ searchQuery NOTIFY searchChanged)
    Q_PROPERTY(bool searchLoading READ searchLoading NOTIFY searchChanged)
    Q_PROPERTY(MediaItemModel *searchMoviesModel READ searchMoviesModel CONSTANT)
    Q_PROPERTY(MediaItemModel *searchSeriesModel READ searchSeriesModel CONSTANT)
    Q_PROPERTY(MediaItemModel *searchEpisodesModel READ searchEpisodesModel CONSTANT)
    Q_PROPERTY(MediaItemModel *searchPeopleModel READ searchPeopleModel CONSTANT)
    Q_PROPERTY(QStringList recentSearches READ recentSearches NOTIFY searchChanged)

    // --- Settings & Appearance ---
    Q_PROPERTY(QString themeMode READ themeMode WRITE setThemeMode NOTIFY settingsChanged)
    Q_PROPERTY(QString accentColor READ accentColor WRITE setAccentColor NOTIFY settingsChanged)
    Q_PROPERTY(bool reduceMotion READ reduceMotion WRITE setReduceMotion NOTIFY settingsChanged)
    Q_PROPERTY(bool dualCoreMode READ dualCoreMode WRITE setDualCoreMode NOTIFY settingsChanged)
    Q_PROPERTY(int cpuCoreCount READ cpuCoreCount CONSTANT)
    Q_PROPERTY(QString hwdecMode READ hwdecMode WRITE setHwdecMode NOTIFY settingsChanged)
    Q_PROPERTY(QString videoProfile READ videoProfile WRITE setVideoProfile NOTIFY settingsChanged)
    Q_PROPERTY(int maxBitrateMbps READ maxBitrateMbps WRITE setMaxBitrateMbps NOTIFY settingsChanged)
    Q_PROPERTY(bool autoSkipSegments READ autoSkipSegments WRITE setAutoSkipSegments NOTIFY settingsChanged)
    Q_PROPERTY(bool autoPlayNextEpisode READ autoPlayNextEpisode WRITE setAutoPlayNextEpisode NOTIFY settingsChanged)
    Q_PROPERTY(QString preferredAudioLanguage READ preferredAudioLanguage WRITE setPreferredAudioLanguage NOTIFY settingsChanged)
    Q_PROPERTY(QString preferredSubtitleLanguage READ preferredSubtitleLanguage WRITE setPreferredSubtitleLanguage NOTIFY settingsChanged)
    Q_PROPERTY(int subtitleFontSize READ subtitleFontSize WRITE setSubtitleFontSize NOTIFY settingsChanged)
    Q_PROPERTY(QString subtitleFontFamily READ subtitleFontFamily WRITE setSubtitleFontFamily NOTIFY settingsChanged)
    Q_PROPERTY(QString subtitleColor READ subtitleColor WRITE setSubtitleColor NOTIFY settingsChanged)
    Q_PROPERTY(QString subtitleOutlineColor READ subtitleOutlineColor WRITE setSubtitleOutlineColor NOTIFY settingsChanged)
    Q_PROPERTY(double subtitleOutlineSize READ subtitleOutlineSize WRITE setSubtitleOutlineSize NOTIFY settingsChanged)
    Q_PROPERTY(double subtitleShadowOffset READ subtitleShadowOffset WRITE setSubtitleShadowOffset NOTIFY settingsChanged)
    Q_PROPERTY(bool subtitleBackgroundBoxEnabled READ subtitleBackgroundBoxEnabled WRITE setSubtitleBackgroundBoxEnabled NOTIFY settingsChanged)
    Q_PROPERTY(QString subtitleBackgroundColor READ subtitleBackgroundColor WRITE setSubtitleBackgroundColor NOTIFY settingsChanged)
    Q_PROPERTY(int subtitlePositionPercent READ subtitlePositionPercent WRITE setSubtitlePositionPercent NOTIFY settingsChanged)
    Q_PROPERTY(bool subtitleAssOverride READ subtitleAssOverride WRITE setSubtitleAssOverride NOTIFY settingsChanged)
    Q_PROPERTY(QString audioDevice READ audioDevice WRITE setAudioDevice NOTIFY settingsChanged)
    Q_PROPERTY(bool audioPassthroughEnabled READ audioPassthroughEnabled WRITE setAudioPassthroughEnabled NOTIFY settingsChanged)
    Q_PROPERTY(bool audioNormalizationEnabled READ audioNormalizationEnabled WRITE setAudioNormalizationEnabled NOTIFY settingsChanged)
    Q_PROPERTY(QString audioChannelLayout READ audioChannelLayout WRITE setAudioChannelLayout NOTIFY settingsChanged)
    Q_PROPERTY(QString customMpvOptions READ customMpvOptions WRITE setCustomMpvOptions NOTIFY settingsChanged)
    Q_PROPERTY(QString mpvLogLevel READ mpvLogLevel WRITE setMpvLogLevel NOTIFY settingsChanged)
    Q_PROPERTY(QString cacheSizeText READ cacheSizeText NOTIFY cacheSizeChanged)

    // --- Window persistence ---
    Q_PROPERTY(int savedWindowWidth READ savedWindowWidth CONSTANT)
    Q_PROPERTY(int savedWindowHeight READ savedWindowHeight CONSTANT)
    Q_PROPERTY(bool savedWindowMaximized READ savedWindowMaximized CONSTANT)

    // --- Player ---
    Q_PROPERTY(PlayerController *player READ player CONSTANT)

public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

    ImageCache *imageCache() const { return m_imageCache; }
    PlayerController *player() const { return m_player; }

    void start();

    // Auth getters
    bool isAuthenticated() const { return m_authenticated; }
    bool isAuthBusy() const { return m_authBusy; }
    QString authError() const { return m_authError; }
    QString serverUrl() const { return m_serverUrl; }
    QString serverName() const { return m_serverName; }
    QString serverVersion() const { return m_serverVersion; }
    QString userName() const { return m_userName; }
    QStringList recentServers() const;
    QString quickConnectCode() const { return m_quickConnectCode; }
    bool quickConnectActive() const { return m_quickConnectActive; }

    // Navigation getters
    QString currentView() const { return m_currentView; }
    QString activeNavKey() const { return m_activeNavKey; }
    QString pageTitle() const { return m_pageTitle; }
    bool canGoBack() const { return !m_history.isEmpty(); }

    // Home getters
    bool homeLoading() const { return m_homeLoading; }
    QVariantMap heroItem() const { return m_heroItem; }
    MediaItemModel *librariesModel() const { return m_librariesModel; }
    HomeSectionModel *homeSectionsModel() const { return m_homeSectionsModel; }

    // Library getters
    MediaItemModel *libraryModel() const { return m_libraryModel; }
    bool libraryLoading() const { return m_libraryLoading; }
    int libraryTotalCount() const { return m_libraryTotalCount; }
    bool libraryHasMore() const;
    QString librarySortBy() const { return m_libraryQuery.sortBy; }
    bool libraryAscending() const { return m_libraryQuery.ascending; }
    QString libraryGenre() const;
    int libraryYear() const;
    QString libraryRating() const;
    QString libraryPlayedFilter() const;
    bool libraryFavoritesOnly() const { return m_libraryQuery.favoritesOnly; }
    QString libraryViewMode() const { return m_libraryViewMode; }
    void setLibraryViewMode(const QString &mode);
    QStringList availableGenres() const { return m_availableGenres; }
    QVariantList availableYears() const { return m_availableYears; }
    QStringList availableRatings() const { return m_availableRatings; }

    // Detail getters
    bool detailLoading() const { return m_detailLoading; }
    QVariantMap detailItem() const { return m_detailItem; }
    MediaItemModel *seasonsModel() const { return m_seasonsModel; }
    MediaItemModel *episodesModel() const { return m_episodesModel; }
    QString selectedSeasonId() const { return m_selectedSeasonId; }

    // Search getters
    QString searchQuery() const { return m_searchQuery; }
    bool searchLoading() const { return m_searchLoading; }
    MediaItemModel *searchMoviesModel() const { return m_searchMoviesModel; }
    MediaItemModel *searchSeriesModel() const { return m_searchSeriesModel; }
    MediaItemModel *searchEpisodesModel() const { return m_searchEpisodesModel; }
    MediaItemModel *searchPeopleModel() const { return m_searchPeopleModel; }
    QStringList recentSearches() const;

    // Settings getters/setters
    QString themeMode() const;
    void setThemeMode(const QString &mode);
    QString accentColor() const;
    void setAccentColor(const QString &hex);
    bool reduceMotion() const;
    void setReduceMotion(bool enabled);
    bool dualCoreMode() const;
    void setDualCoreMode(bool enabled);
    int cpuCoreCount() const;
    QString hwdecMode() const;
    void setHwdecMode(const QString &mode);
    QString videoProfile() const;
    void setVideoProfile(const QString &profile);
    int maxBitrateMbps() const;
    void setMaxBitrateMbps(int mbps);
    bool autoSkipSegments() const;
    void setAutoSkipSegments(bool enabled);
    bool autoPlayNextEpisode() const;
    void setAutoPlayNextEpisode(bool enabled);
    QString preferredAudioLanguage() const;
    void setPreferredAudioLanguage(const QString &lang);
    QString preferredSubtitleLanguage() const;
    void setPreferredSubtitleLanguage(const QString &lang);
    int subtitleFontSize() const;
    void setSubtitleFontSize(int size);
    QString subtitleFontFamily() const;
    void setSubtitleFontFamily(const QString &family);
    QString subtitleColor() const;
    void setSubtitleColor(const QString &argbHex);
    QString subtitleOutlineColor() const;
    void setSubtitleOutlineColor(const QString &argbHex);
    double subtitleOutlineSize() const;
    void setSubtitleOutlineSize(double size);
    double subtitleShadowOffset() const;
    void setSubtitleShadowOffset(double offset);
    bool subtitleBackgroundBoxEnabled() const;
    void setSubtitleBackgroundBoxEnabled(bool enabled);
    QString subtitleBackgroundColor() const;
    void setSubtitleBackgroundColor(const QString &argbHex);
    int subtitlePositionPercent() const;
    void setSubtitlePositionPercent(int percent);
    bool subtitleAssOverride() const;
    void setSubtitleAssOverride(bool enabled);
    QString audioDevice() const;
    void setAudioDevice(const QString &device);
    bool audioPassthroughEnabled() const;
    void setAudioPassthroughEnabled(bool enabled);
    bool audioNormalizationEnabled() const;
    void setAudioNormalizationEnabled(bool enabled);
    QString audioChannelLayout() const;
    void setAudioChannelLayout(const QString &layout);
    QString customMpvOptions() const;
    void setCustomMpvOptions(const QString &options);
    QString mpvLogLevel() const;
    void setMpvLogLevel(const QString &level);
    QString cacheSizeText() const;

    int savedWindowWidth() const;
    int savedWindowHeight() const;
    bool savedWindowMaximized() const;

    // --- Invokable actions from QML ---
    Q_INVOKABLE void probeServer(const QString &rawUrl);
    Q_INVOKABLE void login(const QString &username, const QString &password, bool rememberMe = true);
    Q_INVOKABLE void startQuickConnect();
    Q_INVOKABLE void cancelQuickConnect();
    Q_INVOKABLE void showServerSelect();
    Q_INVOKABLE void logout();

    Q_INVOKABLE void openHome();
    Q_INVOKABLE void openLibrary(const QString &libraryId, const QString &title, const QString &collectionType = {});
    Q_INVOKABLE void openFavorites();
    Q_INVOKABLE void openPerson(const QString &personId, const QString &personName);
    Q_INVOKABLE void openGenre(const QString &genre);
    Q_INVOKABLE void openDetail(const QString &itemId);
    Q_INVOKABLE void openItem(const QVariantMap &item);
    Q_INVOKABLE void openSearch();
    Q_INVOKABLE void openSettings();
    Q_INVOKABLE void goBack();

    Q_INVOKABLE void refreshHome();
    Q_INVOKABLE void setLibrarySort(const QString &sortBy, bool ascending);
    Q_INVOKABLE void setLibraryGenre(const QString &genre);
    Q_INVOKABLE void setLibraryYear(int year);
    Q_INVOKABLE void setLibraryRating(const QString &rating);
    Q_INVOKABLE void setLibraryPlayedFilter(const QString &filter); // "any", "played", "unplayed"
    Q_INVOKABLE void setLibraryFavoritesOnly(bool favoritesOnly);
    Q_INVOKABLE void resetLibraryFilters();
    Q_INVOKABLE void loadMoreLibraryItems();

    Q_INVOKABLE void selectSeason(const QString &seasonId);
    Q_INVOKABLE void setSearchQuery(const QString &query);
    Q_INVOKABLE void clearRecentSearches();

    Q_INVOKABLE void togglePlayed(const QString &itemId, bool currentlyPlayed);
    Q_INVOKABLE void toggleFavorite(const QString &itemId, bool currentlyFavorite);
    Q_INVOKABLE void playItem(const QString &itemId, bool resume = true);
    Q_INVOKABLE void playDirectUrl(const QUrl &url, qint64 startSeconds = 0, const QString &title = {});

    Q_INVOKABLE void clearImageCache();
    Q_INVOKABLE void refreshCacheSize();
    Q_INVOKABLE void saveWindowState(int width, int height, bool maximized);
    Q_INVOKABLE QString formatDuration(qint64 seconds) const;

signals:
    void authChanged();
    void serverChanged();
    void quickConnectChanged();
    void navigationChanged();
    void homeChanged();
    void libraryChanged();
    void libraryFiltersChanged();
    void detailChanged();
    void searchChanged();
    void settingsChanged();
    void cacheSizeChanged();
    void toast(const QString &message, const QString &kind);

private:
    struct NavEntry {
        QString view;
        QString navKey;
        QString title;
        QString itemId;
        JellyfinClient::ItemQuery libraryQuery;
    };

    void pushAndNavigate(const NavEntry &entry, bool pushCurrent = true);
    void restoreNavEntry(const NavEntry &entry);
    void enterAuthenticatedSession(const UserSession &session, bool rememberMe);
    void handleAuthFailureIfUnauthorized(const QString &error);
    void loadLibraryPage(bool reset);
    void loadLibraryFilters();
    void loadDetailItem(const QString &itemId);
    void executeSearch(const QString &term);
    void pollQuickConnectTick();
    void propagateUserState(const QString &itemId, bool played, bool isFavorite);
    std::optional<MediaItem> lookupCachedItem(const QString &itemId) const;
    static QString normalizeServerUrl(const QString &raw);
    static QVariantMap buildDetailVariant(const MediaItem &item);

    Settings *m_settings;
    NetworkQueue *m_networkQueue;
    JellyfinClient *m_client;
    ImageCache *m_imageCache;
    PlayerController *m_player;

    bool m_authenticated = false;
    bool m_authBusy = false;
    QString m_authError;
    QString m_serverUrl;
    QString m_serverName;
    QString m_serverVersion;
    QString m_userName;

    QString m_quickConnectSecret;
    QString m_quickConnectCode;
    bool m_quickConnectActive = false;
    int m_quickConnectPolls = 0;
    QTimer *m_quickConnectTimer;

    QString m_currentView = QStringLiteral("serverSelect");
    QString m_activeNavKey = QStringLiteral("home");
    QString m_pageTitle = QStringLiteral("Home");
    QString m_currentDetailItemId;
    QList<NavEntry> m_history;

    bool m_homeLoading = false;
    QVariantMap m_heroItem;
    MediaItemModel *m_librariesModel;
    HomeSectionModel *m_homeSectionsModel;
    int m_homeRefreshToken = 0;

    MediaItemModel *m_libraryModel;
    JellyfinClient::ItemQuery m_libraryQuery;
    bool m_libraryLoading = false;
    int m_libraryTotalCount = 0;
    int m_libraryRequestToken = 0;
    int m_libraryFiltersToken = 0;
    QString m_libraryViewMode = QStringLiteral("poster");
    QStringList m_availableGenres;
    QVariantList m_availableYears;
    QStringList m_availableRatings;

    bool m_detailLoading = false;
    MediaItem m_detailMediaItem;
    QVariantMap m_detailItem;
    MediaItemModel *m_seasonsModel;
    MediaItemModel *m_episodesModel;
    QString m_selectedSeasonId;
    int m_detailRequestToken = 0;

    QString m_searchQuery;
    bool m_searchLoading = false;
    int m_searchRequestToken = 0;
    QTimer *m_searchDebounceTimer;
    MediaItemModel *m_searchMoviesModel;
    MediaItemModel *m_searchSeriesModel;
    MediaItemModel *m_searchEpisodesModel;
    MediaItemModel *m_searchPeopleModel;

    qint64 m_cachedDiskBytes = 0;
    int m_cacheSizeToken = 0;
};
