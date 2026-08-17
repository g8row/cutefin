#include "PosterGrid.h"

#include "core/ImageCache.h"
#include "widgets/SmoothScrollArea.h"

#include <QFontMetrics>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSet>

#include <utility>

namespace {
constexpr int kGridSpacing = 12; // matches MediaRow's kCardSpacing -- one spacing constant across browse surfaces
constexpr int kPageSize = 100;
constexpr int kOverscanScreens = 1; // one viewport-height of overscan above and below, per FEATURES.md §C1
constexpr int kTextGap = 2;         // mirrors PosterCard.cpp's private kTextGap

// Mirrors PosterCard::sizeHint()'s own formula. Duplicated here rather than
// asking a scratch PosterCard for its sizeHint(): cellRect() runs for every
// visible cell on every scroll tick, and constructing/destroying a QWidget
// that often would itself blow the 16ms frame budget this class exists to
// protect (FEATURES.md §E). If PosterCard's aspect/text math ever changes,
// this must change with it.
int posterCardHeight(int posterWidth, PosterCard::Shape shape, const QFont &font)
{
    const qreal aspect = (shape == PosterCard::Shape::Poster) ? 1.5 : (9.0 / 16.0);
    const int artHeight = qRound(posterWidth * aspect);
    const QFontMetrics fm(font);
    const int textHeight = fm.height() * 2 + kTextGap;
    return artHeight + kTextGap + textHeight;
}
} // namespace

PosterGrid::PosterGrid(QWidget *parent)
    : QWidget(parent)
{
}

void PosterGrid::setImageCache(ImageCache *cache)
{
    m_imageCache = cache;
    for (PosterCard *card : std::as_const(m_live))
        card->setImageCache(cache);
    // Pooled cards pick up the current cache in takeCard()/updateVisibleCells()
    // before their next reuse -- no need to touch them while parked.
}

void PosterGrid::setScrollArea(SmoothScrollArea *scrollArea)
{
    if (m_scrollArea == scrollArea)
        return;

    if (m_scrollArea) {
        // Guard against setScrollArea() being called twice: a stale
        // connection to a previous scroll area would double-fire
        // updateVisibleCells() on every scroll of the new one.
        disconnect(m_scrollArea->verticalScrollBar(), nullptr, this, nullptr);
    }

    m_scrollArea = scrollArea;
    if (!m_scrollArea)
        return;

    // Ties the grid's own width to the viewport width. That makes width()
    // trustworthy inside columnCount()/cellRect() without a separate
    // geometry cache, and it means a viewport resize reaches this widget's
    // own resizeEvent() for free -- no need for a dedicated event filter on
    // the viewport to "watch for resize".
    m_scrollArea->setWidgetResizable(true);

    connect(m_scrollArea->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this](int) { updateVisibleCells(); });

    relayout();
}

void PosterGrid::setPosterWidth(int width)
{
    if (m_posterWidth == width)
        return;
    m_posterWidth = width;

    // Live cards need their own size updated immediately -- relayout() below
    // only fixes their *position*, not the fixed size PosterCard pins itself
    // to.
    for (PosterCard *card : std::as_const(m_live))
        card->setPosterWidth(width);

    relayout();
}

void PosterGrid::setShape(PosterCard::Shape shape)
{
    if (m_shape == shape)
        return;
    m_shape = shape;

    // Shape changes which image type/tag a card fetches, not just its aspect
    // ratio, so live cards need a full setItem() re-apply, not just a resize.
    for (auto it = m_live.constBegin(); it != m_live.constEnd(); ++it) {
        const auto itemIt = m_items.constFind(it.key());
        if (itemIt != m_items.constEnd())
            it.value()->setItem(itemIt.value(), m_shape);
    }

    relayout();
}

void PosterGrid::reset(int total)
{
    m_items.clear();

    // Every live card goes back to the pool, not the graveyard -- a fresh
    // sort/filter still wants the same recycled widgets.
    const QList<int> liveIndices = m_live.keys();
    for (int index : liveIndices)
        releaseCard(index);

    m_total = qMax(0, total);
    relayout();
}

