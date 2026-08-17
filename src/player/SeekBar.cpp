#include "player/SeekBar.h"

#include "core/Formatting.h"
#include "player/TrickplayCache.h"

#include <QCursor>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QStyleOptionSlider>
#include <QStylePainter>
#include <QVBoxLayout>

#include <utility>

// The hover preview shown above the cursor: a trickplay thumbnail (when the
// cache has one) with the formatted timestamp underneath, falling back to
// just the timestamp when trickplay is unavailable. Forward-declared as
// `class TrickplayPopup` in SeekBar.h, so this must stay a plain global-scope
// class -- an anonymous-namespace definition here would be a different type
// and not satisfy that forward declaration.
//
// Qt::ToolTip keeps it out of the taskbar/alt-tab and gives it the platform's
// tooltip-style window behaviour (no focus stealing); FramelessWindowHint
// drops the title bar. Colors come from the ToolTip palette group rather
// than being hardcoded, per DESIGN.md §6.
class TrickplayPopup : public QWidget
{
public:
    explicit TrickplayPopup(QWidget *parent)
        : QWidget(parent, Qt::ToolTip | Qt::FramelessWindowHint)
    {
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAutoFillBackground(true);

        QPalette pal = palette();
        pal.setColor(QPalette::Window, pal.color(QPalette::ToolTipBase));
        pal.setColor(QPalette::WindowText, pal.color(QPalette::ToolTipText));
        setPalette(pal);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(6, 6, 6, 6);
        layout->setSpacing(4);

        m_thumbnailLabel = new QLabel(this);
        m_thumbnailLabel->setAlignment(Qt::AlignCenter);
        m_timeLabel = new QLabel(this);
        m_timeLabel->setAlignment(Qt::AlignCenter);

        layout->addWidget(m_thumbnailLabel);
        layout->addWidget(m_timeLabel);
    }

    void setThumbnail(const QPixmap &pixmap)
    {
        m_thumbnailLabel->setVisible(!pixmap.isNull());
        if (!pixmap.isNull())
            m_thumbnailLabel->setPixmap(pixmap);
    }

    void setTimeText(const QString &text) { m_timeLabel->setText(text); }

private:
    QLabel *m_thumbnailLabel;
    QLabel *m_timeLabel;
};

SeekBar::SeekBar(QWidget *parent)
    : QSlider(Qt::Horizontal, parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void SeekBar::setChapters(const QList<Chapter> &chapters, qint64 durationSeconds)
{
    m_chapters = chapters;
    m_durationSeconds = durationSeconds;
    update();
}

void SeekBar::setTrickplayCache(TrickplayCache *cache)
{
    if (m_trickplay == cache)
        return;

    if (m_trickplay)
        disconnect(m_trickplay, nullptr, this, nullptr);

    m_trickplay = cache;

    if (!m_trickplay)
        return;

    connect(m_trickplay, &TrickplayCache::thumbnailReady, this, [this](qint64 positionMs) {
        if (!m_preview || !m_preview->isVisible() || m_hoverSeconds < 0 || !m_trickplay)
            return;

        // Only repaint if the sheet that just landed is the one the cursor
        // is actually sitting over -- a late reply for a spot the user has
        // since moved away from should not yank the popup's thumbnail.
        const TrickplayInfo &info = m_trickplay->info();
        if (!info.isValid())
            return;
        const qint64 arrivedIndex = positionMs / info.intervalMs;
        const qint64 hoverIndex = (m_hoverSeconds * 1000) / info.intervalMs;
        if (arrivedIndex / info.thumbnailsPerSheet() != hoverIndex / info.thumbnailsPerSheet())
            return;

        updatePreview(mapFromGlobal(QCursor::pos()));
    });
}

int SeekBar::chapterIndexAt(qint64 seconds) const
{
    int result = -1;
    for (int i = 0; i < m_chapters.size(); ++i) {
        const qint64 startSeconds = Formatting::ticksToSeconds(m_chapters.at(i).startTicks);
        if (startSeconds > seconds)
            break;
        result = i;
    }
    return result;
}

qint64 SeekBar::secondsAtX(int x) const
{
    QStyleOptionSlider opt;
    initStyleOption(&opt);
    const QRect groove = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
    const QRect handle = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);

    // The handle's center only travels over (groove.width() - handle.width()),
    // not the full groove -- using the raw groove width here is the classic
    // off-by-handle-width bug that makes markers/clicks land a few pixels
    // away from where the handle itself would go for the same value.
    const int span = groove.width() - handle.width();
    const int pos = x - groove.left() - handle.width() / 2;

    const int value = QStyle::sliderValueFromPosition(minimum(), maximum(), pos, span, opt.upsideDown);
    return static_cast<qint64>(value);
}

