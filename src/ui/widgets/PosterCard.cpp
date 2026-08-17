#include "PosterCard.h"

#include "AsyncImage.h"

#include <QAccessible>
#include <QAccessibleWidget>
#include <QContextMenuEvent>
#include <QEasingCurve>
#include <QEnterEvent>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMetaMethod>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QVariantAnimation>

namespace {
constexpr int kHoverDurationMs = 140;
constexpr qreal kMaxHoverScale = 1.03;
constexpr qreal kFocusRingWidth = 2.0;
constexpr int kProgressStripHeight = 3;
constexpr int kWatchedDotSize = 16;
constexpr int kWatchedDotMargin = 4;
constexpr int kTextGap = 2;

#ifndef QT_NO_ACCESSIBILITY
// Setting accessibleName() on a PosterCard is necessary but *not sufficient*:
// Qt hands a plain QWidget subclass the generic QAccessible::Client role, and
// the macOS Cocoa bridge treats such an element as "ignored", so the card
// never appears in the accessibility tree at all -- verified by dumping the
// tree of the running app, where a full screen of posters contributed zero
// elements. Publishing an interface with a Button role is what actually makes
// the cards reachable by VoiceOver and by UI automation.
class PosterCardAccessible : public QAccessibleWidget
{
public:
    explicit PosterCardAccessible(QWidget *widget)
        : QAccessibleWidget(widget, QAccessible::Button)
    {
    }

    QString text(QAccessible::Text type) const override
    {
        const auto *card = qobject_cast<const PosterCard *>(object());
        if (card) {
            switch (type) {
            case QAccessible::Name:
                return card->accessibleName();
            case QAccessible::Description:
                return card->accessibleDescription();
            default:
                break;
            }
        }
        return QAccessibleWidget::text(type);
    }

    QStringList actionNames() const override
    {
        return QStringList{QAccessibleActionInterface::pressAction()} + QAccessibleWidget::actionNames();
    }

    void doAction(const QString &actionName) override
    {
        if (actionName == QAccessibleActionInterface::pressAction()) {
            if (auto *card = qobject_cast<PosterCard *>(object()))
                card->activate();
            return;
        }
        QAccessibleWidget::doAction(actionName);
    }
};

QAccessibleInterface *posterCardAccessibleFactory(const QString &className, QObject *object)
{
    if (className == QLatin1String("PosterCard") && object && object->isWidgetType())
        return new PosterCardAccessible(static_cast<QWidget *>(object));
    return nullptr;
}
#endif // QT_NO_ACCESSIBILITY
} // namespace

PosterCard::PosterCard(QWidget *parent)
    : QWidget(parent)
    , m_image(new AsyncImage(this))
    , m_hoverAnimation(new QVariantAnimation(this))
{
    setFocusPolicy(Qt::StrongFocus);

#ifndef QT_NO_ACCESSIBILITY
    // Installed once for the process on the first card built. A factory is
    // per-application state, not per-widget, and installing it repeatedly
    // would just prepend duplicates to the factory chain.
    static const bool accessibilityFactoryInstalled = [] {
        QAccessible::installFactory(&posterCardAccessibleFactory);
        return true;
    }();
    Q_UNUSED(accessibilityFactoryInstalled);
#endif

    // m_image is never shown through Qt's own child-widget compositing.
    // PosterCard needs full control over paint order so the progress strip,
    // watched dot, and focus ring can be drawn ON TOP of the artwork, and so
    // the 1.0->1.03 hover scale (a transform, not a real resize -- geometry
    // of a laid-out widget must never be animated) can be applied. Instead
    // paintEvent() renders m_image manually via QWidget::render() every
    // time, which works whether or not the widget is visible.
    m_image->setVisible(false);

    // A hidden widget gets no repaints of its own, so AsyncImage's 180ms
    // cross-fade would be invisible here. This signal is what makes the fade
    // show up without polling it on a timer.
    connect(m_image, &AsyncImage::repaintNeeded, this, [this]() { update(); });

    m_hoverAnimation->setDuration(kHoverDurationMs);
    m_hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_hoverAnimation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &value) { setHoverProgress(value.toDouble()); });

    applyFixedSize();
}

