#pragma once

#include <QList>
#include <QMetaType>
#include <QMutex>
#include <QQuickFramebufferObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <optional>

struct mpv_handle;
struct mpv_event;

// Embeds libmpv directly into the Qt Quick OpenGL Scene Graph via
// QQuickFramebufferObject — zero external windows, works across macOS, Linux,
// and Windows when QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL)
// is active.
class MpvVideoItem : public QQuickFramebufferObject
{
    Q_OBJECT

    Q_PROPERTY(bool usable READ isUsable CONSTANT)
    Q_PROPERTY(bool active READ isActive NOTIFY activeChanged)
    Q_PROPERTY(bool paused READ isPaused NOTIFY pausedChanged)
    Q_PROPERTY(bool buffering READ isBuffering NOTIFY bufferingChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(double cacheDuration READ cacheDuration NOTIFY cacheDurationChanged)
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ isMuted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY speedChanged)
    Q_PROPERTY(QVariantList audioTracks READ audioTracksVariant NOTIFY tracksChanged)
    Q_PROPERTY(QVariantList subtitleTracks READ subtitleTracksVariant NOTIFY tracksChanged)
    Q_PROPERTY(int selectedAudioId READ selectedAudioId NOTIFY tracksChanged)
    Q_PROPERTY(int selectedSubtitleId READ selectedSubtitleId NOTIFY tracksChanged)
    Q_PROPERTY(int selectedSecondarySubtitleId READ selectedSecondarySubtitleId NOTIFY tracksChanged)
    Q_PROPERTY(QString aspectMode READ aspectMode NOTIFY aspectModeChanged)

public:
    struct Track {
        int id = -1;
        QString type; // "audio", "sub", "video"
        QString lang;
        QString title;
        bool selected = false;
        QString codec;
        bool forced = false;
        bool hearingImpaired = false;
        bool external = false;
    };

    struct AudioDevice {
        QString name;
        QString description;
    };

    explicit MpvVideoItem(QQuickItem *parent = nullptr);
    ~MpvVideoItem() override;

    Renderer *createRenderer() const override;

    // Accessors for the QQuickFramebufferObject::Renderer on the render thread.
    mpv_handle *mpvHandle() const { return m_mpv; }
    void notifyRenderContextReady();
    void notifyRenderContextDestroyed();
    void requestFrameUpdate();

    bool isUsable() const { return m_mpv != nullptr; }
    bool isActive() const { return m_active; }
    bool isPaused() const { return m_paused; }
    bool isBuffering() const { return m_buffering; }
    double position() const { return m_position; }
    double duration() const { return m_duration; }
    double cacheDuration() const { return m_cacheDuration; }
    int volume() const { return m_volume; }
    bool isMuted() const { return m_muted; }
    double speed() const { return m_speed; }
    QString aspectMode() const { return m_aspectMode; }

    // Everything QML binds to is served from state cached off observed
    // property events: none of these getters calls into libmpv.
    QList<Track> tracks() const { return m_tracks; }
    QVariantList audioTracksVariant() const { return m_audioTracksVariant; }
    QVariantList subtitleTracksVariant() const { return m_subtitleTracksVariant; }
    int selectedAudioId() const { return m_selectedAudioId; }
    int selectedSubtitleId() const { return m_selectedSubtitleId; }
    int selectedSecondarySubtitleId() const { return m_secondarySubId; }
    bool isPausedForCache() const { return m_pausedForCache; }
    bool isCoreIdle() const { return m_coreIdle; }

