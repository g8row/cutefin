#pragma once

#include "api/Models.h"
#include "player/MpvVideoItem.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <optional>

class JellyfinClient;
class NetworkQueue;
class PlaybackReporter;
class Settings;
class SleepInhibitor;
class TrickplayCache;
class QNetworkReply;
class QTimer;

// Orchestrates full Jellyfin playback inside the single main window around an
// attached MpvVideoItem: playback resolution, progress reporting, chapters,
// trickplay, external subtitles, remembered per-series tracks, skip intro/outro,
// next-episode countdown, subtitle/audio presentation, quality switching, and
// the 2 Hz stats overlay.
class PlayerController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(MpvVideoItem *videoItem READ videoItem WRITE setVideoItem NOTIFY videoItemChanged)
    Q_PROPERTY(bool open READ isOpen NOTIFY openChanged)
    Q_PROPERTY(bool loading READ isLoading NOTIFY loadingChanged)
    Q_PROPERTY(QString itemId READ itemId NOTIFY itemChanged)
    Q_PROPERTY(QString title READ title NOTIFY itemChanged)
    Q_PROPERTY(QString subtitle READ subtitle NOTIFY itemChanged)
    Q_PROPERTY(QVariantList chapters READ chapters NOTIFY chaptersChanged)
    Q_PROPERTY(bool trickplayEnabled READ trickplayEnabled NOTIFY trickplayChanged)
    Q_PROPERTY(int trickplayRevision READ trickplayRevision NOTIFY trickplayChanged)

    Q_PROPERTY(bool skipActive READ skipActive NOTIFY skipChanged)
    Q_PROPERTY(QString skipLabel READ skipLabel NOTIFY skipChanged)

    Q_PROPERTY(bool nextEpisodeVisible READ nextEpisodeVisible NOTIFY nextEpisodeChanged)
    Q_PROPERTY(QString nextEpisodeTitle READ nextEpisodeTitle NOTIFY nextEpisodeChanged)
    Q_PROPERTY(QString nextEpisodeSubtitle READ nextEpisodeSubtitle NOTIFY nextEpisodeChanged)
    Q_PROPERTY(QString nextEpisodeThumbUrl READ nextEpisodeThumbUrl NOTIFY nextEpisodeChanged)
    Q_PROPERTY(int nextEpisodeCountdown READ nextEpisodeCountdown NOTIFY nextEpisodeChanged)

    Q_PROPERTY(bool transcoding READ isTranscoding NOTIFY playbackMetaChanged)
    Q_PROPERTY(QStringList transcodeReasons READ transcodeReasons NOTIFY playbackMetaChanged)
    Q_PROPERTY(int maxBitrateMbps READ maxBitrateMbps WRITE setMaxBitrateMbps NOTIFY playbackMetaChanged)

    Q_PROPERTY(double subtitleDelay READ subtitleDelay NOTIFY presentationChanged)
    Q_PROPERTY(double audioDelay READ audioDelay NOTIFY presentationChanged)
    Q_PROPERTY(QVariantList audioDevices READ audioDevices NOTIFY audioDevicesChanged)

    Q_PROPERTY(bool statsVisible READ statsVisible WRITE setStatsVisible NOTIFY statsVisibleChanged)
    Q_PROPERTY(QVariantMap stats READ stats NOTIFY statsChanged)

public:
    explicit PlayerController(JellyfinClient *client, NetworkQueue *networkQueue, Settings *settings,
                              QObject *parent = nullptr);
    ~PlayerController() override;

    TrickplayCache *trickplayCache() const { return m_trickplay; }

    MpvVideoItem *videoItem() const { return m_videoItem; }
    void setVideoItem(MpvVideoItem *item);

    bool isOpen() const { return m_open; }
    bool isLoading() const { return m_loading; }
    QString itemId() const { return m_itemId; }
    QString title() const { return m_title; }
    QString subtitle() const { return m_subtitle; }
    QVariantList chapters() const { return m_chapters; }
    bool trickplayEnabled() const;
    int trickplayRevision() const { return m_trickplayRevision; }

    bool skipActive() const { return m_skipActive; }
    QString skipLabel() const { return m_skipLabel; }

    bool nextEpisodeVisible() const { return m_nextEpisodeVisible; }
    QString nextEpisodeTitle() const { return m_nextEpisodeTitle; }
    QString nextEpisodeSubtitle() const { return m_nextEpisodeSubtitle; }
    QString nextEpisodeThumbUrl() const { return m_nextEpisodeThumbUrl; }
    int nextEpisodeCountdown() const { return m_nextEpisodeCountdown; }

    bool isTranscoding() const { return m_transcoding; }
    QStringList transcodeReasons() const { return m_transcodeReasons; }
    int maxBitrateMbps() const { return m_maxBitrateMbps; }
    void setMaxBitrateMbps(int mbps);

    double subtitleDelay() const;
    double audioDelay() const;
    QVariantList audioDevices() const { return m_audioDevices; }

    bool statsVisible() const { return m_statsVisible; }
    void setStatsVisible(bool visible);
    QVariantMap stats() const { return m_stats; }

    // Entry points
    void startItemPlayback(const MediaItem &item, bool resume);
    Q_INVOKABLE void playDirectUrl(const QUrl &url, qint64 startSeconds = 0, const QString &title = {});
    Q_INVOKABLE void stopAndClose();

    // True while a playback session is being reported to the server (a
    // Stopped report is still owed).
    bool hasActivePlaybackReport() const;
    // For app shutdown (QCoreApplication::aboutToQuit): ends any active
    // session, then pumps a local event loop until every Stopped report
    // already sent has finished on the wire, or `timeoutMs` elapses. Without
    // this the request dies with the process and "Continue Watching" keeps
    // the stale position. Returns true if nothing was left outstanding.
    Q_INVOKABLE bool flushPendingStopped(int timeoutMs = 800);

    // Controls invoked from PlayerView / PlayerDrawer
    Q_INVOKABLE void selectAudioTrack(int trackId);
    Q_INVOKABLE void selectSubtitleTrack(int trackId);
    Q_INVOKABLE void selectSecondarySubtitleTrack(int trackId);
    Q_INVOKABLE void jumpChapter(int direction);
    Q_INVOKABLE void seekToChapter(int index);
    Q_INVOKABLE void requestTrickplay(qint64 positionMs);
    Q_INVOKABLE void triggerSkip();
    Q_INVOKABLE void playNextEpisode();
    Q_INVOKABLE void cancelNextEpisode();
    Q_INVOKABLE void adjustSubtitleDelay(double deltaSeconds);
    Q_INVOKABLE void adjustAudioDelay(double deltaSeconds);
    Q_INVOKABLE void refreshAudioDevices();
    Q_INVOKABLE void applyPresentationSettings();
    Q_INVOKABLE QStringList applyCustomMpvOptions(const QString &rawText);
    Q_INVOKABLE void copyDiagnosticsToClipboard();
    Q_INVOKABLE void takeScreenshot();

