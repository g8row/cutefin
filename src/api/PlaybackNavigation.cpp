#include "PlaybackNavigation.h"

#include "core/Formatting.h"

namespace PlaybackNavigation {

SkipAffordance decideSkipAffordance(const QList<MediaSegment> &segments, qint64 positionSeconds)
{
    for (const MediaSegment &segment : segments) {
        SegmentKind kind;
        if (segment.type == QLatin1String("Intro"))
            kind = SegmentKind::Intro;
        else if (segment.type == QLatin1String("Outro"))
            kind = SegmentKind::Outro;
        else
            continue; // Recap/Preview/Commercial: no affordance in this phase.

        const qint64 startSeconds = Formatting::ticksToSeconds(segment.startTicks);
        const qint64 endSeconds = Formatting::ticksToSeconds(segment.endTicks);
        if (positionSeconds >= startSeconds && positionSeconds < endSeconds)
            return SkipAffordance{kind, endSeconds};
    }
    return SkipAffordance{};
}

CountdownState decideCountdown(qint64 positionSeconds, qint64 durationSeconds, const QList<MediaSegment> &segments)
{
    if (durationSeconds <= 0)
        return CountdownState{};

    // The Outro segment's start is the server's own idea of "credits begin
    // here" and is exact down to the frame the detector found; the 30s
    // fallback is a blind guess and only used when nothing better exists.
    qint64 triggerSeconds = -1;
    for (const MediaSegment &segment : segments) {
        if (segment.type == QLatin1String("Outro")) {
            triggerSeconds = Formatting::ticksToSeconds(segment.startTicks);
            break;
        }
    }
    if (triggerSeconds < 0)
        triggerSeconds = durationSeconds - 30;

    const qint64 remaining = durationSeconds - positionSeconds;
    CountdownState state;
    state.visible = positionSeconds >= triggerSeconds && remaining > 0;
    state.secondsRemaining = qMax<qint64>(0, remaining);
    return state;
}

MediaItem nextEpisodeInSeason(const QList<MediaItem> &episodesInSeason, const MediaItem &current)
{
    if (current.indexNumber < 0)
        return MediaItem();

    const MediaItem *best = nullptr;
    for (const MediaItem &episode : episodesInSeason) {
        if (episode.indexNumber <= current.indexNumber)
            continue; // behind or equal to current -- not "next"
        if (!best || episode.indexNumber < best->indexNumber)
            best = &episode;
    }
    return best ? *best : MediaItem();
}

MediaItem firstEpisodeInSeason(const QList<MediaItem> &episodesInSeason)
{
    const MediaItem *best = nullptr;
    for (const MediaItem &episode : episodesInSeason) {
        if (episode.indexNumber < 0)
            continue;
        if (!best || episode.indexNumber < best->indexNumber)
            best = &episode;
    }
    return best ? *best : MediaItem();
}

QString nextSeasonId(const QList<MediaItem> &seasons, const QString &currentSeasonId)
{
    int currentIndex = -1;
    bool found = false;
    for (const MediaItem &season : seasons) {
        if (season.id == currentSeasonId) {
            currentIndex = season.indexNumber;
            found = true;
            break;
        }
    }
    if (!found)
        return QString();

    const MediaItem *best = nullptr;
    for (const MediaItem &season : seasons) {
        if (season.indexNumber <= currentIndex)
            continue;
        if (!best || season.indexNumber < best->indexNumber)
            best = &season;
    }
    return best ? best->id : QString();
}

} // namespace PlaybackNavigation
