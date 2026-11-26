#pragma once

#include <QList>
#include <QString>

// Pure decision logic behind the "remembered track choices" half of
// FEATURES.md §C2: matching a remembered audio/subtitle pick against a newly
// loaded file's tracks by *attributes* (language, codec, forced, hearing-
// impaired/SDH) rather than by mpv track id or list index. Indices are
// meaningless across files -- S1E1's track 2 can be a different language
// than S1E2's track 2 depending on how each episode was muxed -- which is
// exactly the bug this exists to avoid.
//
// Nothing here knows mpv or Jellyfin API types exist, so it can be unit
// tested without either. PlayerWindow adapts MpvWidget::Track into
// TrackDescriptor for embedded tracks (on direct play mpv's track-list is
// every track there is, DESIGN.md §7) and MediaStream into the same shape
// for an external Jellyfin subtitle before it has even been sub-add'ed to
// mpv, so both kinds of track go through one matcher. This mirrors
// src/api/PlaybackNavigation.h's separation for the other half of Phase 3b.
namespace TrackSelection {

struct TrackDescriptor {
    int id = -1; // mpv "aid"/"sid" value for *this* file; not comparable across files
    QString language; // ISO-639-ish, exactly as mpv/Jellyfin report it; may be empty
    QString codec; // e.g. "eac3", "subrip", "ass"; may be empty
    QString title;
    bool forced = false;
    bool hearingImpaired = false; // SDH -- mpv's track-list/N/hearing-impaired, Jellyfin's IsHearingImpaired
    // Not inside the container (Jellyfin IsExternal / mpv's external flag on
    // a sub-add'ed track). Informational only -- pickTrack() does not treat
    // an external track any differently, since a remembered SDH English
    // track should be found whether this file ships it embedded or as a
    // sidecar .srt.
    bool external = false;
};

// What Settings persists per series (rememberedAudioChoice() et al.) and
// feeds back into pickTrack() the next time an episode of the same series
// loads. `hasChoice` distinguishes "the user picked track X, whose
// attributes happen to all be empty/false" from "nothing has been picked for
// this series yet" -- a default-constructed RememberedChoice must not be
// mistaken for a real (if unusual) choice.
struct RememberedChoice {
    QString language;
    QString codec;
    bool forced = false;
    bool hearingImpaired = false;
    bool hasChoice = false;
};

// The RememberedChoice to persist after the user explicitly selects `track`
// from the audio/subtitle menu -- what PlayerWindow hands to
// Settings::setRememberedAudioChoice()/setRememberedSubtitleChoice().
RememberedChoice choiceForTrack(const TrackDescriptor &track);

// Picks the best-matching track id in `tracks`, degrading in steps so the
// user is never left with no audio/subtitles selected at all (Phase 3b
// requirement: matching must degrade gracefully):
//
//   1. If `remembered` has a choice with a non-empty language: every track
//      sharing that language is a candidate. Among candidates, a codec match
//      scores higher than a forced match, which scores higher than an SDH
//      match (a codec change between two rips of the same show, e.g. AC3 ->
//      E-AC3, is far more likely than a genuine forced/SDH difference, so
//      losing the codec match should cost less certainty than losing the
//      forced/SDH match would). The highest-scoring candidate wins; ties are
//      broken by the lowest id, which is arbitrary but deterministic -- a
//      tie only happens between candidates that are otherwise
//      indistinguishable by the attributes that matter, so which one gets
//      picked doesn't matter, only that repeated calls agree.
//   2. Otherwise, or if no track shares the remembered language at all (the
//      language was dropped from this release, or this is the first episode
//      of a series with no remembered choice yet): the first track matching
//      `preferredLanguage`, the global setting, lowest id wins ties.
//   3. Otherwise: -1, meaning "leave mpv's own default track selection
//      alone". By the time tracksChanged() fires mpv has already applied its
//      own default-track heuristic, so -1 is a real, safe answer here, not a
//      failure the caller needs to handle specially.
//
// Always returns -1 for an empty `tracks` list -- there is nothing to pick.
int pickTrack(const QList<TrackDescriptor> &tracks, const RememberedChoice &remembered,
              const QString &preferredLanguage);

// The exact codec list FEATURES.md §C2 names for audio-spdif passthrough --
// mpv's own --audio-spdif accepts a longer list (dts-hd, truehd, ...) but
// enabling this app's toggle only ever asks for what the spec calls out.
// Returns the value to set mpv's "audio-spdif" property to (empty string,
// i.e. mpv's own default of "no passthrough", when disabled).
QString spdifOptionValue(bool enabled);

// Whether `audioCodec` (as mpv's "audio-codec-name"/track-list "codec"
// reports it, e.g. "ac3", "eac3", "dts") is one of the codecs spdifOptionValue()
// passes through. audio-spdif is not a blanket "force passthrough" switch --
// it only takes effect when the *currently selected* track already uses a
// listed codec -- so a caller must check this before claiming the toggle
// took effect, and report through the UI when it didn't (Phase 3b
// requirement 3).
bool canPassthrough(const QString &audioCodec);

} // namespace TrackSelection
