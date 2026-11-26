#include "TrackSelection.h"

namespace TrackSelection {

namespace {

// Codec match outweighs forced, which outweighs SDH -- see the reasoning in
// pickTrack()'s doc comment in the header.
int scoreCandidate(const TrackDescriptor &track, const RememberedChoice &remembered)
{
    int score = 0;
    if (!remembered.codec.isEmpty() && track.codec.compare(remembered.codec, Qt::CaseInsensitive) == 0)
        score += 4;
    if (track.forced == remembered.forced)
        score += 2;
    if (track.hearingImpaired == remembered.hearingImpaired)
        score += 1;
    return score;
}

} // namespace

RememberedChoice choiceForTrack(const TrackDescriptor &track)
{
    RememberedChoice choice;
    choice.language = track.language;
    choice.codec = track.codec;
    choice.forced = track.forced;
    choice.hearingImpaired = track.hearingImpaired;
    choice.hasChoice = true;
    return choice;
}

int pickTrack(const QList<TrackDescriptor> &tracks, const RememberedChoice &remembered,
              const QString &preferredLanguage)
{
    if (tracks.isEmpty())
        return -1;

    if (remembered.hasChoice && !remembered.language.isEmpty()) {
        const TrackDescriptor *best = nullptr;
        int bestScore = -1;
        for (const TrackDescriptor &track : tracks) {
            if (track.language.compare(remembered.language, Qt::CaseInsensitive) != 0)
                continue; // language is the anchor -- not a candidate at all otherwise
            const int score = scoreCandidate(track, remembered);
            if (!best || score > bestScore || (score == bestScore && track.id < best->id)) {
                best = &track;
                bestScore = score;
            }
        }
        if (best)
            return best->id;
    }

    if (!preferredLanguage.isEmpty()) {
        const TrackDescriptor *best = nullptr;
        for (const TrackDescriptor &track : tracks) {
            if (track.language.compare(preferredLanguage, Qt::CaseInsensitive) != 0)
                continue;
            if (!best || track.id < best->id)
                best = &track;
        }
        if (best)
            return best->id;
    }

    return -1; // let mpv's own default selection stand
}

QString spdifOptionValue(bool enabled)
{
    return enabled ? QStringLiteral("ac3,dts,eac3") : QString();
}

bool canPassthrough(const QString &audioCodec)
{
    const QString codec = audioCodec.toLower();
    return codec == QLatin1String("ac3") || codec == QLatin1String("dts") || codec == QLatin1String("eac3");
}

} // namespace TrackSelection
