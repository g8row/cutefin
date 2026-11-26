#include "player/PlaybackReporter.h"

#include "api/JellyfinClient.h"
#include "core/Formatting.h"
#include "player/MpvVideoItem.h"

#include <QTimer>

namespace {

// Report progress every 10s, plus immediately on pause/seek.
constexpr int kProgressIntervalMs = 10'000;

qint64 secondsToTicksPrecise(double seconds)
{
    return static_cast<qint64>(qMax(0.0, seconds) * static_cast<double>(Formatting::kTicksPerSecond));
}

} // namespace

PlaybackReporter::PlaybackReporter(JellyfinClient *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_progressTimer(new QTimer(this))
{
    m_progressTimer->setInterval(kProgressIntervalMs);
    connect(m_progressTimer, &QTimer::timeout, this, &PlaybackReporter::reportProgress);
}

PlaybackReporter::~PlaybackReporter()
{
    stop();
}

void PlaybackReporter::attachPlayer(MpvVideoItem *player)
{
    if (m_player == player)
        return;

    if (m_player)
        disconnect(m_player, nullptr, this, nullptr);

    m_player = player;
    if (!m_player)
        return;

    connect(m_player, &MpvVideoItem::positionChanged, this, &PlaybackReporter::onPositionChanged);
    connect(m_player, &MpvVideoItem::pausedChanged, this, &PlaybackReporter::onPausedChanged);
    connect(m_player, &MpvVideoItem::seeked, this, &PlaybackReporter::onSeeked);
    // The item is owned by QML and may go away before we do.
    connect(m_player, &QObject::destroyed, this, [this]() { m_player = nullptr; });
}

void PlaybackReporter::start(const QString &itemId, const QString &playSessionId, const QString &mediaSourceId,
                             qint64 startPositionTicks)
{
    // A new session implicitly ends the previous one with an accurate
    // Stopped report for it.
    if (m_active)
        stop();

    m_itemId = itemId;
    m_playSessionId = playSessionId;
    m_mediaSourceId = mediaSourceId;
    m_positionTicks = qMax<qint64>(0, startPositionTicks);
    m_paused = false;
    m_active = true;

    if (m_client)
        m_client->reportPlaybackStart(m_itemId, m_playSessionId, m_mediaSourceId, m_positionTicks);
    m_progressTimer->start();
}

bool PlaybackReporter::stop()
{
    if (!m_active)
        return false;

    // Take the freshest position mpv has rather than the last throttled
    // emission, so "Continue Watching" resumes exactly where we left off.
    if (m_player && m_player->isActive())
        m_positionTicks = secondsToTicksPrecise(m_player->position());

    m_progressTimer->stop();
    m_active = false;
    if (!m_client)
        return false;
    m_client->reportPlaybackStopped(m_itemId, m_playSessionId, m_mediaSourceId, m_positionTicks);
    emit stoppedReportSent();
    return true;
}

void PlaybackReporter::onPositionChanged()
{
    // After END_FILE the item resets its position to 0; that must not
    // overwrite the real last position of the session being reported.
    if (!m_active || !m_player || !m_player->isActive())
        return;
    m_positionTicks = secondsToTicksPrecise(m_player->position());
}

void PlaybackReporter::onPausedChanged()
{
    if (!m_player)
        return;
    const bool paused = m_player->isPaused();
    if (paused == m_paused)
        return;
    m_paused = paused;
    if (m_active) {
        onPositionChanged();
        reportProgress();
    }
}

void PlaybackReporter::onSeeked()
{
    if (!m_active)
        return;
    onPositionChanged();
    reportProgress();
}

void PlaybackReporter::reportProgress()
{
    if (!m_active || !m_client)
        return;
    m_client->reportPlaybackProgress(m_itemId, m_playSessionId, m_mediaSourceId, m_positionTicks, m_paused);
}
