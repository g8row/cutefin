#pragma once

#include "Models.h"

#include <QList>
#include <QString>

// Pure decision logic behind two of the player's "Navigation" features
// (FEATURES.md §C2): skip intro/credits and the next-episode countdown.
//
// Everything here is a free function with no I/O -- no network, no mpv, no
// Settings -- specifically so it can be unit tested against synthetic and
// fixture-captured JSON without a live server. That matters because the
// live Jellyfin 10.11.8 instance used to design this app has no
// intro-detection plugin installed and returns an empty MediaSegments list
// for every item (FEATURES.md §B): the corner button and the segment half
// of this logic can be exercised here, but "does a real Intro segment make
// the button appear at the right moment" cannot be verified against this
// server and needs a manual check against one that has segments.
//
// PlayerWindow owns turning these decisions into UI: fetching segments,
// walking the episode/season chain, driving the widgets, and applying the
// auto-skip/auto-play-next settings.
namespace PlaybackNavigation {

enum class SegmentKind { None, Intro, Outro };

struct SkipAffordance {
    SegmentKind kind = SegmentKind::None;
    // Where "Skip" seeks to: the active segment's end. Meaningless when
    // kind == None.
    qint64 skipToSeconds = 0;

    bool isActive() const { return kind != SegmentKind::None; }
};

// Which skip affordance (if any) should be visible at `positionSeconds` of
// an item with `segments` (its /MediaSegments/{itemId} result, ticks in
// 100ns units like everywhere else in the API). An empty list is the
// normal "no intro-detection plugin configured" result, not an error --
// this returns SegmentKind::None for it exactly like "no segment covers
// this position", so callers don't need a separate empty-list branch.
//
// Only Intro and Outro produce a visible affordance; Recap/Preview/
// Commercial segments exist in the Jellyfin schema but have no UI in this
// phase and are ignored. `positionSeconds` is compared half-open
// [start, end) so the affordance disappears exactly when playback reaches
// the segment's end, which is also where "Skip" would have sent it.
SkipAffordance decideSkipAffordance(const QList<MediaSegment> &segments, qint64 positionSeconds);

struct CountdownState {
    bool visible = false;
    // Clamped to >= 0; only meaningful when visible is true.
    qint64 secondsRemaining = 0;
};

// Whether the next-episode countdown card should be up at `positionSeconds`
// of an item lasting `durationSeconds`, and how many seconds of runtime are
// left. Triggers at the Outro segment's start when the item has one (exact,
// server-provided); otherwise falls back to the last 30s of runtime
// (FEATURES.md §C2 -- the "final 20s" language in the spec describes the
// typical case once the fallback engages close to the very end, not a
// separate threshold).
//
// This function has no idea whether a next episode actually exists -- it
// answers "is playback in the end-of-episode window", full stop. Whether to
// actually show the card also depends on `PlayerWindow` having resolved a
// next episode (nextEpisodeInSeason / nextSeasonId below) and on the user
// not having cancelled it already for this playback; both are the caller's
// job, deliberately kept out of this function so "last episode of the last
// season" is enforced in exactly one place.
CountdownState decideCountdown(qint64 positionSeconds, qint64 durationSeconds, const QList<MediaSegment> &segments);

// The episode following `current` within `episodesInSeason`, chosen by the
// smallest IndexNumber strictly greater than current's -- not by list
// order, since /Shows/{id}/Episodes does not promise the array is sorted
// and a mid-season gap (missing/unaired episode) must not break the chain.
// Returns a default-constructed MediaItem (id.isEmpty()) when `current` has
// no IndexNumber, isn't found in `episodesInSeason`, or genuinely is the
// last episode in the list.
MediaItem nextEpisodeInSeason(const QList<MediaItem> &episodesInSeason, const MediaItem &current);

// The episode with the smallest IndexNumber in `episodesInSeason` -- used
// when the countdown chain crosses into the next season and needs its
// first episode. Returns a default-constructed MediaItem for an empty list.
MediaItem firstEpisodeInSeason(const QList<MediaItem> &episodesInSeason);

// The id of the season following `currentSeasonId` within `seasons`,
// ordered by IndexNumber (season number). Returns an empty string when
// `currentSeasonId` is the last season, isn't present in `seasons`, or
// `seasons` is empty -- this is the exact check that must suppress the
// next-episode countdown when the currently playing episode is the last
// episode of the last season.
QString nextSeasonId(const QList<MediaItem> &seasons, const QString &currentSeasonId);

} // namespace PlaybackNavigation
