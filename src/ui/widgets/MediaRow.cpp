#include "MediaRow.h"

#include "core/ImageCache.h"
#include "widgets/SmoothScrollArea.h"

#include <QEasingCurve>
#include <QEnterEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QDebug>
#include <QScrollBar>
#include <QShowEvent>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <utility>

namespace {
constexpr int kChevronSize = 32;
constexpr int kChevronMargin = 4;
constexpr int kChevronFadeMs = 160;
constexpr int kCardSpacing = 12;
} // namespace

MediaRow::MediaRow(const QString &title, QWidget *parent)
    : QWidget(parent)
    , m_titleLabel(new QLabel(title, this))
    , m_scrollArea(new SmoothScrollArea(this))
    , m_strip(new QWidget())
    , m_leftChevron(new QToolButton(this))
    , m_rightChevron(new QToolButton(this))
{
    QFont titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 2);
    m_titleLabel->setFont(titleFont);

    auto *stripLayout = new QHBoxLayout(m_strip);
    stripLayout->setContentsMargins(0, 0, 0, 0);
    stripLayout->setSpacing(kCardSpacing);

    // widgetResizable(false): the strip should be exactly as wide as its
    // cards demand so the scroll area actually has something to scroll --
    // a resizable widget would just be clamped to the viewport width.
    m_scrollArea->setWidget(m_strip);
    m_scrollArea->setWidgetResizable(false);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(8);
    outer->addWidget(m_titleLabel);
    outer->addWidget(m_scrollArea);

    for (QToolButton *chevron : {m_leftChevron, m_rightChevron}) {
        chevron->setAutoRaise(true);
        chevron->setFixedSize(kChevronSize, kChevronSize);
        chevron->setVisible(false);
        chevron->setFocusPolicy(Qt::NoFocus);
        chevron->setCursor(Qt::PointingHandCursor);
    }
    m_leftChevron->setArrowType(Qt::LeftArrow);
    m_rightChevron->setArrowType(Qt::RightArrow);

    connect(m_leftChevron, &QToolButton::clicked, this, [this]() { scrollByPage(-1); });
    connect(m_rightChevron, &QToolButton::clicked, this, [this]() { scrollByPage(1); });

    // The scroll offset is the single source of truth for which cards are
    // near the viewport, regardless of whether it moved via wheel, trackpad,
    // or a chevron click.
    connect(m_scrollArea->horizontalScrollBar(), &QScrollBar::valueChanged, this,
            [this](int) { updateVisibleImages(); });

    // The strip's natural width (overflow) and the viewport's width (chevron
    // placement) are not final immediately after setItems() adds cards --
    // catch the layout pass that settles them.
    m_strip->installEventFilter(this);
    m_scrollArea->viewport()->installEventFilter(this);

    setVisible(false); // empty until setItems() supplies content
}

void MediaRow::setImageCache(ImageCache *cache)
{
    m_imageCache = cache;
    for (PosterCard *card : std::as_const(m_cards))
        card->setImageCache(cache);
}

void MediaRow::setItems(const QList<MediaItem> &items, PosterCard::Shape shape)
{
    m_shape = shape;

    qDeleteAll(m_cards);
    m_cards.clear();

    auto *stripLayout = qobject_cast<QHBoxLayout *>(m_strip->layout());
    Q_ASSERT(stripLayout);

    for (const MediaItem &item : items) {
        auto *card = new PosterCard(m_strip);
        card->setImageCache(m_imageCache);
        card->setPosterWidth(m_posterWidth);
        card->setItem(item, m_shape);
        connect(card, &PosterCard::activated, this, &MediaRow::itemActivated);
        connect(card, &PosterCard::contextMenuRequested, this, &MediaRow::itemContextMenuRequested);
        connect(card, &PosterCard::focusStepRequested, this,
                [this, card](PosterCard::FocusDirection direction) { handleCardFocusStep(card, direction); });
        stripLayout->addWidget(card);
        m_cards.append(card);
    }

    m_scrollArea->horizontalScrollBar()->setValue(0);

    // A row that resolves empty removes itself (HomePage relies on this
    // rather than checking isEmpty() after every setItems() call).
    setVisible(!m_cards.isEmpty());

    if (!m_cards.isEmpty())
        m_scrollArea->setFixedHeight(m_cards.first()->sizeHint().height());

    // With setWidgetResizable(false) the scroll area sizes its widget exactly
    // once, inside setWidget() -- which happens in the constructor while the
    // strip is still empty. Nothing resizes it again as cards are added, so
    // without this the strip keeps its empty-layout size and every card is
    // clipped out of view: the row looks blank even though its images loaded.
    m_strip->adjustSize();

    updateChevrons();
    layoutChevrons();

    // Cards have their correct size immediately (PosterCard pins it) but not
    // their correct *position* until the strip's layout runs, and positions
    // are what decides which cards intersect the viewport. Running the pass
    // now would see every card still at x=0, call them all visible, and load
    // the entire row instead of just the visible window -- so defer it one
    // event-loop turn.
    QTimer::singleShot(0, this, [this]() { updateVisibleImages(); });
}

bool MediaRow::isEmpty() const
{
    return m_cards.isEmpty();
}

void MediaRow::setTitle(const QString &title)
{
    m_titleLabel->setText(title);
}

void MediaRow::setPosterWidth(int width)
{
    if (m_posterWidth == width)
        return;
    m_posterWidth = width;

    for (PosterCard *card : std::as_const(m_cards))
        card->setPosterWidth(width);

    if (!m_cards.isEmpty())
        m_scrollArea->setFixedHeight(m_cards.first()->sizeHint().height());

    m_strip->adjustSize();

    updateChevrons();
    layoutChevrons();
    updateVisibleImages();
}

