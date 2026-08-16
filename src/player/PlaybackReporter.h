#pragma once

#include <QObject>
#include <QString>

class JellyfinClient;
class MpvWidget;
class QTimer;

// Owns the /Sessions/Playing* lifecycle for one playback session, driven off
// MpvWidget signals (DESIGN.md §7). Reports:
//   - Sessions/Playing once, when start() is called
//   - Sessions/Playing/Progress every 10s, and immediately on pause/seek
//   - Sessions/Playing/Stopped once, when stop() is called (or a new
//     session starts, or the reporter is destroyed) with the exact last
//     known position -- this is what makes "Continue Watching" correct.
class PlaybackReporter : public QObject
{
    Q_OBJECT
public:
    explicit PlaybackReporter(JellyfinClient *client, MpvWidget *player, QObject *parent = nullptr);
    ~PlaybackReporter() override;

    // startPositionTicks is the resume position playback was started at, so
    // the very first progress report is accurate even before any time-pos
    // update has arrived from mpv.
    void start(const QString &itemId, const QString &playSessionId, const QString &mediaSourceId,
               qint64 startPositionTicks = 0);

    // Sends a final Stopped report (if a session is active) and detaches.
    // Safe to call when nothing is active, and safe to call more than once.
    void stop();

    bool isActive() const { return m_active; }

private slots:
    void onPositionChanged(qint64 seconds);
    void onPausedChanged(bool paused);
    void onSeeked();
    void reportProgress();

private:
    JellyfinClient *m_client;
    MpvWidget *m_player;
    QTimer *m_progressTimer;

    bool m_active = false;
    QString m_itemId;
    QString m_playSessionId;
    QString m_mediaSourceId;
    qint64 m_positionTicks = 0;
    bool m_paused = false;
};