int SeekBar::xForSeconds(qint64 seconds) const
{
    QStyleOptionSlider opt;
    initStyleOption(&opt);
    const QRect groove = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
    const QRect handle = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);

    const int span = groove.width() - handle.width();
    const int pos =
        QStyle::sliderPositionFromValue(minimum(), maximum(), static_cast<int>(seconds), span, opt.upsideDown);
    return groove.left() + handle.width() / 2 + pos;
}

void SeekBar::updatePreview(const QPoint &pos)
{
    if (maximum() <= minimum()) {
        hidePreview();
        return;
    }

    const qint64 seconds = secondsAtX(pos.x());
    m_hoverSeconds = seconds;

    if (!m_preview)
        m_preview = new TrickplayPopup(this);

    QPixmap thumbnail;
    if (m_trickplay && m_trickplay->isEnabled())
        thumbnail = m_trickplay->thumbnailAt(seconds * 1000);

    m_preview->setThumbnail(thumbnail);
    m_preview->setTimeText(Formatting::formatPosition(seconds));
    m_preview->adjustSize();

    int popupX = xForSeconds(seconds) - m_preview->width() / 2;
    QPoint topLeft = mapToGlobal(QPoint(popupX, -m_preview->height() - 8));

    if (const QScreen *onScreen = screen()) {
        const QRect avail = onScreen->availableGeometry();
        topLeft.setX(qBound(avail.left(), topLeft.x(), avail.right() - m_preview->width()));
        topLeft.setY(qMax(avail.top(), topLeft.y()));
    }

    m_preview->move(topLeft);
    m_preview->show();
}

void SeekBar::hidePreview()
{
    if (m_preview)
        m_preview->hide();
    m_hoverSeconds = -1;
}

void SeekBar::paintEvent(QPaintEvent *)
{
    QStyleOptionSlider opt;
    initStyleOption(&opt);

    QStylePainter painter(this);
    painter.drawComplexControl(QStyle::CC_Slider, opt);

    if (m_chapters.isEmpty() || maximum() <= minimum())
        return;

    const QRect groove = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);

    // Palette-derived only, never hardcoded, per DESIGN.md §6.
    painter.setPen(QPen(palette().color(QPalette::Highlight), 1));

    const int top = groove.top() + 1;
    const int bottom = groove.bottom() - 1;

    for (const Chapter &chapter : std::as_const(m_chapters)) {
        const qint64 seconds = Formatting::ticksToSeconds(chapter.startTicks);
        if (seconds <= 0)
            continue; // every file has a chapter at 0; skip it, it just clutters the left edge
        const int x = xForSeconds(seconds);
        painter.drawLine(x, top, x, bottom);
    }
}

void SeekBar::mouseMoveEvent(QMouseEvent *event)
{
    QSlider::mouseMoveEvent(event);
    updatePreview(event->pos());
}

void SeekBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        QStyleOptionSlider opt;
        initStyleOption(&opt);
        const QRect handleRect = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);

        if (!handleRect.contains(event->pos())) {
            // A groove click means "go there", not QSlider's default page
            // step. Move the value (and thus the handle) under the cursor
            // first, then let QSlider's own press handling run against the
            // same event -- with the handle now sitting where the click
            // landed, it starts a normal drag from the seeked-to position
            // instead of page-stepping.
            const qint64 seconds = secondsAtX(event->pos().x());
            setValue(static_cast<int>(seconds));
            emit seekRequested(seconds);
        }
    }

    QSlider::mousePressEvent(event);
}

void SeekBar::leaveEvent(QEvent *event)
{
    hidePreview();
    QSlider::leaveEvent(event);
}

bool SeekBar::event(QEvent *event)
{
    switch (event->type()) {
    case QEvent::ToolTip:
        // The hover preview popup is this bar's own tooltip equivalent;
        // suppress Qt's native one so the two never stack.
        return true;
    case QEvent::PaletteChange:
    case QEvent::ApplicationPaletteChange:
        update();
        break;
    default:
        break;
    }
    return QSlider::event(event);
}