void PosterCard::setImageCache(ImageCache *cache)
{
    m_image->setImageCache(cache);
}

void PosterCard::setItem(const MediaItem &item, Shape shape)
{
    m_item = item;
    m_shape = shape;

    QString tag;
    ImageCache::ImageType type = ImageCache::ImageType::Primary;

    if (shape == Shape::Poster) {
        tag = item.imageTags.value(QStringLiteral("Primary"));
        type = ImageCache::ImageType::Primary;
        if (tag.isEmpty() && item.type == QStringLiteral("Episode")) {
            // Episodes frequently have no Primary of their own; fall back to
            // the episode's own Thumb. DESIGN.md also asks for a further
            // fallback to the series' Primary image, but MediaItem carries
            // no seriesPrimaryImageTag field to source that from -- see the
            // implementation report for this gap.
            tag = item.imageTags.value(QStringLiteral("Thumb"));
            type = ImageCache::ImageType::Thumb;
        }
    } else {
        tag = item.imageTags.value(QStringLiteral("Thumb"));
        type = ImageCache::ImageType::Thumb;
        if (tag.isEmpty()) {
            tag = item.imageTags.value(QStringLiteral("Primary"));
            type = ImageCache::ImageType::Primary;
        }
    }

    const QString blurHash = item.blurHashes.value(ImageCache::imageTypeToString(type)).value(tag);
    // MediaRow/LibraryPage decide when to actually fetch; setItem only ever
    // primes the placeholder.
    m_image->setSource(item.id, type, tag, blurHash, /*autoLoad=*/false);

    if (item.type == QStringLiteral("Episode")) {
        m_title = item.name;
        QStringList context;
        if (!item.seriesName.isEmpty())
            context << item.seriesName;
        if (item.parentIndexNumber >= 0 && item.indexNumber >= 0)
            context << QStringLiteral("S%1:E%2").arg(item.parentIndexNumber).arg(item.indexNumber);
        else if (item.indexNumber >= 0)
            context << QStringLiteral("E%1").arg(item.indexNumber);
        m_subtitle = context.join(QStringLiteral(" · "));
    } else {
        // Movie, Series, and anything else fall back to the same shape:
        // name plus release year.
        m_title = item.name;
        m_subtitle = item.productionYear > 0 ? QString::number(item.productionYear) : QString();
    }

    m_progressFraction = (item.userData.playedPercentage > 0.0 && item.userData.playedPercentage < 100.0)
        ? item.userData.playedPercentage / 100.0
        : 0.0;
    m_played = item.userData.played;

    updateAccessibility();
    applyFixedSize();
    update();
}

void PosterCard::activate()
{
    emit activated(m_item);
}

void PosterCard::loadImage()
{
    m_image->load();
}

void PosterCard::releaseImage()
{
    m_image->release();
    update();
}

void PosterCard::setTitleText(const QString &title)
{
    m_title = title;
    updateAccessibility();
    update();
}

void PosterCard::setSubtitleText(const QString &subtitle)
{
    m_subtitle = subtitle;
    updateAccessibility();
    update();
}

void PosterCard::setPosterWidth(int width)
{
    if (m_posterWidth == width)
        return;
    m_posterWidth = width;
    applyFixedSize();
    update();
}

void PosterCard::applyFixedSize()
{
    // A card's size is fully determined by its poster width and shape, so
    // pin it rather than waiting for the parent layout to get around to it.
    // This is not cosmetic: AsyncImage derives the maxWidth it asks the
    // server for from its own geometry, and MediaRow triggers the first
    // image load in the same turn the cards are created -- before any layout
    // pass has run. Without a size that is correct immediately, every poster
    // is fetched at the default-widget 100px and, because the load is
    // one-shot, never re-fetched at the real size.
    setFixedSize(sizeHint());
    updateGeometry();
    relayoutChildren();
}

