#include "player/PlaybackReporter.h"

#include "api/JellyfinClient.h"
#include "core/Formatting.h"
#include "player/MpvWidget.h"

#include <QTimer>

namespace {
// DESIGN.md §7: report progress every 10s, plus immediately on pause/seek.
constexpr int kProgressIntervalMs = 10'000;
} // namespace

PlaybackReporter::PlaybackReporter(JellyfinClient *client, MpvWidget *player, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_player(player)
{
    m_progressTimer = new QTimer(this);
    m_progressTimer->setInterval(kProgressIntervalMs);
    connect(m_progressTimer, &QTimer::timeout, this, &PlaybackReporter::reportProgress);

    connect(m_player, &MpvWidget::positionChanged, this, &PlaybackReporter::onPositionChanged);
    connect(m_player, &MpvWidget::pausedChanged, this, &PlaybackReporter::onPausedChanged);
    connect(m_player, &MpvWidget::seeked, this, &PlaybackReporter::onSeeked);
}

PlaybackReporter::~PlaybackReporter()
{
    stop();
}

void PlaybackReporter::start(const QString &itemId, const QString &playSessionId, const QString &mediaSourceId,
                              qint64 startPositionTicks)
{
    // A new session implicitly ends whatever was playing before, with an
    // accurate Stopped report for it.
    if (m_active)
        stop();

    m_itemId = itemId;
    m_playSessionId = playSessionId;
    m_mediaSourceId = mediaSourceId;
    m_positionTicks = startPositionTicks;
    m_paused = false;
    m_active = true;

    m_client->reportPlaybackStart(m_itemId, m_playSessionId, m_mediaSourceId, m_positionTicks);
    m_progressTimer->start();
}

void PlaybackReporter::stop()
{
    if (!m_active)
        return;

    m_progressTimer->stop();
    m_client->reportPlaybackStopped(m_itemId, m_playSessionId, m_mediaSourceId, m_positionTicks);
    m_active = false;
}

void PlaybackReporter::onPositionChanged(qint64 seconds)
{
    m_positionTicks = Formatting::secondsToTicks(seconds);
}

void PlaybackReporter::onPausedChanged(bool paused)
{
    m_paused = paused;
    if (m_active)
        reportProgress();
}

void PlaybackReporter::onSeeked()
{
    if (m_active)
        reportProgress();
}

void PlaybackReporter::reportProgress()
{
    if (!m_active)
        return;
    m_client->reportPlaybackProgress(m_itemId, m_playSessionId, m_mediaSourceId, m_positionTicks, m_paused);
}
