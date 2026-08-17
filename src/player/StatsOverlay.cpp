#include "player/StatsOverlay.h"

#include "player/MpvWidget.h"

#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QHideEvent>
#include <QList>
#include <QPaintEvent>
#include <QPainter>
#include <QShowEvent>
#include <QStringList>
#include <QTimer>

#include <cmath>
#include <limits>
#include <utility>

namespace {

// mpv leaves a numeric property at 0 for perfectly legitimate reasons (e.g.
// avsync briefly at exactly zero), so 0/-1 can't double as "unset". NaN is
// used as the fallback for every double property instead, and checked with
// std::isnan before a row is emitted.
constexpr double kUnset = std::numeric_limits<double>::quiet_NaN();

// Row.label is normally a plain "label:" prefix, but collectRows() also
// needs to smuggle section headers and Row structs through the same flat
// QList<Row> the header commits us to. A header row's label carries this
// prefix (never legal in a real label) and an empty value; it is rendered
// in bold instead of as a "label: value" pair.
const QString kHeaderMark = QStringLiteral("\x01");

bool isHeaderLabel(const QString &label)
{
    return label.startsWith(kHeaderMark);
}

QString headerTitle(const QString &label)
{
    return label.mid(kHeaderMark.length());
}

QString trimTrailingZeros(QString text)
{
    while (text.endsWith(QLatin1Char('0')))
        text.chop(1);
    if (text.endsWith(QLatin1Char('.')))
        text.chop(1);
    return text;
}

QString formatFps(double fps)
{
    if (std::isnan(fps) || fps <= 0.0)
        return QString();
    return trimTrailingZeros(QString::number(fps, 'f', 3));
}

QString formatBitrateBps(double bitsPerSecond)
{
    if (std::isnan(bitsPerSecond) || bitsPerSecond <= 0.0)
        return QString();
    if (bitsPerSecond >= 1'000'000.0)
        return QStringLiteral("%1 Mbps").arg(bitsPerSecond / 1'000'000.0, 0, 'f', 2);
    return QStringLiteral("%1 kbps").arg(bitsPerSecond / 1000.0, 0, 'f', 0);
}

QString formatBytes(qint64 bytes)
{
    if (bytes <= 0)
        return QString();
    static const char *const kUnits[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    return QStringLiteral("%1 %2").arg(value, 0, 'f', unit == 0 ? 0 : 2).arg(QLatin1String(kUnits[unit]));
}

QString formatBytesPerSecond(double bytesPerSecond)
{
    if (std::isnan(bytesPerSecond) || bytesPerSecond <= 0.0)
        return QString();
    return formatBytes(static_cast<qint64>(bytesPerSecond)) + QStringLiteral("/s");
}

QString formatSeconds(double seconds)
{
    if (std::isnan(seconds) || seconds < 0.0)
        return QString();
    return QStringLiteral("%1 s").arg(seconds, 0, 'f', 2);
}

QString resolutionText(const MpvWidget *mpv)
{
    const qint64 w = mpv->intProperty("width", -1);
    const qint64 h = mpv->intProperty("height", -1);
    if (w <= 0 || h <= 0)
        return QString();
    QString text = QStringLiteral("%1x%2").arg(w).arg(h);
    const qint64 dw = mpv->intProperty("dwidth", -1);
    const qint64 dh = mpv->intProperty("dheight", -1);
    if (dw > 0 && dh > 0 && (dw != w || dh != h))
        text += QStringLiteral(" -> %1x%2").arg(dw).arg(dh);
    return text;
}

QString fpsText(const MpvWidget *mpv)
{
    const QString container = formatFps(mpv->doubleProperty("container-fps", kUnset));
    const QString estimated = formatFps(mpv->doubleProperty("estimated-vf-fps", kUnset));
    if (container.isEmpty() && estimated.isEmpty())
        return QString();
    if (container.isEmpty())
        return estimated + QStringLiteral(" (estimated)");
    if (estimated.isEmpty())
        return container + QStringLiteral(" (container)");
    return QStringLiteral("%1 (container) / %2 (estimated)").arg(container, estimated);
}

QString colourText(const MpvWidget *mpv)
{
    QStringList parts;
    for (const char *name : {"video-params/colormatrix", "video-params/primaries", "video-params/gamma"}) {
        const QString value = mpv->stringProperty(name);
        if (!value.isEmpty())
            parts << value;
    }
    return parts.join(QStringLiteral(" / "));
}

QString hdrPeakText(const MpvWidget *mpv)
{
    const double peak = mpv->doubleProperty("video-params/sig-peak", kUnset);
    if (std::isnan(peak) || peak <= 0.0)
        return QString();
    return QString::number(peak, 'f', 2);
}

QString channelsText(const MpvWidget *mpv)
{
    const QString hrChannels = mpv->stringProperty("audio-params/hr-channels");
    if (!hrChannels.isEmpty())
        return hrChannels;
    const qint64 count = mpv->intProperty("audio-params/channel-count", -1);
    if (count > 0)
        return QStringLiteral("%1 ch").arg(count);
    return QString();
}

QString droppedFramesText(const MpvWidget *mpv)
{
    const qint64 vo = mpv->intProperty("frame-drop-count", -1);
    const qint64 decoder = mpv->intProperty("decoder-frame-drop-count", -1);
    if (vo < 0 && decoder < 0)
        return QString();
    QStringList parts;
    if (vo >= 0)
        parts << QStringLiteral("vo=%1").arg(vo);
    if (decoder >= 0)
        parts << QStringLiteral("decoder=%1").arg(decoder);
    return parts.join(QStringLiteral(", "));
}

QString avsyncText(const MpvWidget *mpv)
{
    const double value = mpv->doubleProperty("avsync", kUnset);
    if (std::isnan(value))
        return QString();
    return QStringLiteral("%1 ms").arg(value * 1000.0, 0, 'f', 1);
}

QString displayVsVideoFpsText(const MpvWidget *mpv)
{
    QString display = formatFps(mpv->doubleProperty("display-fps", kUnset));
    if (display.isEmpty())
        display = formatFps(mpv->doubleProperty("estimated-display-fps", kUnset));
    const QString video = formatFps(mpv->doubleProperty("estimated-vf-fps", kUnset));
    if (display.isEmpty() && video.isEmpty())
        return QString();
    if (display.isEmpty())
        return QStringLiteral("video=%1").arg(video);
    if (video.isEmpty())
        return QStringLiteral("display=%1").arg(display);
    return QStringLiteral("display=%1 video=%2").arg(display, video);
}

} // namespace

StatsOverlay::StatsOverlay(MpvWidget *mpv, QWidget *parent)
    : QWidget(parent)
    , m_mpv(mpv)
    , m_timer(new QTimer(this))
{
    // Purely informational -- never wants to steal clicks meant for the
    // player controls underneath it.
    setAttribute(Qt::WA_TransparentForMouseEvents);

    m_timer->setInterval(kPollIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &StatsOverlay::refresh);
}

void StatsOverlay::setPlaybackContext(const QString &playSessionId, const QString &mediaSourceId, bool transcoding,
                                       const QStringList &transcodeReasons, const QString &serverName)
{
    m_playSessionId = playSessionId;
    m_mediaSourceId = mediaSourceId;
    m_transcoding = transcoding;
    m_transcodeReasons = transcodeReasons;
    m_serverName = serverName;

    // Only rebuild (which touches mpv) if we're actually on screen -- the
    // timer being stopped while hidden is what keeps this class free while
    // nobody's looking at it, and a context update shouldn't punch a hole
    // in that guarantee.
    if (isVisible())
        refresh();
}

void StatsOverlay::appendLogLine(const QString &line)
{
    m_logLines.append(line);
    while (m_logLines.size() > kMaxLogLines)
        m_logLines.removeFirst();
}

QString StatsOverlay::diagnosticsText() const
{
    // Rebuilt fresh rather than reusing m_rows: this is a one-shot "copy to
    // clipboard" action, not the 2 Hz paint path, so the extra mpv round
    // trip here is fine and gives the pasted report current numbers even if
    // the overlay was just opened.
    QStringList lines;
    lines << QStringLiteral("cutefin diagnostics");
    for (const Row &row : collectRows()) {
        if (isHeaderLabel(row.label)) {
            lines << QString() << headerTitle(row.label);
        } else if (row.label.isEmpty()) {
            lines << row.value;
        } else {
            lines << QStringLiteral("  %1: %2").arg(row.label, row.value);
        }
    }
    return lines.join(QLatin1Char('\n'));
}

void StatsOverlay::refresh()
{
    m_rows = collectRows();
    update();
}

QList<StatsOverlay::Row> StatsOverlay::collectRows() const
{
    QList<Row> rows;
    if (!m_mpv || !m_mpv->isUsable())
        return rows;

    auto addRow = [](QList<Row> &target, const QString &label, const QString &value) {
        if (!value.isEmpty())
            target.append({label, value});
    };
    auto appendSection = [&rows](const QString &title, const QList<Row> &section) {
        if (section.isEmpty())
            return;
        rows.append({kHeaderMark + title, QString()});
        rows.append(section);
    };

    QList<Row> video;
    addRow(video, QStringLiteral("Codec"), m_mpv->stringProperty("video-codec"));
    addRow(video, QStringLiteral("Format"), m_mpv->stringProperty("video-format"));
    addRow(video, QStringLiteral("VO"), m_mpv->stringProperty("current-vo"));
    addRow(video, QStringLiteral("HW decode"), m_mpv->stringProperty("hwdec-current"));
    addRow(video, QStringLiteral("Resolution"), resolutionText(m_mpv));
    addRow(video, QStringLiteral("Pixel format"), m_mpv->stringProperty("video-params/pixelformat"));
    addRow(video, QStringLiteral("FPS"), fpsText(m_mpv));
    addRow(video, QStringLiteral("Bitrate"), formatBitrateBps(m_mpv->doubleProperty("video-bitrate", kUnset)));
    addRow(video, QStringLiteral("Colour"), colourText(m_mpv));
    addRow(video, QStringLiteral("HDR sig-peak"), hdrPeakText(m_mpv));
    appendSection(QStringLiteral("Video"), video);

    QList<Row> audio;
    addRow(audio, QStringLiteral("Codec"), m_mpv->stringProperty("audio-codec-name"));
    addRow(audio, QStringLiteral("Channels"), channelsText(m_mpv));
    const qint64 sampleRate = m_mpv->intProperty("audio-params/samplerate", -1);
    if (sampleRate > 0)
        audio.append({QStringLiteral("Sample rate"), QStringLiteral("%1 Hz").arg(sampleRate)});
    addRow(audio, QStringLiteral("Bitrate"), formatBitrateBps(m_mpv->doubleProperty("audio-bitrate", kUnset)));
    addRow(audio, QStringLiteral("Device"), m_mpv->stringProperty("audio-device"));
    appendSection(QStringLiteral("Audio"), audio);

    QList<Row> perf;
    addRow(perf, QStringLiteral("Dropped frames"), droppedFramesText(m_mpv));
    addRow(perf, QStringLiteral("A/V sync"), avsyncText(m_mpv));
    const qint64 delayed = m_mpv->intProperty("vo-delayed-frame-count", -1);
    if (delayed >= 0)
        perf.append({QStringLiteral("Delayed frames"), QString::number(delayed)});
    addRow(perf, QStringLiteral("Display/video FPS"), displayVsVideoFpsText(m_mpv));
    appendSection(QStringLiteral("Performance"), perf);

    QList<Row> net;
    addRow(net, QStringLiteral("Cache duration"), formatSeconds(m_mpv->doubleProperty("demuxer-cache-duration", kUnset)));
    addRow(net, QStringLiteral("Cache speed"), formatBytesPerSecond(m_mpv->doubleProperty("cache-speed", kUnset)));
    net.append({QStringLiteral("Cache idle"),
                m_mpv->flagProperty("demuxer-cache-idle", false) ? QStringLiteral("yes") : QStringLiteral("no")});
    appendSection(QStringLiteral("Network / cache"), net);

    QList<Row> jellyfin;
    jellyfin.append(
        {QStringLiteral("Mode"), m_transcoding ? QStringLiteral("Transcoding") : QStringLiteral("Direct Play")});
    if (m_transcoding && !m_transcodeReasons.isEmpty())
        jellyfin.append({QStringLiteral("Reasons"), m_transcodeReasons.join(QStringLiteral(", "))});
    addRow(jellyfin, QStringLiteral("Play session"), m_playSessionId);
    addRow(jellyfin, QStringLiteral("Media source"), m_mediaSourceId);
    addRow(jellyfin, QStringLiteral("Server"), m_serverName);
    addRow(jellyfin, QStringLiteral("Container"), m_mpv->stringProperty("file-format"));
    addRow(jellyfin, QStringLiteral("File size"), formatBytes(m_mpv->intProperty("file-size", -1)));
    appendSection(QStringLiteral("Jellyfin"), jellyfin);

    if (!m_logLines.isEmpty()) {
        QList<Row> log;
        for (const QString &line : m_logLines)
            log.append({QString(), line});
        appendSection(QStringLiteral("Log"), log);
    }

    return rows;
}

void StatsOverlay::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // Populate immediately so the first paint isn't blank while waiting for
    // the first timer tick, then start polling at 2 Hz for as long as we're
    // actually visible.
    refresh();
    m_timer->start();
}