void PosterGrid::setItems(int startIndex, const QList<MediaItem> &items)
{
    for (int i = 0; i < items.size(); ++i) {
        const int index = startIndex + i;
        if (index >= m_total)
            break; // a stale/overrunning page for a query that has since shrunk

        m_items.insert(index, items.at(i));
        if (PosterCard *card = m_live.value(index))
            card->setItem(items.at(i), m_shape);
    }

    // Cells that were visible-but-dataless a moment ago may now have data --
    // this is what turns their placeholder into a real card.
    updateVisibleCells();
}

void PosterGrid::updateItem(const MediaItem &item)
{
    for (auto it = m_items.begin(); it != m_items.end(); ++it) {
        if (it.value().id != item.id)
            continue;
        it.value() = item;
        if (PosterCard *card = m_live.value(it.key()))
            card->setItem(item, m_shape);
        return;
    }
}

int PosterGrid::liveCardCount() const
{
    return m_live.size();
}

void PosterGrid::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    const int newColumns = columnCount();
    if (newColumns != m_columns) {
        relayout(); // recomputes m_columns, total height, and the visible band
        return;
    }

    // Column count didn't change but width may have (e.g. a scrollbar
    // appearing/disappearing) -- cell x-positions shift for centering even
    // when nothing else does, so live cards need repositioning.
    updateVisibleCells();
}

void PosterGrid::paintEvent(QPaintEvent *event)
{
    if (m_total <= 0 || m_columns <= 0)
        return;

    const int pitch = rowHeight();
    const int totalRows = (m_total + m_columns - 1) / m_columns;
    const int firstRow = qMax(0, event->rect().top() / pitch);
    const int lastRow = qMin(totalRows - 1, event->rect().bottom() / pitch);
    if (firstRow > lastRow)
        return;

    const int firstIndex = firstRow * m_columns;
    const int lastIndex = qMin(m_total - 1, (lastRow + 1) * m_columns - 1);

    QPainter painter(this);
    // Palette-derived, subtle -- a placeholder for a cell whose page hasn't
    // arrived yet, not a loud "loading" box (DESIGN.md §6: no hardcoded
    // colors, everything from palette()).
    const QColor placeholder = palette().color(QPalette::Midlight);

    for (int index = firstIndex; index <= lastIndex; ++index) {
        if (m_live.contains(index))
            continue; // materialized cards paint themselves
        const QRect cell = cellRect(index);
        if (cell.intersects(event->rect()))
            painter.fillRect(cell, placeholder);
    }
}

void PosterGrid::relayout()
{
    m_columns = columnCount();
    const int rows = m_columns > 0 ? (m_total + m_columns - 1) / m_columns : 0;

    // The grid sizes itself from the TOTAL count immediately, so the scroll
    // area's scrollbar is correct even though almost nothing is materialized
    // yet -- the whole point of this class (FEATURES.md §C1).
    setMinimumHeight(rows * rowHeight());

    updateVisibleCells();
}