signals:
    void videoItemChanged();
    void openChanged();
    void loadingChanged();
    void itemChanged();
    void chaptersChanged();
    void trickplayChanged();
    void skipChanged();
    void nextEpisodeChanged();
    void playbackMetaChanged();
    void presentationChanged();
    void audioDevicesChanged();
    void statsVisibleChanged();
    void statsChanged();
    void closed();
    void toast(const QString &message, const QString &kind);
    // Internal: a Stopped report finished (success or failure).
    void pendingStoppedReportsChanged();

private:
    void startPlaybackWithSettings(const MediaItem &item, qint64 startSeconds, bool forceTranscode);
    void applyItemDetails(const MediaItem &full);
    void loadExternalSubtitles(const MediaItem &item);
    void autoSelectTracks();
    void updateAudioPassthroughForSelectedTrack();
    void fetchSegmentsAndNextEpisode(const MediaItem &item);
    void resolveNextEpisode(const MediaItem &item);
    void onPositionUpdated();
    void onPausedUpdated();
    void onEndOfFile();
    void pollStats();
    void resetItemState();
    void playOrDefer(const QUrl &url, qint64 startSeconds, const QStringList &headerFields = {});
    void applySubtitleSettings();
    void applyAudioSettings();
    void applyEngineSettings();
    void onNetworkReplyFinished(QNetworkReply *reply);

    JellyfinClient *m_client;
    Settings *m_settings;
    PlaybackReporter *m_reporter;
    TrickplayCache *m_trickplay;
    SleepInhibitor *m_sleepInhibitor;
    QPointer<MpvVideoItem> m_videoItem;
    QTimer *m_statsTimer;

    bool m_open = false;
    bool m_loading = false;
    QString m_itemId;
    QString m_title;
    QString m_subtitle;
    QString m_playSessionId;
    QString m_mediaSourceId;
    QString m_container;
    qint64 m_fileSize = 0;
    qint64 m_totalBitrate = 0;
    bool m_transcoding = false;
    QStringList m_transcodeReasons;
    int m_maxBitrateMbps = 0;

    MediaItem m_currentItem;
    QString m_currentSeriesId;
    QVariantList m_chapters;
    int m_trickplayRevision = 0;

    QList<MediaSegment> m_segments;
    bool m_skipActive = false;
    QString m_skipLabel;
    qint64 m_skipTargetSeconds = -1;
    qint64 m_autoSkippedToSeconds = -1;

    MediaItem m_nextEpisode;
    bool m_nextEpisodeCancelled = false;
    bool m_nextEpisodeVisible = false;
    QString m_nextEpisodeTitle;
    QString m_nextEpisodeSubtitle;
    QString m_nextEpisodeThumbUrl;
    int m_nextEpisodeCountdown = 0;

    bool m_userChangedAudioTrack = false;
    bool m_userChangedSubtitleTrack = false;
    bool m_userChangedSecondarySubtitleTrack = false;
    bool m_externalSubsLoaded = false;

    QVariantList m_audioDevices;
    bool m_statsVisible = false;
    QVariantMap m_stats;

    // play() requested before QML's Loader has attached the (lazily
    // created) MpvVideoItem; started from setVideoItem().
    struct DeferredPlay {
        QUrl url;
        qint64 startSeconds = 0;
        QStringList headerFields;
    };
    std::optional<DeferredPlay> m_deferredPlay;

    // What applyPresentationSettings() last pushed into the current video
    // item, per group, so a subtitle slider drag never re-runs
    // apply-profile / hwdec / custom options. Reset when the item changes.
    struct AppliedPresentation {
        QString subtitleKey;
        QString audioKey;
        QString performanceKey;
        QString customOptions;
        QString logLevel;
        bool customApplied = false;
    };
    AppliedPresentation m_applied;

    // Stopped reports handed to the network layer whose replies have not
    // finished yet (see flushPendingStopped()).
    int m_pendingStoppedReports = 0;
};
