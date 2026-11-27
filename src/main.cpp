#include "app/AppController.h"
#include "app/ImageProviders.h"
#include "app/MediaItemModel.h"
#include "player/MpvVideoItem.h"
#include "player/PlayerController.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QImage>
#include <QLoggingCategory>
#include <QMetaMethod>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <QTimer>
#include <QUrl>
#include <QVariant>

#include <clocale>
#include <functional>
#include <memory>

namespace {

Q_LOGGING_CATEGORY(lcMain, "cutefin.main")

// --debug-view support (only with --screenshot): drives the app to a given
// page so a visual audit can capture every screen headlessly.
//
//   home (default), library, detail-movie, detail-series, search:<term>,
//   settings, login | serverSelect, player-stats, player-drawer
//
// The player views need --play.

// Runs `fn` exactly once, the first time `ready()` holds -- checked now and
// again after each emission of `signal` on `sender`.
template<typename Sender, typename Signal>
void runWhen(Sender *sender, Signal signal, const std::function<bool()> &ready, const std::function<void()> &fn)
{
    if (ready()) {
        QTimer::singleShot(0, sender, fn);
        return;
    }
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = QObject::connect(sender, signal, sender, [sender, connection, ready, fn]() {
        if (!ready())
            return;
        QObject::disconnect(*connection);
        // Let the emitting code finish its own navigation first.
        QTimer::singleShot(0, sender, fn);
    });
}

const MediaItem *findLibrary(const MediaItemModel *libraries, const QString &collectionType)
{
    const QList<MediaItem> &items = libraries->items();
    for (const MediaItem &item : items) {
        if (item.collectionType.compare(collectionType, Qt::CaseInsensitive) == 0)
            return &item;
    }
    return nullptr;
}

// Opens the first library of `collectionType`; then, if `openFirstItem`,
// the detail page of its first item.
void openLibraryView(AppController *app, const QString &collectionType, bool openFirstItem)
{
    MediaItemModel *libraries = app->librariesModel();
    runWhen(
        libraries, &MediaItemModel::countChanged, [libraries]() { return libraries->rowCount() > 0; },
        [app, libraries, collectionType, openFirstItem]() {
            const MediaItem *match = findLibrary(libraries, collectionType);
            if (!match) {
                qCWarning(lcMain, "--debug-view: no \"%ls\" library; using the first one",
                          qUtf16Printable(collectionType));
            }
            const MediaItem library = match ? *match : libraries->items().constFirst();
            app->openLibrary(library.id, library.name, library.collectionType);
            if (!openFirstItem)
                return;

            MediaItemModel *items = app->libraryModel();
            runWhen(
                items, &MediaItemModel::countChanged, [items]() { return items->rowCount() > 0; },
                [app, items]() { app->openDetail(items->items().constFirst().id); });
        });
}

void runAfterAuthentication(AppController *app, const std::function<void()> &fn)
{
    runWhen(
        app, &AppController::authChanged, [app]() { return app->isAuthenticated() && !app->isAuthBusy(); }, fn);
}

// Calls a QML function on the root window, matching a `string`-typed or an
// untyped (`var`) parameter.
bool invokeRootFunction(QObject *root, const char *name, const QString &argument)
{
    if (!root)
        return false;
    const QMetaObject *meta = root->metaObject();
    for (int i = meta->methodOffset(); i < meta->methodCount(); ++i) {
        const QMetaMethod method = meta->method(i);
        if (method.name() != name || method.parameterCount() != 1)
            continue;
        if (method.parameterMetaType(0) == QMetaType::fromType<QString>())
            return method.invoke(root, Qt::DirectConnection, Q_ARG(QString, argument));
        return method.invoke(root, Qt::DirectConnection, Q_ARG(QVariant, QVariant(argument)));
    }
    return false;
}

// Returns false if the view must not call AppController::start() (login).
bool applyDebugView(const QString &view, AppController *app, QObject *rootWindow, bool playing, int screenshotDelayMs)
{
    const QString name = view.trimmed();
    if (name.isEmpty() || name == QLatin1String("home"))
        return true;

    if (name == QLatin1String("login") || name.compare(QLatin1String("serverSelect"), Qt::CaseInsensitive) == 0) {
        // Simply never start the session: AppController begins on the
        // server-select page. (showServerSelect() would log out and drop the
        // saved token.)
        return false;
    }
    if (name == QLatin1String("library")) {
        openLibraryView(app, QStringLiteral("movies"), false);
    } else if (name == QLatin1String("detail-movie")) {
        openLibraryView(app, QStringLiteral("movies"), true);
    } else if (name == QLatin1String("detail-series")) {
        openLibraryView(app, QStringLiteral("tvshows"), true);
    } else if (name.startsWith(QLatin1String("search:")) || name == QLatin1String("search")) {
        const int colon = name.indexOf(QLatin1Char(':'));
        const QString term = colon < 0 ? QString() : name.mid(colon + 1);
        runAfterAuthentication(app, [app, term]() {
            app->openSearch();
            if (!term.isEmpty())
                app->setSearchQuery(term);
        });
    } else if (name == QLatin1String("settings")) {
        runAfterAuthentication(app, [app]() { app->openSettings(); });
    } else if (name == QLatin1String("player-stats") || name == QLatin1String("player-drawer")) {
        if (!playing) {
            qCWarning(lcMain, "--debug-view %ls needs --play", qUtf16Printable(name));
            return true;
        }
        if (name == QLatin1String("player-stats")) {
            app->player()->setStatsVisible(true);
        } else {
            // Give the player page a moment to load before opening its drawer.
            const QPointer<QObject> root(rootWindow);
            QTimer::singleShot(qBound(300, screenshotDelayMs / 3, 1500), app, [root]() {
                if (!invokeRootFunction(root, "openDebugDrawer", QStringLiteral("tracks")))
                    qCWarning(lcMain, "--debug-view player-drawer: root has no openDebugDrawer(tab)");
            });
        }
    } else {
        qCWarning(lcMain, "unknown --debug-view \"%ls\"; showing home", qUtf16Printable(name));
    }
    return true;
}

} // namespace

