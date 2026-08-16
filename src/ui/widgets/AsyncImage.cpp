#include "AsyncImage.h"

#include "core/BlurHash.h"

#include <QEasingCurve>
#include <QPainter>
#include <QResizeEvent>
#include <QScreen>
#include <QVariantAnimation>

namespace {
constexpr int kFadeDurationMs = 180;

// Round up to the next multiple of 100, per DESIGN.md §3's image sizing rule.
int roundUpToNext100(int value)
{
    if (value <= 0)
        return 100;
    return ((value + 99) / 100) * 100;
}
} // namespace

AsyncImage::AsyncImage(QWidget *parent)
    : QWidget(parent)
    , m_fadeAnimation(new QVariantAnimation(this))
{
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    m_fadeAnimation->setDuration(kFadeDurationMs);
    m_fadeAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_fadeAnimation->setStartValue(0.0);
    m_fadeAnimation->setEndValue(1.0);
    connect(m_fadeAnimation, &QVariantAnimation::valueChanged, this, [this]() { notifyRepaint(); });
    connect(m_fadeAnimation, &QVariantAnimation::finished, this, [this]() {
        m_currentPixmap = m_incomingPixmap;
        m_incomingPixmap = QPixmap();
        notifyRepaint();
    });
}

void AsyncImage::notifyRepaint()
{
    update();
    emit repaintNeeded();
}

void AsyncImage::setImageCache(ImageCache *cache)
{
    m_cache = cache;
}

QSize AsyncImage::sizeHint() const
{
    return QSize(150, 225);
}

void AsyncImage::setSource(const QString &itemId, ImageCache::ImageType type, const QString &tag,
                            const QString &blurHash, bool autoLoad)
{
    if (m_itemId == itemId && m_type == type && m_tag == tag) {
        if (autoLoad)
            load();
        return;
    }

    // Retargeted at a different item: cancel any in-flight reply by bumping
    // the generation, and reset to the placeholder immediately so a recycled
    // card never flashes the previous item's poster.
    ++m_generation;
    m_fadeAnimation->stop();
    m_itemId = itemId;
    m_type = type;
    m_tag = tag;
    m_blurHash = blurHash;
    m_hasRealImage = false;
    m_loading = false;
    m_incomingPixmap = QPixmap();
    rebuildPlaceholder();
    m_currentPixmap = m_placeholder;
    notifyRepaint();

    if (autoLoad)
        load();
}

void AsyncImage::rebuildPlaceholder()
{
    if (m_blurHash.isEmpty()) {
        m_placeholder = QPixmap();
        return;
    }
    // Decode at a small fixed size, then let paintEvent scale it up; the DCT
    // synthesis cost scales with output pixel count so keep it tiny.
    const QImage decoded = BlurHash::decode(m_blurHash, 32, 32);
    if (decoded.isNull()) {
        m_placeholder = QPixmap();
        return;
    }
    m_placeholder = QPixmap::fromImage(decoded);
}

int AsyncImage::computeMaxWidth() const
{
    // A widget that is never shown is not associated with a screen, so its
    // own devicePixelRatioF() reports 1.0 -- and PosterCard deliberately
    // keeps its AsyncImage hidden and renders it by hand. Taking that 1.0 at
    // face value makes every poster in the grid arrive at half resolution on
    // a Retina display, which looks soft for no reason the user can see.
    // Fall back to the top-level window and the screen it is on.
    qreal dpr = devicePixelRatioF();
    if (const QWidget *top = window())
        dpr = qMax(dpr, top->devicePixelRatioF());
    if (const QScreen *widgetScreen = screen())
        dpr = qMax(dpr, widgetScreen->devicePixelRatio());

    const int w = qMax(1, width());
    return roundUpToNext100(static_cast<int>(w * dpr));
}

void AsyncImage::load()
{
    if (!m_cache || m_itemId.isEmpty() || m_tag.isEmpty())
        return;
    if (m_hasRealImage || m_loading)
        return;

    m_loading = true;
    const quint64 generation = m_generation;
    m_cache->requestImage(m_itemId, m_type, m_tag, computeMaxWidth(), this,
                           [this, generation](const QPixmap &pixmap) {
                               if (generation != m_generation)
                                   return; // stale: this AsyncImage has since been re-targeted
                               m_loading = false;
                               if (pixmap.isNull())
                                   return;
                               m_hasRealImage = true;
                               beginCrossFade(pixmap);
                           });
}

void AsyncImage::beginCrossFade(const QPixmap &incoming)
{
    m_fadeAnimation->stop();
    m_incomingPixmap = incoming;
    m_fadeAnimation->setStartValue(0.0);
    m_fadeAnimation->setEndValue(1.0);
    m_fadeAnimation->start();
    notifyRepaint();
}

void AsyncImage::release()
{
    if (!m_hasRealImage)
        return;
    // Bump generation so a reply already in flight (unlikely once
    // m_hasRealImage is true, but a re-load could race) is ignored.
    ++m_generation;
    m_fadeAnimation->stop();
    m_hasRealImage = false;
    m_loading = false;
    m_incomingPixmap = QPixmap();
    m_currentPixmap = m_placeholder;
    notifyRepaint();
}

void AsyncImage::clear()
{
    ++m_generation;
    m_fadeAnimation->stop();
    m_itemId.clear();
    m_tag.clear();
    m_blurHash.clear();
    m_placeholder = QPixmap();
    m_currentPixmap = QPixmap();
    m_incomingPixmap = QPixmap();
    m_hasRealImage = false;
    m_loading = false;
    notifyRepaint();
}

void AsyncImage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
}

void AsyncImage::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const QRect r = rect();

    if (!m_currentPixmap.isNull()) {
        painter.drawPixmap(r, m_currentPixmap);
    } else {
        painter.fillRect(r, palette().color(QPalette::Midlight));
    }

    if (!m_incomingPixmap.isNull()) {
        const qreal progress = m_fadeAnimation->currentValue().toDouble();
        painter.setOpacity(progress);
        painter.drawPixmap(r, m_incomingPixmap);
        painter.setOpacity(1.0);
    }
}