void PosterCard::setHoverProgress(qreal progress)
{
    m_hoverProgress = progress;
    update();
}

QSize PosterCard::sizeHint() const
{
    const int w = m_posterWidth;
    const qreal aspect = (m_shape == Shape::Poster) ? 1.5 : (9.0 / 16.0);
    const int artHeight = qRound(w * aspect);
    const QFontMetrics fm(font());
    const int textHeight = fm.height() * 2 + kTextGap;
    return QSize(w, artHeight + kTextGap + textHeight);
}

QRect PosterCard::artworkRect() const
{
    const int w = qMax(1, width());
    const qreal aspect = (m_shape == Shape::Poster) ? 1.5 : (9.0 / 16.0);
    const int h = qRound(w * aspect);
    return QRect(0, 0, w, h);
}

void PosterCard::relayoutChildren()
{
    // m_image's geometry is set once per resize/item change, never as part
    // of the hover animation -- the visual zoom is a paint-time transform
    // only (see paintEvent()), so this never fights the "don't animate
    // geometry" rule.
    m_image->setGeometry(artworkRect());
}

void PosterCard::updateAccessibility()
{
    // PosterCard paints its own title/subtitle -- there is no QLabel behind
    // them for the platform to expose, so this is the entire fix for
    // FEATURES.md §C6's "QAccessible names on custom widgets": an empty
    // accessibleName is what makes the macOS accessibility bridge treat a
    // generic-role QWidget as ignored, i.e. invisible to VoiceOver/the
    // Accessibility Inspector even though it is perfectly Tab-focusable.
    setAccessibleName(m_title);

    QStringList description;
    if (!m_subtitle.isEmpty())
        description << m_subtitle;
    if (m_played)
        description << QStringLiteral("Watched");
    else if (m_progressFraction > 0.0)
        description << QStringLiteral("%1% watched").arg(qRound(m_progressFraction * 100));
    setAccessibleDescription(description.join(QStringLiteral(" · ")));

    // The title is elided at paint time with nothing behind it to fall back
    // on, so a tooltip is the only place a full title that doesn't fit is
    // ever recoverable.
    setToolTip(m_title);
}

void PosterCard::animateHoverTo(qreal target)
{
    if (m_hoverAnimation->state() == QAbstractAnimation::Running && m_hoverAnimation->endValue().toDouble() == target)
        return;
    m_hoverAnimation->stop();
    m_hoverAnimation->setStartValue(m_hoverProgress);
    m_hoverAnimation->setEndValue(target);
    m_hoverAnimation->start();
}