void MediaRow::updateVisibleImages()
{
    if (m_cards.isEmpty())
        return;

    const int viewportWidth = qMax(1, m_scrollArea->viewport()->width());
    const int scrollX = m_scrollArea->horizontalScrollBar()->value();

    // Viewport plus one full viewport-width of prefetch either side --
    // DESIGN.md §5's poster loading discipline. Anything further out
    // releases its pixmap back to the shared cache.
    const int loadLeft = scrollX - viewportWidth;
    const int loadRight = scrollX + viewportWidth + viewportWidth;

    for (PosterCard *card : std::as_const(m_cards)) {
        const int left = card->x();
        const int right = left + card->width();
        if (right >= loadLeft && left <= loadRight)
            card->loadImage();
        else
            card->releaseImage();
    }
}

void MediaRow::updateChevrons()
{
    const bool overflow = m_strip->sizeHint().width() > m_scrollArea->viewport()->width();
    const bool shouldShow = overflow && m_hovered;

    auto fadeIn = [](QToolButton *button) {
        if (button->isVisible() && !button->graphicsEffect())
            return; // already fully shown and settled -- nothing to do

        button->raise();
        button->setVisible(true);

        auto *effect = new QGraphicsOpacityEffect(button);
        effect->setOpacity(0.0);
        button->setGraphicsEffect(effect);

        auto *animation = new QPropertyAnimation(effect, "opacity", button);
        animation->setDuration(kChevronFadeMs);
        animation->setEasingCurve(QEasingCurve::OutCubic);
        animation->setStartValue(0.0);
        animation->setEndValue(1.0);
        connect(animation, &QPropertyAnimation::finished, button, [button]() {
            // A permanent QGraphicsEffect forces every repaint through an
            // offscreen buffer and would tank scroll performance -- remove
            // it once the fade has landed (DESIGN.md §6).
            button->setGraphicsEffect(nullptr);
        });
        animation->start(QAbstractAnimation::DeleteWhenStopped);
    };

    if (shouldShow) {
        fadeIn(m_leftChevron);
        fadeIn(m_rightChevron);
    } else {
        m_leftChevron->setGraphicsEffect(nullptr);
        m_rightChevron->setGraphicsEffect(nullptr);
        m_leftChevron->setVisible(false);
        m_rightChevron->setVisible(false);
    }
}

void MediaRow::layoutChevrons()
{
    const QRect area = m_scrollArea->geometry();
    const int y = area.top() + (area.height() - m_leftChevron->height()) / 2;
    m_leftChevron->move(area.left() + kChevronMargin, y);
    m_rightChevron->move(area.right() - m_rightChevron->width() - kChevronMargin, y);
    m_leftChevron->raise();
    m_rightChevron->raise();
}

void MediaRow::scrollByPage(int direction)
{
    const int page = qMax(1, static_cast<int>(m_scrollArea->viewport()->width() * 0.9));
    m_scrollArea->scrollByPixels(direction * page, 0);
}

void MediaRow::handleCardFocusStep(PosterCard *card, PosterCard::FocusDirection direction)
{
    const int index = m_cards.indexOf(card);
    if (index < 0)
        return;

    int target = index;
    switch (direction) {
    case PosterCard::FocusDirection::Previous:
        target = qMax(0, index - 1);
        break;
    case PosterCard::FocusDirection::Next:
        target = qMin(m_cards.size() - 1, index + 1);
        break;
    case PosterCard::FocusDirection::First:
        target = 0;
        break;
    case PosterCard::FocusDirection::Last:
        target = m_cards.size() - 1;
        break;
    }

    PosterCard *targetCard = m_cards.at(target);
    targetCard->setFocus(Qt::TabFocusReason);
    scrollCardIntoView(targetCard);
}

void MediaRow::scrollCardIntoView(PosterCard *card)
{
    // Ease toward the card via SmoothScrollArea's own accumulate-and-ease
    // machinery (DESIGN.md §5) rather than snapping the scrollbar value --
    // Home/End on a long row can cover a lot of ground in one keypress, and
    // an instant jump there would be exactly the "stepped feel" the smooth
    // scroll area exists to avoid.
    const int viewportWidth = m_scrollArea->viewport()->width();
    const int scrollX = m_scrollArea->horizontalScrollBar()->value();
    const int cardLeft = card->x();
    const int cardRight = cardLeft + card->width();

    int delta = 0;
    if (cardLeft < scrollX)
        delta = cardLeft - scrollX;
    else if (cardRight > scrollX + viewportWidth)
        delta = cardRight - (scrollX + viewportWidth);

    if (delta != 0)
        m_scrollArea->scrollByPixels(delta, 0);
}

void MediaRow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    layoutChevrons();
    updateChevrons();
    updateVisibleImages();
}

void MediaRow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    layoutChevrons();
    updateChevrons();
    updateVisibleImages();
}

void MediaRow::enterEvent(QEnterEvent *event)
{
    QWidget::enterEvent(event);
    m_hovered = true;
    updateChevrons();
}

void MediaRow::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    m_hovered = false;
    updateChevrons();
}

bool MediaRow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Resize && (watched == m_strip || watched == m_scrollArea->viewport())) {
        layoutChevrons();
        updateChevrons();
        updateVisibleImages();
    }
    return QWidget::eventFilter(watched, event);
}