// Usage: cutefin [--play <url> [--start <seconds>] [--title <text>]]
//                [--screenshot <path> [--screenshot-delay <ms>] [--debug-view <name>]]
int main(int argc, char *argv[])
{
#if defined(Q_OS_LINUX)
    // libmpv's GL interop (VA-API/VDPAU) wants an EGL context; Qt's default
    // GLX integration on X11 makes hwdec fall back to software copies.
    if (qEnvironmentVariableIsEmpty("QT_XCB_GL_INTEGRATION"))
        qputenv("QT_XCB_GL_INTEGRATION", "xcb_egl");
#endif

    // libmpv renders through OpenGL, so the scene graph must use it too. Both
    // this and the surface format must be set before the application exists.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

#ifdef Q_OS_MACOS
    // macOS only hands out modern OpenGL through an explicit core profile.
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(0);
    format.setStencilBufferSize(8);
    QSurfaceFormat::setDefaultFormat(format);
#endif

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QGuiApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("cutefin"));
    app.setApplicationName(QStringLiteral("cutefin"));
    app.setApplicationDisplayName(QStringLiteral("Cutefin"));
    app.setDesktopFileName(QStringLiteral("cutefin"));

    // libmpv refuses to initialize unless LC_NUMERIC is "C". QGuiApplication's
    // constructor applies the system locale, so this has to come after it.
    std::setlocale(LC_NUMERIC, "C");

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption playOption(QStringLiteral("play"), QStringLiteral("Play a URL directly."),
                                        QStringLiteral("url"));
    const QCommandLineOption startOption(QStringLiteral("start"),
                                         QStringLiteral("Start position in seconds for --play."),
                                         QStringLiteral("seconds"), QStringLiteral("0"));
    const QCommandLineOption titleOption(QStringLiteral("title"), QStringLiteral("Title shown for --play."),
                                         QStringLiteral("text"));
    const QCommandLineOption screenshotOption(QStringLiteral("screenshot"),
                                              QStringLiteral("Debug: save a PNG screenshot after delay and exit."),
                                              QStringLiteral("path"));
    const QCommandLineOption screenshotDelayOption(QStringLiteral("screenshot-delay"),
                                                   QStringLiteral("Delay in ms before --screenshot captures."),
                                                   QStringLiteral("ms"), QStringLiteral("3500"));
    const QCommandLineOption debugViewOption(
        QStringLiteral("debug-view"),
        QStringLiteral("Debug (with --screenshot): page to capture -- home, library, detail-movie, detail-series, "
                       "search:<term>, settings, login, player-stats, player-drawer (player views need --play)."),
        QStringLiteral("name"), QStringLiteral("home"));
    parser.addOption(playOption);
    parser.addOption(startOption);
    parser.addOption(titleOption);
    parser.addOption(screenshotOption);
    parser.addOption(screenshotDelayOption);
    parser.addOption(debugViewOption);
    parser.process(app);

    qmlRegisterType<MpvVideoItem>("Cutefin", 1, 0, "MpvVideoItem");

    // Declared before the engine so it outlives every QML object and image
    // provider that points into it.
    AppController appController;

    // Last chance to tell the server where playback stopped: end any active
    // session and wait (bounded) for its Stopped report to leave the
    // process before the network stack is torn down.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &appController,
                     [&appController]() { appController.player()->flushPendingStopped(); });

    QQmlApplicationEngine engine;
    // The engine takes ownership of the providers.
    engine.addImageProvider(QStringLiteral("jellyfin"), new JellyfinImageProvider(appController.imageCache()));
    engine.addImageProvider(QStringLiteral("blurhash"), new BlurHashImageProvider);
    engine.addImageProvider(QStringLiteral("trickplay"),
                            new TrickplayImageProvider(appController.player()->trickplayCache()));

    engine.rootContext()->setContextProperty(QStringLiteral("app"), &appController);
    engine.rootContext()->setContextProperty(QStringLiteral("player"), appController.player());

    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Cutefin/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    QObject *rootObject = engine.rootObjects().constFirst();
    const bool screenshotMode = parser.isSet(screenshotOption);
    const int screenshotDelayMs = qMax(500, parser.value(screenshotDelayOption).toInt());
    const bool playing = parser.isSet(playOption);

    if (playing) {
        appController.playDirectUrl(QUrl::fromUserInput(parser.value(playOption)),
                                    parser.value(startOption).toLongLong(), parser.value(titleOption));
    }

    bool startSession = !playing;
    if (screenshotMode && parser.isSet(debugViewOption)) {
        // Hooks are installed before start() so no early signal is missed.
        if (!applyDebugView(parser.value(debugViewOption), &appController, rootObject, playing, screenshotDelayMs))
            startSession = false;
    }
    if (startSession)
        appController.start();

    if (screenshotMode) {
        const QString outPath = parser.value(screenshotOption);
        if (auto *window = qobject_cast<QQuickWindow *>(rootObject)) {
            QTimer::singleShot(screenshotDelayMs, window, [&app, &appController, window, outPath]() {
                const QImage frame = window->grabWindow();
                if (!frame.save(outPath))
                    qCWarning(lcMain, "could not save screenshot to %ls", qUtf16Printable(outPath));
                if (appController.player()->isOpen())
                    appController.player()->stopAndClose();
                QTimer::singleShot(150, &app, &QGuiApplication::quit);
            });
        }
    }

    return app.exec();
}