void PosterCard::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const QRect art = artworkRect();

    // Artwork, scaled 1.0->1.03 about its own center as hoverProgress rises.
    // Always routed through render() (see constructor) so paint order below
    // this line reliably lands on top of it.
    {
        const qreal scale = 1.0 + (kMaxHoverScale - 1.0) * m_hoverProgress;
        painter.save();
        const QPointF center = QRectF(art).center();
        painter.translate(center);
        painter.scale(scale, scale);
        painter.translate(-center);
        m_image->render(&painter, art.topLeft());
        painter.restore();
    }

    // Focus ring: same hoverProgress drives hover and keyboard focus
    // identically, per DESIGN.md §5.
    if (m_hoverProgress > 0.0) {
        QColor ring = palette().color(QPalette::Highlight);
        ring.setAlphaF(static_cast<float>(m_hoverProgress));
        painter.save();
        painter.setPen(QPen(ring, kFocusRingWidth));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(art).adjusted(1, 1, -1, -1), 6, 6);
        painter.restore();
    }

    // Progress strip across the bottom of the artwork for a partially
    // watched item.
    if (m_progressFraction > 0.0) {
        const QRect track(art.left(), art.bottom() - kProgressStripHeight, art.width(), kProgressStripHeight);
        QColor trackColor = palette().color(QPalette::Shadow);
        trackColor.setAlpha(160);
        painter.fillRect(track, trackColor);

        QRect fill = track;
        fill.setWidth(qRound(track.width() * m_progressFraction));
        painter.fillRect(fill, palette().color(QPalette::Highlight));
    }

    // Small "watched" indicator in the artwork's top-right corner.
    if (m_played) {
        const QRect dot(art.right() - kWatchedDotSize - kWatchedDotMargin, art.top() + kWatchedDotMargin,
                         kWatchedDotSize, kWatchedDotSize);
        painter.save();
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette().color(QPalette::Highlight));
        painter.drawEllipse(dot);

        painter.setPen(QPen(palette().color(QPalette::HighlightedText), 2.0));
        const QPoint p1(dot.left() + 4, dot.center().y());
        const QPoint p2(dot.left() + 7, dot.bottom() - 4);
        const QPoint p3(dot.right() - 4, dot.top() + 4);
        painter.drawLine(p1, p2);
        painter.drawLine(p2, p3);
        painter.restore();
    }

    // Title / subtitle beneath the artwork.
    const QFontMetrics fm(font());
    const QRect titleRect(0, art.bottom() + kTextGap, width(), fm.height());
    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(m_title, Qt::ElideRight, titleRect.width()));

    if (!m_subtitle.isEmpty()) {
        const QRect subtitleRect(0, titleRect.bottom(), width(), fm.height());
        // Disabled/Text is a palette-derived subdued tone -- no hardcoded
        // color needed for secondary text.
        painter.setPen(palette().color(QPalette::Disabled, QPalette::Text));
        painter.drawText(subtitleRect, Qt::AlignLeft | Qt::AlignVCenter,
                          fm.elidedText(m_subtitle, Qt::ElideRight, subtitleRect.width()));
    }
}

void PosterCard::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    relayoutChildren();
}

void PosterCard::enterEvent(QEnterEvent *event)
{
    QWidget::enterEvent(event);
    animateHoverTo(1.0);
}

void PosterCard::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    if (!hasFocus())
        animateHoverTo(0.0);
}

void PosterCard::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        setFocus(Qt::MouseFocusReason);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void PosterCard::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->pos())) {
        activate();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void PosterCard::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        activate();
        event->accept();
        return;
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Home:
    case Qt::Key_End: {
        // Only MediaRow connects to focusStepRequested() -- a standalone
        // card (PosterGrid's pool) has no listener, so isSignalConnected()
        // lets these keys fall straight through to the default handling
        // below exactly as they did before this feature existed, instead of
        // being silently swallowed for a widget nobody wired up navigation
        // for.
        static const QMetaMethod signal = QMetaMethod::fromSignal(&PosterCard::focusStepRequested);
        if (isSignalConnected(signal)) {
            FocusDirection direction = FocusDirection::Next;
            if (event->key() == Qt::Key_Left)
                direction = FocusDirection::Previous;
            else if (event->key() == Qt::Key_Home)
                direction = FocusDirection::First;
            else if (event->key() == Qt::Key_End)
                direction = FocusDirection::Last;
            emit focusStepRequested(direction);
            event->accept();
            return;
        }
        break;
    }
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}

void PosterCard::focusInEvent(QFocusEvent *event)
{
    QWidget::focusInEvent(event);
    animateHoverTo(1.0);
}

void PosterCard::focusOutEvent(QFocusEvent *event)
{
    QWidget::focusOutEvent(event);
    if (!underMouse())
        animateHoverTo(0.0);
}

void PosterCard::contextMenuEvent(QContextMenuEvent *event)
{
    emit contextMenuRequested(m_item, event->globalPos());
    event->accept();
}
