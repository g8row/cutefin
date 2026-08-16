#include "ui/MainWindow.h"

#include <QApplication>
#include <QSurfaceFormat>

#ifdef CUTEFIN_ENABLE_MPV
#include "player/PlayerWindow.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QUrl>

#include <clocale>
#endif

namespace {

#ifdef CUTEFIN_ENABLE_MPV
// --------------------------------------------------------------------------
// TEMPORARY debug entry point for Phase 2a, added additively so the embedded
// player can be exercised standalone before the rest of the UI exists:
//
//   cutefin --play <url> [--start <seconds>] [--title <text>]
//
// A later phase replaces main()'s normal (no-args) path with the real
// MainWindow/ServerSelectPage flow; this block should be removed then. It
// intentionally does not touch anything outside this guarded block.
// --------------------------------------------------------------------------
int runPlayerDebugHarness(QApplication &app, const QString &url, qint64 startSeconds, const QString &title)
{
    PlayerWindow window(nullptr);
    window.resize(1280, 720);
    window.playUrl(QUrl(url), startSeconds, title);
    window.show();
    return app.exec();
}
#endif

} // namespace

// Sets up the OpenGL surface format the player needs (must happen before
// QApplication is constructed, see DESIGN.md §7), then either runs the
// `--play` debug harness (see above) or the real MainWindow flow.
int main(int argc, char *argv[])
{
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(0);
    format.setStencilBufferSize(8);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("cutefin"));
    app.setApplicationName(QStringLiteral("cutefin"));

#ifdef CUTEFIN_ENABLE_MPV
    // libmpv hard-requires LC_NUMERIC == "C" (it parses/formats some option
    // and property values with the C locale's decimal point and will refuse
    // to initialize otherwise). QApplication's constructor calls
    // setlocale(LC_ALL, "") internally, which pulls LC_NUMERIC from the
    // system locale and breaks that invariant -- so this must run *after*
    // QApplication is constructed and before any MpvWidget exists. Do not
    // remove this; it looks like dead code but silently makes every mpv
    // handle fail to initialize.
    std::setlocale(LC_NUMERIC, "C");

    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption playOption(QStringLiteral("play"), QStringLiteral("Debug: play a URL directly."),
                                   QStringLiteral("url"));
    QCommandLineOption startOption(QStringLiteral("start"), QStringLiteral("Debug: start position in seconds."),
                                    QStringLiteral("seconds"), QStringLiteral("0"));
    QCommandLineOption titleOption(QStringLiteral("title"), QStringLiteral("Debug: window title."),
                                    QStringLiteral("text"));
    parser.addOption(playOption);
    parser.addOption(startOption);
    parser.addOption(titleOption);
    parser.process(app);

    if (parser.isSet(playOption)) {
        return runPlayerDebugHarness(app, parser.value(playOption), parser.value(startOption).toLongLong(),
                                      parser.value(titleOption));
    }
#endif

    MainWindow window;
    window.show();
    window.start();

    return app.exec();
}
