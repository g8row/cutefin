#pragma once

#include <QString>
#include <QtGlobal>

// Small stateless helpers for turning Jellyfin's raw numeric fields into
// display strings. Jellyfin ticks are 100ns units: ticks = seconds * 10'000'000.
namespace Formatting {

constexpr qint64 kTicksPerSecond = 10'000'000LL;

qint64 ticksToSeconds(qint64 ticks);
qint64 secondsToTicks(qint64 seconds);

// "1h 52m" (or "52m" if under an hour, "45s" if under a minute).
QString formatRuntimeTicks(qint64 ticks);

// "12:34" for under an hour, "1:12:34" for an hour or more.
QString formatPosition(qint64 seconds);

// Community/critic rating with one decimal, e.g. "6.9". Returns an empty
// string for a negative/absent rating.
QString formatRating(double rating);

} // namespace Formatting