    Q_INVOKABLE void play(const QUrl &url, qint64 startSeconds = 0, const QStringList &headerFields = {});
    Q_INVOKABLE void stop();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void seekRelative(double deltaSeconds);
    Q_INVOKABLE void setVolume(int volumePercent);
    Q_INVOKABLE void setMuted(bool muted);
    Q_INVOKABLE void setSpeed(double speed);
    Q_INVOKABLE void setAudioTrack(int id);
    Q_INVOKABLE void setSubtitleTrack(int id);
    Q_INVOKABLE void setSecondarySubtitleTrack(int id);
    Q_INVOKABLE void addSubtitleUrl(const QUrl &url, const QString &title, const QString &language);
    Q_INVOKABLE void frameStep();
    Q_INVOKABLE void frameBackStep();
    Q_INVOKABLE void cycleAspectMode();
    Q_INVOKABLE QString takeScreenshot();

    void applyPerformanceProfile(bool dualCoreMode, const QString &hwdecMode, const QString &videoProfile);

    // mpv log verbosity ("no", "fatal", "error", "warn", "info", "v", "debug",
    // "trace"). Empty means "warn". CUTEFIN_MPV_LOGLEVEL, when set, always
    // wins so a debugging session can't be silenced by the saved setting.
    void setLogLevel(const QString &level);
    QString logLevel() const { return m_logLevel; }

    // Strips credentials (api_key query values, MediaBrowser Token="...")
    // from a line before it is logged, kept, or copied to the clipboard.
    static QString redactSecrets(const QString &line);

    QString stringProperty(const char *name) const;
    double doubleProperty(const char *name, double fallback = 0.0) const;
    qint64 intProperty(const char *name, qint64 fallback = 0) const;
    bool flagProperty(const char *name, bool fallback = false) const;
    void setProperty(const char *name, const QString &value);
    bool setPropertyChecked(const char *name, const QString &value);

    QList<AudioDevice> audioDevices() const;
    QVariantList audioDevicesVariant() const;
    QStringList applyOptionLines(const QStringList &lines);

    QVariantMap collectStatsSnapshot() const;
    QStringList recentLogs() const { return m_recentLogs; }

signals:
    void activeChanged();
    void pausedChanged();
    void bufferingChanged();
    void positionChanged();
    void durationChanged();
    void cacheDurationChanged();
    void volumeChanged();
    void mutedChanged();
    void speedChanged();
    void tracksChanged();
    void aspectModeChanged();
    void fileLoaded();
    void endOfFile();
    void seeked();
    void mpvLogMessage(const QString &message);
    void mpvFatalError(const QString &message);

private:
    static void onWakeup(void *ctx);

    void processMpvEvents();
    void handleEvent(const mpv_event *event);
    void observeProperties();
    void updateTracksFromNode(void *nodePtr);
    void refreshBuffering();
    void resetFileState();
    void command(const QStringList &args);

    mpv_handle *m_mpv = nullptr;
    bool m_renderContextReady = false;

    struct PendingPlay {
        QUrl url;
        qint64 startSeconds = 0;
        QStringList headerFields;
    };
    std::optional<PendingPlay> m_pendingPlay;

    bool m_active = false;
    bool m_paused = false;
    bool m_buffering = false;
    double m_position = 0.0;
    double m_lastEmittedPosition = -1.0;
    double m_duration = 0.0;
    double m_cacheDuration = 0.0;
    int m_volume = 100;
    bool m_muted = false;
    double m_speed = 1.0;
    int m_selectedAudioId = -1;
    int m_selectedSubtitleId = -1;
    int m_secondarySubId = -1;
    bool m_pausedForCache = false;
    bool m_coreIdle = true;
    bool m_eofReached = false;
    int m_aspectIndex = 0;
    QString m_aspectMode = QStringLiteral("Fit");
    QList<Track> m_tracks;
    QVariantList m_audioTracksVariant;
    QVariantList m_subtitleTracksVariant;
    QStringList m_recentLogs;
    QString m_logLevel;

    // Set by the mpv wakeup callback (any thread), cleared by the GUI thread
    // right before it drains the event queue: at most one drain is ever
    // queued, however often mpv wakes us.
    std::atomic_bool m_wakeupPending{false};
};

Q_DECLARE_METATYPE(MpvVideoItem::Track)
