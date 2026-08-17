#pragma once

#include "api/Models.h"
#include "widgets/PosterCard.h"

#include <QHash>
#include <QList>
#include <QVector>
#include <QWidget>

class ImageCache;
class SmoothScrollArea;

// A virtualized grid of PosterCards for library browsing.
//
// The whole point of this class is that a 303-item library must not mean 303
// live widgets (FEATURES.md §C1/§E). It knows the *total* result count up
// front (from TotalRecordCount), sizes itself to the full scroll height that
// implies, and materializes cards only for the rows intersecting the viewport
// plus one screen of overscan either side. Cards outside that band are
// returned to a recycling pool and reused for rows that scroll in, so the
// live widget count stays proportional to the viewport, never to the library.
//
// Data arrives one page at a time: when the visible band covers cells with no
// MediaItem yet, the grid emits pageNeeded() and paints those cells as empty
// placeholders until LibraryPage supplies them via setItems().
class PosterGrid : public QWidget
{
    Q_OBJECT
public:
    explicit PosterGrid(QWidget *parent = nullptr);

    void setImageCache(ImageCache *cache);

    // The scroll area this grid lives inside. Required: the grid drives its
    // materialization off that viewport's geometry and scroll position.
    void setScrollArea(SmoothScrollArea *scrollArea);

    void setPosterWidth(int width);
    void setShape(PosterCard::Shape shape);

    // Resets to an empty result set of `total` items, discarding all loaded
    // data (a new sort/filter). Call before the first setItems() of a query.
    void reset(int total);

    // Supplies a contiguous page of results starting at `startIndex`.
    void setItems(int startIndex, const QList<MediaItem> &items);

    // Replaces one already-loaded item in place (e.g. after a played or
    // favourite toggle) without disturbing scroll position or refetching.
    void updateItem(const MediaItem &item);

    int totalCount() const { return m_total; }
    int loadedCount() const { return m_items.size(); }

    // How many live PosterCard widgets currently exist. Exposed so a test or
    // the stats overlay can assert the recycling actually bounds it.
    int liveCardCount() const;

signals:
    void itemActivated(const MediaItem &item);
    void itemContextMenuRequested(const MediaItem &item, const QPoint &globalPos);

    // The visible band needs data the grid does not have. `startIndex` is
    // page-aligned; LibraryPage is responsible for de-duplicating requests
    // for pages already in flight.
    void pageNeeded(int startIndex, int count);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void relayout();          // recompute columns/rows and the grid's own height
    void updateVisibleCells(); // materialize/recycle cards for the visible band
    PosterCard *takeCard();    // from the pool, or a new one
    void releaseCard(int index);
    QRect cellRect(int index) const;
    int columnCount() const;
    int rowHeight() const;

    ImageCache *m_imageCache = nullptr;
    SmoothScrollArea *m_scrollArea = nullptr;

    int m_total = 0;
    QHash<int, MediaItem> m_items;      // index -> item, for pages received so far
    QHash<int, PosterCard *> m_live;    // index -> materialized card
    QVector<PosterCard *> m_pool;       // recycled, hidden, ready for reuse

    int m_posterWidth = 160;
    PosterCard::Shape m_shape = PosterCard::Shape::Poster;
    int m_columns = 1;
};