void PosterGrid::updateVisibleCells()
{
    if (!m_scrollArea || m_total <= 0 || m_columns <= 0)
        return;

    const int viewportHeight = qMax(1, m_scrollArea->viewport()->height());
    const int scrollY = m_scrollArea->verticalScrollBar()->value();

    // Visible viewport plus one viewport-height of overscan above and below
    // -- roughly 3 screens' worth of band, which is what keeps
    // liveCardCount() bounded regardless of m_total (see class comment).
    const int bandTop = qMax(0, scrollY - kOverscanScreens * viewportHeight);
    const int bandBottom = scrollY + viewportHeight + kOverscanScreens * viewportHeight;

    const int pitch = rowHeight();
    const int totalRows = (m_total + m_columns - 1) / m_columns;
    const int firstRow = qMax(0, bandTop / pitch);
    const int lastRow = qMin(totalRows - 1, bandBottom / pitch);

    QSet<int> stillVisible;
    QSet<int> requestedPagesThisCall; // at most one pageNeeded() per missing page per call

    if (firstRow <= lastRow) {
        const int firstIndex = firstRow * m_columns;
        const int lastIndex = qMin(m_total - 1, (lastRow + 1) * m_columns - 1);

        for (int index = firstIndex; index <= lastIndex; ++index) {
            stillVisible.insert(index);

            if (PosterCard *existing = m_live.value(index)) {
                // Already materialized -- still needs its geometry kept in
                // sync with the current column layout (e.g. after a resize).
                existing->setGeometry(cellRect(index));
                continue;
            }

            const auto itemIt = m_items.constFind(index);
            if (itemIt == m_items.constEnd()) {
                const int pageStart = (index / kPageSize) * kPageSize;
                if (!requestedPagesThisCall.contains(pageStart)) {
                    requestedPagesThisCall.insert(pageStart);
                    emit pageNeeded(pageStart, kPageSize);
                }
                continue; // no data yet -- paintEvent() draws a placeholder instead
            }

            PosterCard *card = takeCard();
            card->setImageCache(m_imageCache);
            card->setPosterWidth(m_posterWidth);
            card->setItem(itemIt.value(), m_shape);
            card->setGeometry(cellRect(index));
            card->show();
            card->loadImage();
            m_live.insert(index, card);
        }
    }

    // Anything still live but outside the band this pass goes back to the
    // pool -- this, not deletion, is what "recycling" means here.
    const QList<int> liveIndices = m_live.keys();
    for (int index : liveIndices) {
        if (!stillVisible.contains(index))
            releaseCard(index);
    }

    update(); // placeholder rectangles may have appeared/moved/gone
}

PosterCard *PosterGrid::takeCard()
{
    if (!m_pool.isEmpty())
        return m_pool.takeLast();

    auto *card = new PosterCard(this);

    // Connected once, here, at creation -- never on reuse, since
    // reconnecting per-recycle would leak a fresh connection every time a
    // card scrolls back into view. Reading item() off the card itself (via
    // the captured `card` pointer) rather than forwarding the signal's own
    // argument is what guarantees a recycled card can never emit the
    // MediaItem it was originally created for.
    connect(card, &PosterCard::activated, this, [this, card]() { emit itemActivated(card->item()); });
    connect(card, &PosterCard::contextMenuRequested, this,
            [this, card](const MediaItem &, const QPoint &globalPos) { emit itemContextMenuRequested(card->item(), globalPos); });

    return card;
}

void PosterGrid::releaseCard(int index)
{
    PosterCard *card = m_live.take(index);
    if (!card)
        return;
    card->releaseImage();
    card->hide();
    m_pool.append(card);
}

QRect PosterGrid::cellRect(int index) const
{
    const int columns = qMax(1, m_columns);
    const int col = index % columns;
    const int row = index / columns;

    const int cardHeight = posterCardHeight(m_posterWidth, m_shape, font());
    const int pitch = cardHeight + kGridSpacing;

    // Centre the columns horizontally when there is leftover width -- a grid
    // jammed against the left edge with a wide gutter on the right looks
    // broken.
    const int contentWidth = columns * m_posterWidth + qMax(0, columns - 1) * kGridSpacing;
    const int leftMargin = qMax(0, (width() - contentWidth) / 2);

    const int x = leftMargin + col * (m_posterWidth + kGridSpacing);
    const int y = row * pitch;
    return QRect(x, y, m_posterWidth, cardHeight);
}

int PosterGrid::columnCount() const
{
    const int availableWidth = m_scrollArea ? qMax(1, m_scrollArea->viewport()->width()) : qMax(1, width());
    const int stride = m_posterWidth + kGridSpacing;
    const int columns = (availableWidth + kGridSpacing) / stride;
    return qMax(1, columns);
}

int PosterGrid::rowHeight() const
{
    return posterCardHeight(m_posterWidth, m_shape, font()) + kGridSpacing;
}
