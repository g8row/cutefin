#include "Formatting.h"

namespace Formatting {

qint64 ticksToSeconds(qint64 ticks)
{
    return ticks / kTicksPerSecond;
}

qint64 secondsToTicks(qint64 seconds)
{
    return seconds * kTicksPerSecond;
}

QString formatRuntimeTicks(qint64 ticks)
{
    const qint64 totalSeconds = ticksToSeconds(ticks);
    const qint64 totalMinutes = totalSeconds / 60;
    const qint64 hours = totalMinutes / 60;
    const qint64 minutes = totalMinutes % 60;

    if (hours > 0)
        return QStringLiteral("%1h %2m").arg(hours).arg(minutes);
    if (totalMinutes > 0)
        return QStringLiteral("%1m").arg(minutes);
    return QStringLiteral("%1s").arg(totalSeconds);
}

QString formatPosition(qint64 seconds)
{
    if (seconds < 0)
        seconds = 0;

    const qint64 hours = seconds / 3600;
    const qint64 minutes = (seconds % 3600) / 60;
    const qint64 secs = seconds % 60;

    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(secs, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(minutes).arg(secs, 2, 10, QLatin1Char('0'));
}

QString formatRating(double rating)
{
    if (rating < 0.0)
        return QString();
    return QString::number(rating, 'f', 1);
}

} // namespace Formatting
