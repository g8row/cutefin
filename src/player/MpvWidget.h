#pragma once

#include <QList>
#include <QMetaType>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QString>
#include <QStringList>
#include <QUrl>

struct mpv_handle;
struct mpv_render_context;
struct mpv_event;

// Embeds libmpv in a Qt Widgets application through the render API (the only
// approach that works on both macOS and Linux from a widget host -- see
// DESIGN.md §7). All mpv calls that touch the render context happen on the
// GUI thread; mpv's own wakeup/update callbacks fire on mpv-internal threads
// and only ever hop back to Qt through a queued QMetaObject::invokeMethod --
// they must never touch Qt objects directly.
class MpvWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT
public:
    struct Track {
        int id = -1;
        QString type; // "audio", "sub", "video"
        QString lang;
        QString title;
        bool selected = false;
    };

    explicit MpvWidget(QWidget *parent = nullptr);
    ~MpvWidget() override;

    // headerFields are raw "Header: value" strings and are only meaningful
    // for URLs that need an Authorization header, i.e. transcode URLs -- a
    // direct-play URL already carries api_key in its query string (see
    // DESIGN.md §7).
    void play(const QUrl &url, qint64 startSeconds = 0, const QStringList &headerFields = {});
    void stop();

    void pause();
    void resume();
    void togglePause();
    bool isPaused() const { return m_paused; }

    void seek(qint64 seconds);
    void seekRelative(qint64 deltaSeconds);

    void setVolume(int volumePercent);
    int volume() const { return m_volume; }
    void setMuted(bool muted);
    bool isMuted() const { return m_muted; }

    void setAudioTrack(int id);
    void setSubtitleTrack(int id);
    // mpv's `secondary-sid`. Pass -1 to disable.
    void setSecondarySubtitleTrack(int id);

    // Loads an external subtitle file/URL into the running mpv instance.
    // Jellyfin's IsExternal streams are not inside the container, so mpv
    // cannot see them without this (FEATURES.md §C2).
    void addSubtitleUrl(const QUrl &url, const QString &title, const QString &language);

    void setSpeed(double speed);
    double speed() const { return m_speed; }

    // --- Generic property access ---
    // The stats overlay and the subtitle/audio presentation settings both
    // need a long tail of mpv properties that would be pointless to wrap
    // one-by-one. These are thin, synchronous wrappers over
    // mpv_get_property/mpv_set_property_string; all are no-ops (returning the
    // given default) when mpv failed to initialize.
    QString stringProperty(const char *name) const;
    double doubleProperty(const char *name, double fallback = 0.0) const;
    qint64 intProperty(const char *name, qint64 fallback = 0) const;
    bool flagProperty(const char *name, bool fallback = false) const;
    void setProperty(const char *name, const QString &value);

    // Applies free-form `key=value` lines (an mpv.conf fragment). Returns the
    // keys mpv rejected, so the settings UI can report them rather than
    // swallowing them (FEATURES.md §C5 "Advanced").
    QStringList applyOptionLines(const QStringList &lines);

    qint64 position() const { return m_position; }
    qint64 duration() const { return m_duration; }
    QList<Track> tracks() const { return m_tracks; }

    bool isUsable() const { return m_mpv != nullptr; }

signals:
    void positionChanged(qint64 seconds);
    void durationChanged(qint64 seconds);
    void pausedChanged(bool paused);
    void coreIdleChanged(bool idle);
    void endOfFile();
    void fileLoaded();
    void seeked();
    void tracksChanged(const QList<MpvWidget::Track> &tracks);
    void mpvLogMessage(const QString &message);
    void mpvFatalError(const QString &message);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

private:
    static void onRenderUpdate(void *ctx);
    static void onWakeup(void *ctx);

    void processMpvEvents();
    void handleEvent(const mpv_event *event);
    void observeProperties();
    void updateTracksFromNode(void *nodePtr);
    void command(const QStringList &args);

    mpv_handle *m_mpv = nullptr;
    mpv_render_context *m_mpvGl = nullptr;

    qint64 m_position = 0;
    qint64 m_duration = 0;
    bool m_paused = false;
    bool m_muted = false;
    int m_volume = 100;
    double m_speed = 1.0;
    QList<Track> m_tracks;
};

Q_DECLARE_METATYPE(MpvWidget::Track)