void StatsOverlay::hideEvent(QHideEvent *event)
{
    // This is the entire cost guarantee: no timer running, no property
    // queries, nothing -- while the overlay is hidden it does not exist as
    // far as the CPU is concerned.
    m_timer->stop();
    QWidget::hideEvent(event);
}

void StatsOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QColor scrim = palette().color(QPalette::Shadow);
    scrim.setAlpha(200);
    painter.setPen(Qt::NoPen);
    painter.setBrush(scrim);
    painter.drawRoundedRect(rect(), 8, 8);

    if (m_rows.isEmpty())
        return;

    // paintEvent only ever draws m_rows, collected by the last refresh() --
    // it must never touch mpv itself, since Qt can call this far more often
    // than the 2 Hz poll rate (e.g. on every video frame while visible).
    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    QFont headerFont = mono;
    headerFont.setBold(true);

    const QFontMetrics metrics(mono);
    const int lineHeight = metrics.height() + 2;
    const int margin = 10;
    const int valueColumnX = margin + metrics.horizontalAdvance(QLatin1Char('0')) * 22;
    const QColor textColor = palette().color(QPalette::Text);

    painter.setClipRect(rect());
    int y = margin + metrics.ascent();
    bool firstRow = true;

    for (const Row &row : std::as_const(m_rows)) {
        if (y > height() - margin)
            break;

        if (isHeaderLabel(row.label)) {
            if (!firstRow)
                y += lineHeight / 2;
            painter.setFont(headerFont);
            painter.setPen(textColor);
            painter.drawText(margin, y, headerTitle(row.label));
            y += lineHeight;
            firstRow = false;
            continue;
        }

        painter.setFont(mono);
        painter.setPen(textColor);
        if (row.label.isEmpty()) {
            // A log line -- no label column, just the message.
            painter.drawText(margin, y, metrics.elidedText(row.value, Qt::ElideRight, width() - 2 * margin));
        } else {
            painter.drawText(margin, y, row.label + QLatin1Char(':'));
            const int valueWidth = width() - valueColumnX - margin;
            if (valueWidth > 0)
                painter.drawText(valueColumnX, y, metrics.elidedText(row.value, Qt::ElideRight, valueWidth));
        }
        y += lineHeight;
        firstRow = false;
    }
}
