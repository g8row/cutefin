#include "player/PlayerController.h"

#include "api/JellyfinClient.h"
#include "api/PlaybackNavigation.h"
#include "core/Formatting.h"
#include "core/NetworkQueue.h"
#include "core/Settings.h"
#include "core/TrackSelection.h"
#include "player/PlaybackReporter.h"
#include "player/SleepInhibitor.h"
#include "player/TrickplayCache.h"

#include <QClipboard>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSysInfo>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <utility>

namespace {

Q_LOGGING_CATEGORY(lcPlayer, "cutefin.player")

constexpr int kStatsIntervalMs = 500; // 2 Hz
constexpr double kChapterRestartWindowSeconds = 3.0;
constexpr double kChapterlessJumpSeconds = 60.0;
// Label of the volume-normalization filter we own inside mpv's "af" chain.
const QString kNormFilterLabel = QStringLiteral("@cfnorm");
// RememberedChoice::codec sentinel meaning "the user turned subtitles off
// for this series". A real track never reports this codec.
const QString kSubtitlesOffCodec = QStringLiteral("none");
// Up Next card thumbnail width, routed through image://jellyfin (ImageCache).
constexpr int kNextEpisodeThumbWidth = 400;
// JellyfinClient's error for a PlaybackInfo answer with no usable source.
const QString kNoPlayableSourceError = QStringLiteral("No playable media source");

QString jellyfinImageSource(const QString &itemId, const QString &type, const QString &tag, int maxWidth)
{
    const auto enc = [](const QString &part) { return QString::fromLatin1(QUrl::toPercentEncoding(part)); };
    return QStringLiteral("image://jellyfin/%1/%2/%3/%4").arg(enc(itemId), enc(type), enc(tag)).arg(maxWidth);
}

bool isStoppedReport(const QNetworkReply *reply)
{
    return reply && reply->operation() == QNetworkAccessManager::PostOperation
        && reply->request().url().path().endsWith(QLatin1String("/Sessions/Playing/Stopped"));
}

// Starts/stops `timer` to match `shouldRun`; returns true if it was just started.
bool syncTimer(QTimer *timer, bool shouldRun)
{
    if (shouldRun) {
        if (!timer->isActive()) {
            timer->start();
            return true;
        }
        return false;
    }
    timer->stop();
    return false;
}

TrackSelection::TrackDescriptor toDescriptor(const MpvVideoItem::Track &track)
{
    TrackSelection::TrackDescriptor d;
    d.id = track.id;
    d.language = track.lang;
    d.codec = track.codec;
    d.title = track.title;
    d.forced = track.forced;
    d.hearingImpaired = track.hearingImpaired;
    d.external = track.external;
    return d;
}

QList<TrackSelection::TrackDescriptor> descriptorsOfType(const QList<MpvVideoItem::Track> &tracks, const QString &type)
{
    QList<TrackSelection::TrackDescriptor> out;
    for (const MpvVideoItem::Track &track : tracks) {
        if (track.type == type)
            out.append(toDescriptor(track));
    }
    return out;
}

std::optional<MpvVideoItem::Track> findTrack(const QList<MpvVideoItem::Track> &tracks, const QString &type, int id)
{
    for (const MpvVideoItem::Track &track : tracks) {
        if (track.type == type && track.id == id)
            return track;
    }
    return std::nullopt;
}

QString episodeCode(const MediaItem &item)
{
    if (item.parentIndexNumber >= 0 && item.indexNumber >= 0)
        return QStringLiteral("S%1:E%2").arg(item.parentIndexNumber).arg(item.indexNumber);
    if (item.indexNumber >= 0)
        return QStringLiteral("E%1").arg(item.indexNumber);
    return QString();
}

QString formatBytes(qint64 bytes)
{
    if (bytes <= 0)
        return QStringLiteral("—");
    const double gib = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    if (gib >= 1.0)
        return QStringLiteral("%1 GiB").arg(gib, 0, 'f', 2);
    return QStringLiteral("%1 MiB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
}

QString formatBitsPerSecond(qint64 bps)
{
    if (bps <= 0)
        return QStringLiteral("—");
    return QStringLiteral("%1 Mbps").arg(static_cast<double>(bps) / 1'000'000.0, 0, 'f', 1);
}

QString subtitleFormatForCodec(const QString &codec)
{
    const QString c = codec.toLower();
    if (c == QLatin1String("ass") || c == QLatin1String("ssa"))
        return QStringLiteral("ass");
    if (c == QLatin1String("vtt") || c == QLatin1String("webvtt"))
        return QStringLiteral("vtt");
    return QStringLiteral("srt");
}

// Splits an mpv filter-chain string ("a=x,b=[c,d]") on top-level commas.
QStringList splitFilterChain(const QString &chain)
{
    QStringList parts;
    QString current;
    int depth = 0;
    for (const QChar ch : chain) {
        if (ch == QLatin1Char('['))
            ++depth;
        else if (ch == QLatin1Char(']'))
            depth = qMax(0, depth - 1);
        if (ch == QLatin1Char(',') && depth == 0) {
            if (!current.trimmed().isEmpty())
                parts.append(current.trimmed());
            current.clear();
            continue;
        }
        current.append(ch);
    }
    if (!current.trimmed().isEmpty())
        parts.append(current.trimmed());
    return parts;
}

QString signedMilliseconds(double seconds)
{
    const int ms = qRound(seconds * 1000.0);
    return ms > 0 ? QStringLiteral("+%1 ms").arg(ms) : QStringLiteral("%1 ms").arg(ms);
}

} // namespace

PlayerController::PlayerController(JellyfinClient *client, NetworkQueue *networkQueue, Settings *settings,
                                   QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_settings(settings)
    , m_reporter(new PlaybackReporter(client, this))
    , m_trickplay(new TrickplayCache(client, networkQueue, this))
    , m_sleepInhibitor(new SleepInhibitor(this))
    , m_statsTimer(new QTimer(this))
{
    m_statsTimer->setInterval(kStatsIntervalMs);
    connect(m_statsTimer, &QTimer::timeout, this, &PlayerController::pollStats);

    connect(m_trickplay, &TrickplayCache::thumbnailReady, this, [this](qint64) {
        ++m_trickplayRevision;
        emit trickplayChanged();
    });

    // Track Stopped reports until their replies finish so shutdown can wait
    // for them (flushPendingStopped). Every QNetworkReply of the shared
    // manager ends in finished(), including errors and aborts.
    connect(m_reporter, &PlaybackReporter::stoppedReportSent, this, [this]() { ++m_pendingStoppedReports; });
    if (networkQueue && networkQueue->networkAccessManager()) {
        connect(networkQueue->networkAccessManager(), &QNetworkAccessManager::finished, this,
                &PlayerController::onNetworkReplyFinished);
    }

    if (m_settings) {
        m_maxBitrateMbps = qMax(0, m_settings->maxBitrateMbps());
        m_trickplay->setDualCoreMode(m_settings->dualCoreMode());
    }
}

PlayerController::~PlayerController()
{
    m_statsTimer->stop();
    disconnect(m_reporter, nullptr, this, nullptr);
    m_reporter->stop();
    m_sleepInhibitor->setInhibited(false);
    if (m_videoItem)
        disconnect(m_videoItem, nullptr, this, nullptr);
}

// ---------------------------------------------------------------------------
// Video item wiring
// ---------------------------------------------------------------------------

void PlayerController::setVideoItem(MpvVideoItem *item)
{
    if (m_videoItem == item)
        return;

    if (m_videoItem)
        disconnect(m_videoItem, nullptr, this, nullptr);

    m_videoItem = item;
    m_reporter->attachPlayer(item);
    // A fresh item carries none of our presentation settings yet.
    m_applied = AppliedPresentation();

    if (item) {
        connect(item, &MpvVideoItem::positionChanged, this, &PlayerController::onPositionUpdated);
        connect(item, &MpvVideoItem::durationChanged, this, &PlayerController::onPositionUpdated);
        connect(item, &MpvVideoItem::pausedChanged, this, &PlayerController::onPausedUpdated);
        connect(item, &MpvVideoItem::endOfFile, this, &PlayerController::onEndOfFile);
        connect(item, &MpvVideoItem::tracksChanged, this, &PlayerController::autoSelectTracks);
        connect(item, &MpvVideoItem::fileLoaded, this, [this]() {
            if (m_loading) {
                m_loading = false;
                emit loadingChanged();
            }
            onPausedUpdated();
            if (!m_externalSubsLoaded && !m_currentItem.mediaSources.isEmpty())
                loadExternalSubtitles(m_currentItem);
            autoSelectTracks();
        });
        connect(item, &MpvVideoItem::mpvFatalError, this, [this](const QString &message) {
            qCWarning(lcPlayer, "%ls", qUtf16Printable(message));
            if (m_loading) {
                m_loading = false;
                emit loadingChanged();
            }
            emit toast(message, QStringLiteral("error"));
        });
        connect(item, &MpvVideoItem::volumeChanged, this, [this]() {
            if (m_videoItem && m_settings)
                m_settings->setPlayerVolume(m_videoItem->volume());
        });
        connect(item, &QObject::destroyed, this, [this]() {
            // Unloaded by QML (lazy Loader): the next item starts clean.
            m_applied = AppliedPresentation();
            emit videoItemChanged();
        });

        if (m_settings)
            item->setVolume(m_settings->playerVolume());
        applyPresentationSettings();
        refreshAudioDevices();

        if (m_deferredPlay) {
            const DeferredPlay deferred = *m_deferredPlay;
            m_deferredPlay.reset();
            item->play(deferred.url, deferred.startSeconds, deferred.headerFields);
        }
    }

    emit videoItemChanged();
}

bool PlayerController::trickplayEnabled() const
{
    return m_trickplay->isEnabled();
}

// ---------------------------------------------------------------------------
// Quality / presentation getters & setters
// ---------------------------------------------------------------------------

void PlayerController::setMaxBitrateMbps(int mbps)
{
    mbps = qMax(0, mbps);
    if (mbps == m_maxBitrateMbps)
        return;
    m_maxBitrateMbps = mbps;
    emit playbackMetaChanged();

    // Re-resolve the stream from where the user is right now.
    if (!m_open || m_currentItem.id.isEmpty())
        return;

    qint64 resumeSeconds = 0;
    if (m_videoItem && m_videoItem->isActive())
        resumeSeconds = static_cast<qint64>(m_videoItem->position());

    // Track ids are not stable across a re-resolved stream (a transcode
    // usually carries a single audio track); let remembered/preferred
    // selection run again on the new file.
    m_userChangedAudioTrack = false;
    m_userChangedSubtitleTrack = false;
    m_userChangedSecondarySubtitleTrack = false;

    if (!m_loading) {
        m_loading = true;
        emit loadingChanged();
    }
    emit toast(mbps > 0 ? tr("Switching to %1 Mbps…").arg(mbps) : tr("Switching to Auto quality…"),
               QStringLiteral("info"));
    startPlaybackWithSettings(m_currentItem, resumeSeconds, false);
}

double PlayerController::subtitleDelay() const
{
    return m_settings ? m_settings->subtitleDelaySeconds() : 0.0;
}

double PlayerController::audioDelay() const
{
    return m_settings ? m_settings->audioDelaySeconds() : 0.0;
}

void PlayerController::setStatsVisible(bool visible)
{
    if (visible == m_statsVisible)
        return;
    m_statsVisible = visible;
    emit statsVisibleChanged();
    if (syncTimer(m_statsTimer, m_statsVisible && m_open))
        pollStats();
}

// ---------------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------------

void PlayerController::startItemPlayback(const MediaItem &item, bool resume)
{
    if (item.id.isEmpty() || !m_client)
        return;

    // End whatever was playing (accurate Stopped report first).
    m_reporter->stop();
    if (m_videoItem)
        m_videoItem->stop();
    m_trickplay->clear();
    m_deferredPlay.reset();
    resetItemState();

    const bool wasOpen = m_open;

    m_currentItem = item;
    // Repopulated by fetchItemDetails(); its presence is what tells
    // fileLoaded that external subtitles can be added.
    m_currentItem.mediaSources.clear();
    m_itemId = item.id;
    m_currentSeriesId = item.type == QLatin1String("Episode") ? item.seriesId : QString();

    m_title = item.name;
    if (item.type == QLatin1String("Episode")) {
        const QString code = episodeCode(item);
        if (!item.seriesName.isEmpty() && !code.isEmpty())
            m_subtitle = QStringLiteral("%1 · %2").arg(item.seriesName, code);
        else
            m_subtitle = item.seriesName.isEmpty() ? code : item.seriesName;
    } else if (item.productionYear > 0) {
        m_subtitle = QString::number(item.productionYear);
    }
    emit itemChanged();

    if (!wasOpen && m_settings) {
        m_maxBitrateMbps = qMax(0, m_settings->maxBitrateMbps());
        emit playbackMetaChanged();
    }

    if (!m_open) {
        m_open = true;
        emit openChanged();
    }
    if (!m_loading) {
        m_loading = true;
        emit loadingChanged();
    }
    if (syncTimer(m_statsTimer, m_statsVisible && m_open))
        pollStats();

    applyPresentationSettings();

    qint64 startSeconds = 0;
    if (resume && !item.userData.played)
        startSeconds = Formatting::ticksToSeconds(item.userData.playbackPositionTicks);
    startPlaybackWithSettings(item, startSeconds, false);

    // Chapters/trickplay/streams are a heavier request kept off the path to
    // first frame.
    const QString id = item.id;
    m_client->fetchItemDetails(id, this, [this, id](bool ok, const MediaItem &full, const QString &error) {
        if (id != m_itemId)
            return;
        if (!ok) {
            qCWarning(lcPlayer, "item details failed: %ls", qUtf16Printable(error));
            return;
        }
        applyItemDetails(full);
    });

    fetchSegmentsAndNextEpisode(item);
}

void PlayerController::playDirectUrl(const QUrl &url, qint64 startSeconds, const QString &title)
{
    if (!url.isValid())
        return;

    m_reporter->stop();
    if (m_videoItem)
        m_videoItem->stop();
    m_trickplay->clear();
    m_deferredPlay.reset();
    resetItemState();

    m_title = title.isEmpty() ? url.fileName() : title;
    emit itemChanged();

    if (!m_open) {
        m_open = true;
        emit openChanged();
    }
    if (!m_loading) {
        m_loading = true;
        emit loadingChanged();
    }
    if (syncTimer(m_statsTimer, m_statsVisible && m_open))
        pollStats();

    applyPresentationSettings();
    playOrDefer(url, startSeconds);
}

void PlayerController::playOrDefer(const QUrl &url, qint64 startSeconds, const QStringList &headerFields)
{
    if (m_videoItem) {
        m_deferredPlay.reset();
        m_videoItem->play(url, startSeconds, headerFields);
    } else {
        // The player page's Loader creates the MpvVideoItem only once the
        // player opens; setVideoItem() starts this as soon as it attaches.
        m_deferredPlay = DeferredPlay{url, startSeconds, headerFields};
    }
}

void PlayerController::stopAndClose()
{
    m_reporter->stop();
    if (m_videoItem)
        m_videoItem->stop();
    m_trickplay->clear();
    m_sleepInhibitor->setInhibited(false);
    m_deferredPlay.reset();
    m_statsTimer->stop();

    resetItemState();

    if (m_loading) {
        m_loading = false;
        emit loadingChanged();
    }

    const bool wasOpen = m_open;
    m_open = false;
    if (wasOpen)
        emit openChanged();
    emit closed();
}

bool PlayerController::hasActivePlaybackReport() const
{
    return m_reporter->isActive();
}

bool PlayerController::flushPendingStopped(int timeoutMs)
{
    // Ends the session (sending Stopped) if one is still running, e.g. the
    // window was closed straight from the player.
    if (m_open || m_reporter->isActive())
        stopAndClose();

    if (m_pendingStoppedReports <= 0)
        return true;

    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(this, &PlayerController::pendingStoppedReportsChanged, &loop, [this, &loop]() {
        if (m_pendingStoppedReports <= 0)
            loop.quit();
    });
    QElapsedTimer elapsed;
    elapsed.start();
    deadline.start(qMax(0, timeoutMs));
    // Network replies only; no user input may sneak in during shutdown.
    loop.exec(QEventLoop::ExcludeUserInputEvents);

    const bool flushed = m_pendingStoppedReports <= 0;
    if (flushed)
        qCDebug(lcPlayer, "Stopped report flushed in %lld ms", static_cast<long long>(elapsed.elapsed()));
    else
        qCWarning(lcPlayer, "Stopped report still pending after %d ms; quitting anyway", timeoutMs);
    return flushed;
}

void PlayerController::onNetworkReplyFinished(QNetworkReply *reply)
{
    if (m_pendingStoppedReports <= 0 || !isStoppedReport(reply))
        return;
    --m_pendingStoppedReports;
    emit pendingStoppedReportsChanged();
}

// ---------------------------------------------------------------------------
// Controls
// ---------------------------------------------------------------------------

void PlayerController::selectAudioTrack(int trackId)
{
    if (!m_videoItem)
        return;
    m_userChangedAudioTrack = true;
    m_videoItem->setAudioTrack(trackId);

    if (!m_currentSeriesId.isEmpty() && m_settings) {
        if (const auto track = findTrack(m_videoItem->tracks(), QStringLiteral("audio"), trackId))
            m_settings->setRememberedAudioChoice(m_currentSeriesId, TrackSelection::choiceForTrack(toDescriptor(*track)));
    }
    updateAudioPassthroughForSelectedTrack();
}

void PlayerController::selectSubtitleTrack(int trackId)
{
    if (!m_videoItem)
        return;
    m_userChangedSubtitleTrack = true;
    m_videoItem->setSubtitleTrack(trackId);

    if (!m_currentSeriesId.isEmpty() && m_settings) {
        if (trackId < 0) {
            TrackSelection::RememberedChoice off;
            off.codec = kSubtitlesOffCodec;
            off.hasChoice = true;
            m_settings->setRememberedSubtitleChoice(m_currentSeriesId, off);
        } else if (const auto track = findTrack(m_videoItem->tracks(), QStringLiteral("sub"), trackId)) {
            m_settings->setRememberedSubtitleChoice(m_currentSeriesId,
                                                    TrackSelection::choiceForTrack(toDescriptor(*track)));
        }
    }
}

void PlayerController::selectSecondarySubtitleTrack(int trackId)
{
    if (!m_videoItem)
        return;
    m_userChangedSecondarySubtitleTrack = true;
    m_videoItem->setSecondarySubtitleTrack(trackId);

    if (!m_currentSeriesId.isEmpty() && m_settings) {
        if (trackId < 0) {
            TrackSelection::RememberedChoice off;
            off.codec = kSubtitlesOffCodec;
            off.hasChoice = true;
            m_settings->setRememberedSecondarySubtitleChoice(m_currentSeriesId, off);
        } else if (const auto track = findTrack(m_videoItem->tracks(), QStringLiteral("sub"), trackId)) {
            m_settings->setRememberedSecondarySubtitleChoice(m_currentSeriesId,
                                                             TrackSelection::choiceForTrack(toDescriptor(*track)));
        }
    }
}

void PlayerController::jumpChapter(int direction)
{
    if (!m_videoItem || direction == 0)
        return;

    if (m_chapters.isEmpty()) {
        m_videoItem->seekRelative(direction > 0 ? kChapterlessJumpSeconds : -kChapterlessJumpSeconds);
        return;
    }

    const double position = m_videoItem->position();
    if (direction < 0) {
        // "Previous" within the first few seconds of a chapter goes to the
        // one before it; otherwise it restarts the current chapter.
        for (int i = m_chapters.size() - 1; i >= 0; --i) {
            const double start = m_chapters.at(i).toMap().value(QStringLiteral("startSeconds")).toDouble();
            if (start < position - kChapterRestartWindowSeconds) {
                m_videoItem->seek(start);
                return;
            }
        }
        m_videoItem->seek(0.0);
        return;
    }

    for (const QVariant &entry : std::as_const(m_chapters)) {
        const double start = entry.toMap().value(QStringLiteral("startSeconds")).toDouble();
        if (start > position + 0.5) {
            m_videoItem->seek(start);
            return;
        }
    }
}

void PlayerController::seekToChapter(int index)
{
    if (!m_videoItem || index < 0 || index >= m_chapters.size())
        return;
    m_videoItem->seek(m_chapters.at(index).toMap().value(QStringLiteral("startSeconds")).toDouble());
}

void PlayerController::requestTrickplay(qint64 positionMs)
{
    // A hit needs nothing further (the image provider reads the cache
    // directly); a miss starts the fetch and bumps trickplayRevision later.
    m_trickplay->thumbnailAt(positionMs);
}

void PlayerController::triggerSkip()
{
    if (!m_videoItem || !m_skipActive || m_skipTargetSeconds < 0)
        return;
    m_videoItem->seek(static_cast<double>(m_skipTargetSeconds));
    m_skipActive = false;
    m_skipLabel.clear();
    m_skipTargetSeconds = -1;
    emit skipChanged();
}

void PlayerController::playNextEpisode()
{
    if (m_nextEpisode.id.isEmpty())
        return;
    const MediaItem next = m_nextEpisode;
    const bool resume = !next.userData.played && next.userData.playbackPositionTicks > 0;
    startItemPlayback(next, resume);
}

void PlayerController::cancelNextEpisode()
{
    m_nextEpisodeCancelled = true;
    if (m_nextEpisodeVisible) {
        m_nextEpisodeVisible = false;
        m_nextEpisodeCountdown = 0;
        emit nextEpisodeChanged();
    }

    // Cancelled while sitting on the final frame: nothing left to watch.
    if (m_videoItem && m_videoItem->duration() > 0.0
        && m_videoItem->position() >= m_videoItem->duration() - 0.5) {
        stopAndClose();
    }
}

void PlayerController::adjustSubtitleDelay(double deltaSeconds)
{
    if (!m_settings)
        return;
    double value = qRound((m_settings->subtitleDelaySeconds() + deltaSeconds) * 1000.0) / 1000.0;
    if (qAbs(value) < 0.0005)
        value = 0.0;
    m_settings->setSubtitleDelaySeconds(value);
    if (m_videoItem)
        m_videoItem->setProperty("sub-delay", QString::number(value, 'f', 3));
    emit presentationChanged();
    emit toast(tr("Subtitle delay: %1").arg(signedMilliseconds(value)), QStringLiteral("info"));
}

void PlayerController::adjustAudioDelay(double deltaSeconds)
{
    if (!m_settings)
        return;
    double value = qRound((m_settings->audioDelaySeconds() + deltaSeconds) * 1000.0) / 1000.0;
    if (qAbs(value) < 0.0005)
        value = 0.0;
    m_settings->setAudioDelaySeconds(value);
    if (m_videoItem)
        m_videoItem->setProperty("audio-delay", QString::number(value, 'f', 3));
    emit presentationChanged();
    emit toast(tr("Audio delay: %1").arg(signedMilliseconds(value)), QStringLiteral("info"));
}

void PlayerController::refreshAudioDevices()
{
    // With a lazily created video item there is often no mpv core to ask;
    // keep the last known list (Settings shows it) instead of blanking it.
    if (!m_videoItem)
        return;
    const QVariantList devices = m_videoItem->audioDevicesVariant();
    if (devices.isEmpty() && !m_audioDevices.isEmpty())
        return;
    if (devices == m_audioDevices)
        return;
    m_audioDevices = devices;
    emit audioDevicesChanged();
}

void PlayerController::applyPresentationSettings()
{
    if (!m_videoItem || !m_settings)
        return;

    // Called on every settings change (including each step of a slider
    // drag). Each group is pushed into mpv only when its own inputs changed,
    // so dragging a subtitle slider touches sub-* and nothing else.
    applySubtitleSettings();
    applyAudioSettings();
    applyEngineSettings();

    emit presentationChanged();
}

void PlayerController::applySubtitleSettings()
{
    MpvVideoItem *v = m_videoItem;
    Settings *s = m_settings;

    const QString family = s->subtitleFontFamily().trimmed();
    const QString outlineSize = QString::number(s->subtitleOutlineSize(), 'f', 2);
    const QStringList key = {
        QString::number(s->subtitleFontSize()),
        family,
        s->subtitleColor(),
        s->subtitleOutlineColor(),
        outlineSize,
        QString::number(s->subtitleShadowOffset(), 'f', 2),
        s->subtitleBackgroundColor(),
        s->subtitleBackgroundBoxEnabled() ? QStringLiteral("box") : QStringLiteral("outline"),
        QString::number(qBound(0, s->subtitlePositionPercent(), 150)),
        QString::number(s->subtitleDelaySeconds(), 'f', 3),
        s->subtitleAssOverride() ? QStringLiteral("force") : QStringLiteral("scale"),
    };
    const QString joined = key.join(QChar(0x1f));
    if (joined == m_applied.subtitleKey)
        return;
    m_applied.subtitleKey = joined;

    v->setProperty("sub-font-size", key.at(0));
    v->setProperty("sub-font", family.isEmpty() ? QStringLiteral("sans-serif") : family);
    v->setProperty("sub-color", s->subtitleColor());
    // mpv 0.38 renamed sub-border-* to sub-outline-*; try both.
    if (!v->setPropertyChecked("sub-outline-color", s->subtitleOutlineColor()))
        v->setPropertyChecked("sub-border-color", s->subtitleOutlineColor());
    if (!v->setPropertyChecked("sub-outline-size", outlineSize))
        v->setPropertyChecked("sub-border-size", outlineSize);
    v->setProperty("sub-shadow-offset", key.at(5));
    v->setProperty("sub-back-color", s->subtitleBackgroundColor());
    if (s->subtitleBackgroundBoxEnabled()) {
        if (!v->setPropertyChecked("sub-border-style", QStringLiteral("background-box")))
            v->setPropertyChecked("sub-border-style", QStringLiteral("opaque-box"));
    } else {
        v->setPropertyChecked("sub-border-style", QStringLiteral("outline-and-shadow"));
    }
    v->setProperty("sub-pos", key.at(8));
    v->setProperty("sub-delay", key.at(9));
    v->setProperty("sub-ass-override", key.at(10));
}

void PlayerController::applyAudioSettings()
{
    MpvVideoItem *v = m_videoItem;
    Settings *s = m_settings;

    const QString device = s->audioDevice().trimmed();
    const QString channels = s->audioChannelLayout().trimmed();
    const QStringList key = {
        QString::number(s->audioDelaySeconds(), 'f', 3),
        device,
        s->audioPassthroughEnabled() ? QStringLiteral("spdif") : QStringLiteral("pcm"),
        channels,
        s->audioNormalizationEnabled() ? QStringLiteral("norm") : QStringLiteral("raw"),
    };
    const QString joined = key.join(QChar(0x1f));
    if (joined == m_applied.audioKey)
        return;
    m_applied.audioKey = joined;

    v->setProperty("audio-delay", key.at(0));
    if (!v->setPropertyChecked("audio-device", device.isEmpty() ? QStringLiteral("auto") : device))
        v->setPropertyChecked("audio-device", QStringLiteral("auto"));
    v->setProperty("audio-spdif", TrackSelection::spdifOptionValue(s->audioPassthroughEnabled()));
    v->setProperty("audio-channels", channels.isEmpty() ? QStringLiteral("auto") : channels);

    // Volume normalization: maintain only our own labelled entry in "af"
    // so filters added through custom options survive.
    QStringList filters = splitFilterChain(v->stringProperty("af"));
    for (int i = filters.size() - 1; i >= 0; --i) {
        if (filters.at(i).startsWith(kNormFilterLabel + QLatin1Char(':')))
            filters.removeAt(i);
    }
    if (s->audioNormalizationEnabled())
        filters.append(kNormFilterLabel + QStringLiteral(":lavfi=[dynaudnorm=f=250:g=31]"));
    if (!v->setPropertyChecked("af", filters.join(QLatin1Char(','))))
        qCWarning(lcPlayer, "could not apply audio filter chain");
}

void PlayerController::applyEngineSettings()
{
    MpvVideoItem *v = m_videoItem;
    Settings *s = m_settings;

    const QString logLevel = s->mpvLogLevel().trimmed();
    if (logLevel != m_applied.logLevel) {
        m_applied.logLevel = logLevel;
        v->setLogLevel(logLevel);
    }

    // --- Video / performance: hwdec, demuxer budget, apply-profile. Only
    // when one of those inputs changed -- re-running it resets hwdec and the
    // scaler state mid-playback.
    const bool dualCore = s->dualCoreMode();
    const QString performanceKey = QStringList{dualCore ? QStringLiteral("dual") : QStringLiteral("multi"),
                                               s->hwdecMode().trimmed(), s->videoProfile().trimmed().toLower()}
                                       .join(QChar(0x1f));
    const bool performanceChanged = performanceKey != m_applied.performanceKey;
    if (performanceChanged) {
        m_applied.performanceKey = performanceKey;
        v->applyPerformanceProfile(dualCore, s->hwdecMode(), s->videoProfile());
        m_trickplay->setDualCoreMode(dualCore);
    }

    // --- User's raw mpv options last, so they win -- re-applied after a
    // profile change too, since apply-profile may have overridden them.
    const QString custom = s->customMpvOptions();
    if (!m_applied.customApplied || custom != m_applied.customOptions || performanceChanged) {
        m_applied.customApplied = true;
        m_applied.customOptions = custom;
        const QStringList rejected = applyCustomMpvOptions(custom);
        if (!rejected.isEmpty())
            qCWarning(lcPlayer, "rejected custom mpv options: %ls",
                      qUtf16Printable(rejected.join(QStringLiteral(", "))));
    }
}

QStringList PlayerController::applyCustomMpvOptions(const QString &rawText)
{
    if (!m_videoItem)
        return {};
    const QString normalized = QString(rawText).replace(QLatin1String("\r\n"), QLatin1String("\n"));
    return m_videoItem->applyOptionLines(normalized.split(QLatin1Char('\n'), Qt::SkipEmptyParts));
}

void PlayerController::copyDiagnosticsToClipboard()
{
    QStringList lines;
    lines << QStringLiteral("cutefin playback diagnostics")
          << QStringLiteral("Captured: %1").arg(QDateTime::currentDateTime().toString(Qt::ISODate));
    lines << QString();
    lines << QStringLiteral("Title: %1").arg(m_title);
    if (!m_subtitle.isEmpty())
        lines << QStringLiteral("Subtitle: %1").arg(m_subtitle);
    lines << QStringLiteral("Item ID: %1").arg(m_itemId);
    lines << QStringLiteral("Play session: %1").arg(m_playSessionId);
    lines << QStringLiteral("Media source: %1").arg(m_mediaSourceId);
    lines << QStringLiteral("Play method: %1").arg(m_transcoding ? QStringLiteral("Transcode") : QStringLiteral("Direct"));
    if (!m_transcodeReasons.isEmpty())
        lines << QStringLiteral("Transcode reasons: %1").arg(m_transcodeReasons.join(QStringLiteral(", ")));
    lines << QStringLiteral("Container: %1").arg(m_container);
    lines << QStringLiteral("File size: %1").arg(formatBytes(m_fileSize));
    lines << QStringLiteral("Source bitrate: %1").arg(formatBitsPerSecond(m_totalBitrate));
    lines << QStringLiteral("Max bitrate: %1")
                 .arg(m_maxBitrateMbps > 0 ? QStringLiteral("%1 Mbps").arg(m_maxBitrateMbps) : QStringLiteral("Auto"));
    if (m_client)
        lines << QStringLiteral("Server host: %1").arg(QUrl(m_client->serverUrl()).host());
    lines << QStringLiteral("OS: %1 (%2)").arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture());
    lines << QStringLiteral("CPU threads: %1").arg(QThread::idealThreadCount());
    if (m_settings) {
        lines << QStringLiteral("Dual-core mode: %1")
                     .arg(m_settings->dualCoreMode() ? QStringLiteral("on") : QStringLiteral("off"));
        lines << QStringLiteral("hwdec setting: %1 / profile: %2")
                     .arg(m_settings->hwdecMode(), m_settings->videoProfile());
    }

    QStringList logs;
    if (m_videoItem) {
        lines << QStringLiteral("mpv: %1").arg(m_videoItem->stringProperty("mpv-version"));
        lines << QString();

        const QVariantMap stats = m_videoItem->collectStatsSnapshot();
        static const char *const kKeys[] = {
            "codec",       "hwdec",          "resolution",   "displayRes",      "pixelFormat",
            "containerFps", "estimatedFps",  "videoBitrate", "colorSpace",      "hdrPeak",
            "audioCodec",  "audioChannels",  "audioSampleRate", "audioBitrate", "audioDevice",
            "droppedFrames", "decoderDroppedFrames", "avSync", "cacheDuration", "cacheSpeed",
        };
        for (const char *key : kKeys) {
            const QString name = QString::fromLatin1(key);
            lines << QStringLiteral("%1: %2").arg(name, stats.value(name).toString());
        }
        logs = m_videoItem->recentLogs();
    }

    if (!logs.isEmpty()) {
        lines << QString() << QStringLiteral("--- Recent mpv log ---");
        lines << logs;
    }

    if (QClipboard *clipboard = QGuiApplication::clipboard()) {
        // Logs are redacted as they arrive; this is belt and braces for
        // anything else that might quote a stream URL.
        clipboard->setText(MpvVideoItem::redactSecrets(lines.join(QLatin1Char('\n'))));
        emit toast(tr("Diagnostics copied to clipboard"), QStringLiteral("success"));
    }
}

void PlayerController::takeScreenshot()
{
    if (!m_videoItem)
        return;
    const QString path = m_videoItem->takeScreenshot();
    if (path.isEmpty())
        emit toast(tr("Could not save screenshot"), QStringLiteral("error"));
    else
        emit toast(tr("Screenshot saved to %1").arg(path), QStringLiteral("success"));
}

// ---------------------------------------------------------------------------
// Playback resolution & item details
// ---------------------------------------------------------------------------

void PlayerController::startPlaybackWithSettings(const MediaItem &item, qint64 startSeconds, bool forceTranscode)
{
    if (!m_client)
        return;

    const qint64 maxBitrateBps = static_cast<qint64>(m_maxBitrateMbps) * 1'000'000LL;
    const QString id = item.id;

    m_client->getPlaybackInfo(
        id, maxBitrateBps, forceTranscode, this,
        [this, item, id, startSeconds, forceTranscode](const JellyfinClient::PlaybackResolution &resolution,
                                                       const QString &error) {
            if (id != m_itemId || !m_open)
                return; // superseded by another item or closed meanwhile

            if (!resolution.ok) {
                // Retry as a forced transcode only when the server actually
                // answered and offered nothing direct-playable. A network
                // failure or 401 would fail the same way again (and a 401
                // must surface so the session can be re-authenticated).
                const bool serverAnswered =
                    !resolution.playSessionId.isEmpty() || error == kNoPlayableSourceError;
                if (!forceTranscode && serverAnswered) {
                    startPlaybackWithSettings(item, startSeconds, true);
                    return;
                }
                if (m_loading) {
                    m_loading = false;
                    emit loadingChanged();
                }
                const bool stillPlaying = m_videoItem && m_videoItem->isActive();
                emit toast(tr("Cannot play %1: %2").arg(item.name, error), QStringLiteral("error"));
                if (!stillPlaying)
                    stopAndClose();
                return;
            }

            m_playSessionId = resolution.playSessionId;
            m_mediaSourceId = resolution.mediaSourceId;
            m_container = resolution.container;
            m_fileSize = resolution.size;
            m_totalBitrate = resolution.bitRate;
            m_transcoding = resolution.isTranscoding;
            m_transcodeReasons = resolution.transcodeReasons;
            emit playbackMetaChanged();

            // A (re)loaded file starts with no external subtitles attached.
            m_externalSubsLoaded = false;

            // start() closes out a previous session (quality switch) with an
            // accurate Stopped report before announcing the new one.
            m_reporter->start(id, resolution.playSessionId, resolution.mediaSourceId,
                              Formatting::secondsToTicks(startSeconds));

            playOrDefer(resolution.url, startSeconds);
        });
}

void PlayerController::applyItemDetails(const MediaItem &full)
{
    // Details are a superset of the list item that started playback.
    m_currentItem = full;
    if (full.type == QLatin1String("Episode") && !full.seriesId.isEmpty())
        m_currentSeriesId = full.seriesId;

    // --- Chapters
    m_chapters.clear();
    for (int i = 0; i < full.chapters.size(); ++i) {
        const Chapter &chapter = full.chapters.at(i);
        const double startSeconds =
            static_cast<double>(chapter.startTicks) / static_cast<double>(Formatting::kTicksPerSecond);
        QVariantMap map;
        map.insert(QStringLiteral("index"), i);
        map.insert(QStringLiteral("name"), chapter.name.isEmpty() ? tr("Chapter %1").arg(i + 1) : chapter.name);
        map.insert(QStringLiteral("startSeconds"), startSeconds);
        map.insert(QStringLiteral("startMs"), static_cast<qint64>(startSeconds * 1000.0));
        map.insert(QStringLiteral("imageTag"), chapter.imageTag);
        m_chapters.append(map);
    }
    emit chaptersChanged();

    // --- Trickplay: prefer the source being played; fall back to the only
    // source the item has, since /PlaybackInfo can hand back an id that is
    // not a key here for remuxed sources. Widest resolution wins: the hover
    // preview scales down far better than up.
    QHash<int, TrickplayInfo> byWidth = full.trickplay.value(m_mediaSourceId);
    if (byWidth.isEmpty() && full.trickplay.size() == 1)
        byWidth = full.trickplay.constBegin().value();
    TrickplayInfo chosen;
    int chosenWidth = 0;
    for (auto it = byWidth.constBegin(); it != byWidth.constEnd(); ++it) {
        if (it.key() > chosenWidth && it.value().isValid()) {
            chosenWidth = it.key();
            chosen = it.value();
        }
    }
    m_trickplay->setSource(full.id, chosen);
    ++m_trickplayRevision;
    emit trickplayChanged();

    // --- External subtitles, if the file is already loaded.
    if (m_videoItem && m_videoItem->isActive() && !m_loading && !m_externalSubsLoaded)
        loadExternalSubtitles(m_currentItem);
}

void PlayerController::loadExternalSubtitles(const MediaItem &item)
{
    if (!m_videoItem || !m_client || m_externalSubsLoaded || item.mediaSources.isEmpty())
        return;

    const MediaSource *source = nullptr;
    for (const MediaSource &candidate : item.mediaSources) {
        if (candidate.id == m_mediaSourceId) {
            source = &candidate;
            break;
        }
    }
    if (!source)
        source = &item.mediaSources.first();

    m_externalSubsLoaded = true;

    // mpv only sees tracks inside the container; Jellyfin's external
    // subtitles live beside the file and must be sub-add'ed explicitly.
    for (const MediaStream &stream : source->streams) {
        if (stream.type != QLatin1String("Subtitle") || !stream.isExternal)
            continue;
        const QUrl url =
            m_client->subtitleStreamUrl(item.id, source->id, stream.index, subtitleFormatForCodec(stream.codec));
        const QString title = stream.displayTitle.isEmpty() ? stream.language : stream.displayTitle;
        m_videoItem->addSubtitleUrl(url, title, stream.language);
    }
}

void PlayerController::autoSelectTracks()
{
    if (!m_videoItem || !m_videoItem->isActive() || !m_settings)
        return;

    const QList<MpvVideoItem::Track> tracks = m_videoItem->tracks();
    const QList<TrackSelection::TrackDescriptor> audio = descriptorsOfType(tracks, QStringLiteral("audio"));
    const QList<TrackSelection::TrackDescriptor> subs = descriptorsOfType(tracks, QStringLiteral("sub"));
    const bool isSeries = !m_currentSeriesId.isEmpty();

    if (!m_userChangedAudioTrack && !audio.isEmpty()) {
        const TrackSelection::RememberedChoice remembered =
            isSeries ? m_settings->rememberedAudioChoice(m_currentSeriesId) : TrackSelection::RememberedChoice();
        const int pick = TrackSelection::pickTrack(audio, remembered, m_settings->preferredAudioLanguage());
        if (pick >= 0 && pick != m_videoItem->selectedAudioId()) {
            m_videoItem->setAudioTrack(pick);
            updateAudioPassthroughForSelectedTrack();
        }
    }

    if (!m_userChangedSubtitleTrack) {
        const TrackSelection::RememberedChoice remembered =
            isSeries ? m_settings->rememberedSubtitleChoice(m_currentSeriesId) : TrackSelection::RememberedChoice();
        if (remembered.hasChoice && remembered.codec == kSubtitlesOffCodec) {
            if (m_videoItem->selectedSubtitleId() >= 0)
                m_videoItem->setSubtitleTrack(-1);
        } else if (!subs.isEmpty()) {
            const int pick = TrackSelection::pickTrack(subs, remembered, m_settings->preferredSubtitleLanguage());
            if (pick >= 0 && pick != m_videoItem->selectedSubtitleId()
                && pick != m_videoItem->selectedSecondarySubtitleId())
                m_videoItem->setSubtitleTrack(pick);
        }
    }

    // Secondary subtitles only ever come from an explicit remembered choice:
    // there is no "preferred secondary language" setting.
    if (!m_userChangedSecondarySubtitleTrack && isSeries && !subs.isEmpty()) {
        const TrackSelection::RememberedChoice remembered =
            m_settings->rememberedSecondarySubtitleChoice(m_currentSeriesId);
        if (remembered.hasChoice && remembered.codec != kSubtitlesOffCodec) {
            const int pick = TrackSelection::pickTrack(subs, remembered, QString());
            if (pick >= 0 && pick != m_videoItem->selectedSecondarySubtitleId()
                && pick != m_videoItem->selectedSubtitleId())
                m_videoItem->setSecondarySubtitleTrack(pick);
        }
    }
}

void PlayerController::updateAudioPassthroughForSelectedTrack()
{
    if (!m_videoItem || !m_settings)
        return;

    const bool enabled = m_settings->audioPassthroughEnabled();
    m_videoItem->setProperty("audio-spdif", TrackSelection::spdifOptionValue(enabled));
    if (!enabled)
        return;

    // audio-spdif only takes effect for an already-matching codec; say so
    // instead of silently claiming passthrough.
    const auto track = findTrack(m_videoItem->tracks(), QStringLiteral("audio"), m_videoItem->selectedAudioId());
    if (track && !track->codec.isEmpty() && !TrackSelection::canPassthrough(track->codec)) {
        emit toast(tr("Passthrough isn't available for %1 audio; decoding to PCM").arg(track->codec.toUpper()),
                   QStringLiteral("info"));
    }
}

// ---------------------------------------------------------------------------
// Skip intro / next episode
// ---------------------------------------------------------------------------

void PlayerController::fetchSegmentsAndNextEpisode(const MediaItem &item)
{
    if (!m_client)
        return;

    const QString id = item.id;
    m_client->fetchMediaSegments(id, this, [this, id](bool ok, const QList<MediaSegment> &segments, const QString &) {
        if (id != m_itemId)
            return;
        // An empty list is the normal "no intro-detection plugin" answer.
        m_segments = ok ? segments : QList<MediaSegment>();
        onPositionUpdated();
    });

    if (item.type == QLatin1String("Episode") && !item.seriesId.isEmpty())
        resolveNextEpisode(item);
}

void PlayerController::resolveNextEpisode(const MediaItem &item)
{
    if (!m_client || item.seasonId.isEmpty())
        return;

    const QString id = item.id;
    const QString seriesId = item.seriesId;
    const QString seasonId = item.seasonId;

    const auto assignNext = [this, id](const MediaItem &next) {
        if (id != m_itemId || next.id.isEmpty())
            return;
        m_nextEpisode = next;
        m_nextEpisodeTitle = next.name;
        m_nextEpisodeSubtitle = episodeCode(next);
        // Through the image provider (ImageCache: disk cache, dedup, no
        // api_key in a URL QML might log) rather than a raw server URL.
        const QString primaryTag = next.imageTags.value(QStringLiteral("Primary"));
        const QString thumbTag = next.imageTags.value(QStringLiteral("Thumb"));
        if (!primaryTag.isEmpty()) {
            m_nextEpisodeThumbUrl =
                jellyfinImageSource(next.id, QStringLiteral("Primary"), primaryTag, kNextEpisodeThumbWidth);
        } else if (!thumbTag.isEmpty()) {
            m_nextEpisodeThumbUrl = jellyfinImageSource(next.id, QStringLiteral("Thumb"), thumbTag, kNextEpisodeThumbWidth);
        } else if (!next.parentBackdropItemId.isEmpty() && !next.parentBackdropTags.isEmpty()) {
            m_nextEpisodeThumbUrl = jellyfinImageSource(next.parentBackdropItemId, QStringLiteral("Backdrop"),
                                                        next.parentBackdropTags.first(), kNextEpisodeThumbWidth);
        } else {
            m_nextEpisodeThumbUrl.clear();
        }
        emit nextEpisodeChanged();
        onPositionUpdated();
    };

    m_client->fetchEpisodes(
        seriesId, seasonId, this,
        [this, item, id, seriesId, seasonId, assignNext](bool ok, const QList<MediaItem> &episodes, const QString &) {
            if (id != m_itemId || !ok)
                return;

            // The list copy carries IndexNumber even if the item we were
            // started with didn't.
            MediaItem current = item;
            for (const MediaItem &episode : episodes) {
                if (episode.id == id) {
                    current = episode;
                    break;
                }
            }

            const MediaItem next = PlaybackNavigation::nextEpisodeInSeason(episodes, current);
            if (!next.id.isEmpty()) {
                assignNext(next);
                return;
            }

            // Last in this season: cross into the next one, if any.
            m_client->fetchSeasons(
                seriesId, this,
                [this, id, seriesId, seasonId, assignNext](bool ok, const QList<MediaItem> &seasons, const QString &) {
                    if (id != m_itemId || !ok)
                        return;
                    const QString nextSeason = PlaybackNavigation::nextSeasonId(seasons, seasonId);
                    if (nextSeason.isEmpty())
                        return; // last episode of the last season: no countdown

                    m_client->fetchEpisodes(
                        seriesId, nextSeason, this,
                        [this, id, assignNext](bool ok, const QList<MediaItem> &nextEpisodes, const QString &) {
                            if (id != m_itemId || !ok)
                                return;
                            assignNext(PlaybackNavigation::firstEpisodeInSeason(nextEpisodes));
                        });
                });
        });
}

void PlayerController::onPositionUpdated()
{
    if (!m_videoItem || !m_open || !m_videoItem->isActive())
        return;

    const qint64 position = static_cast<qint64>(m_videoItem->position());
    const qint64 duration = static_cast<qint64>(m_videoItem->duration());

    // --- Next-episode countdown
    bool countdownVisible = false;
    int countdown = 0;
    if (!m_nextEpisode.id.isEmpty() && !m_nextEpisodeCancelled && duration > 0) {
        const PlaybackNavigation::CountdownState state =
            PlaybackNavigation::decideCountdown(position, duration, m_segments);
        countdownVisible = state.visible;
        countdown = static_cast<int>(qMax<qint64>(0, state.secondsRemaining));
    }
    if (countdownVisible != m_nextEpisodeVisible || countdown != m_nextEpisodeCountdown) {
        m_nextEpisodeVisible = countdownVisible;
        m_nextEpisodeCountdown = countdown;
        emit nextEpisodeChanged();
    }
    if (countdownVisible && countdown <= 0 && m_settings && m_settings->autoPlayNextEpisode()) {
        playNextEpisode();
        return;
    }

    // --- Skip intro / credits
    const PlaybackNavigation::SkipAffordance affordance =
        PlaybackNavigation::decideSkipAffordance(m_segments, position);
    bool skipActive = affordance.isActive();
    QString label;
    qint64 target = -1;
    if (skipActive) {
        const bool intro = affordance.kind == PlaybackNavigation::SegmentKind::Intro;
        label = intro ? tr("Skip Intro") : tr("Skip Credits");
        target = affordance.skipToSeconds;

        // The Up Next card already covers the credits.
        if (!intro && countdownVisible)
            skipActive = false;

        if (skipActive && m_settings && m_settings->autoSkipSegments() && m_autoSkippedToSeconds != target) {
            m_autoSkippedToSeconds = target;
            m_videoItem->seek(static_cast<double>(target));
            emit toast(intro ? tr("Skipped intro") : tr("Skipped credits"), QStringLiteral("info"));
            skipActive = false;
        }
    }
    if (!skipActive) {
        label.clear();
        target = -1;
    }
    if (skipActive != m_skipActive || label != m_skipLabel || target != m_skipTargetSeconds) {
        m_skipActive = skipActive;
        m_skipLabel = label;
        m_skipTargetSeconds = target;
        emit skipChanged();
    }
}

void PlayerController::onPausedUpdated()
{
    if (!m_videoItem)
        return;
    m_sleepInhibitor->setInhibited(m_open && m_videoItem->isActive() && !m_videoItem->isPaused());
}

void PlayerController::onEndOfFile()
{
    if (!m_open || !m_videoItem)
        return;

    m_sleepInhibitor->setInhibited(false);

    if (!m_nextEpisode.id.isEmpty() && !m_nextEpisodeCancelled) {
        if (m_settings && m_settings->autoPlayNextEpisode()) {
            playNextEpisode();
            return;
        }
        // Auto-play is off: keep the Up Next card up on the last frame.
        if (!m_nextEpisodeVisible || m_nextEpisodeCountdown != 0) {
            m_nextEpisodeVisible = true;
            m_nextEpisodeCountdown = 0;
            emit nextEpisodeChanged();
        }
        return;
    }

    stopAndClose();
}

// ---------------------------------------------------------------------------
// Stats
// ---------------------------------------------------------------------------

void PlayerController::pollStats()
{
    if (!m_videoItem || !m_open)
        return;

    QVariantMap stats = m_videoItem->collectStatsSnapshot();
    QString playMethod;
    if (m_currentItem.id.isEmpty() && m_itemId.isEmpty())
        playMethod = QStringLiteral("Direct URL");
    else
        playMethod = m_transcoding ? QStringLiteral("Transcode") : QStringLiteral("Direct Play");
    stats.insert(QStringLiteral("playMethod"), playMethod);
    stats.insert(QStringLiteral("transcodeReasons"), m_transcodeReasons.join(QStringLiteral(", ")));
    stats.insert(QStringLiteral("container"), m_container);
    stats.insert(QStringLiteral("fileSize"), formatBytes(m_fileSize));
    stats.insert(QStringLiteral("sourceBitrate"), formatBitsPerSecond(m_totalBitrate));
    stats.insert(QStringLiteral("maxBitrate"),
                 m_maxBitrateMbps > 0 ? QStringLiteral("%1 Mbps").arg(m_maxBitrateMbps) : QStringLiteral("Auto"));
    stats.insert(QStringLiteral("playSessionId"), m_playSessionId);
    stats.insert(QStringLiteral("mediaSourceId"), m_mediaSourceId);
    if (m_client)
        stats.insert(QStringLiteral("serverHost"), QUrl(m_client->serverUrl()).host());

    m_stats = stats;
    emit statsChanged();
}

void PlayerController::resetItemState()
{
    m_itemId.clear();
    m_title.clear();
    m_subtitle.clear();
    m_playSessionId.clear();
    m_mediaSourceId.clear();
    m_container.clear();
    m_fileSize = 0;
    m_totalBitrate = 0;
    m_transcoding = false;
    m_transcodeReasons.clear();

    m_currentItem = MediaItem();
    m_currentSeriesId.clear();
    m_chapters.clear();

    m_segments.clear();
    m_skipActive = false;
    m_skipLabel.clear();
    m_skipTargetSeconds = -1;
    m_autoSkippedToSeconds = -1;

    m_nextEpisode = MediaItem();
    m_nextEpisodeCancelled = false;
    m_nextEpisodeVisible = false;
    m_nextEpisodeTitle.clear();
    m_nextEpisodeSubtitle.clear();
    m_nextEpisodeThumbUrl.clear();
    m_nextEpisodeCountdown = 0;

    m_userChangedAudioTrack = false;
    m_userChangedSubtitleTrack = false;
    m_userChangedSecondarySubtitleTrack = false;
    m_externalSubsLoaded = false;

    m_stats.clear();

    emit itemChanged();
    emit chaptersChanged();
    emit skipChanged();
    emit nextEpisodeChanged();
    emit playbackMetaChanged();
    emit trickplayChanged();
    emit statsChanged();
}
