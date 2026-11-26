#pragma once

#include <QObject>
#include <QString>

class JellyfinClient;
class MpvVideoItem;
class QTimer;

// Owns the /Sessions/Playing* lifecycle for one playback session, driven off
// MpvVideoItem signals. Reports Playing on start, Progress every 10s + on
// pause/seek, and Stopped on stop/destruction with the exact last position.
class PlaybackReporter : public QObject
{
    Q_OBJECT
public:
    explicit PlaybackReporter(JellyfinClient *client, QObject *parent = nullptr);
    ~PlaybackReporter() override;

    void attachPlayer(MpvVideoItem *player);

    void start(const QString &itemId, const QString &playSessionId, const QString &mediaSourceId,
               qint64 startPositionTicks = 0);
    // Sends Stopped for the active session (no-op if none). Returns true when
    // a Stopped report was actually sent.
    bool stop();

    bool isActive() const { return m_active; }

signals:
    // Emitted right after a POST /Sessions/Playing/Stopped was handed to the
    // network layer, so the owner can wait for it before the app quits.
    void stoppedReportSent();

private slots:
    void onPositionChanged();
    void onPausedChanged();
    void onSeeked();
    void reportProgress();

private:
    JellyfinClient *m_client;
    MpvVideoItem *m_player = nullptr;
    QTimer *m_progressTimer;

    bool m_active = false;
    QString m_itemId;
    QString m_playSessionId;
    QString m_mediaSourceId;
    qint64 m_positionTicks = 0;
    bool m_paused = false;
};
