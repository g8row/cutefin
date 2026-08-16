#pragma once

#include "api/Models.h"
#include "widgets/PosterCard.h"

#include <QList>
#include <QString>
#include <QWidget>

class AsyncImage;
class ImageCache;
class QLabel;
class QToolButton;
class SmoothScrollArea;

// A titled horizontal strip of PosterCards over a SmoothScrollArea, with
// chevron buttons that fade in on hover when the content overflows.
//
// Poster loading discipline (DESIGN.md §5): only cards intersecting the
// viewport plus one screen of prefetch either side have their images
// requested; cards scrolled further away release their pixmaps back to the
// shared cache. updateVisibleImages() is the single place that decides, and
// is called on scroll, resize, and show.
class MediaRow : public QWidget
{
    Q_OBJECT
public:
    explicit MediaRow(const QString &title, QWidget *parent = nullptr);

    void setImageCache(ImageCache *cache);

    // Replaces the row's contents. An empty list makes the row hide itself
    // (HomePage removes rows that resolve empty).
    void setItems(const QList<MediaItem> &items, PosterCard::Shape shape = PosterCard::Shape::Poster);
    bool isEmpty() const;

    void setTitle(const QString &title);

    // Poster width for every card in the row; the row's height follows.
    void setPosterWidth(int width);

signals:
    void itemActivated(const MediaItem &item);
    void itemContextMenuRequested(const MediaItem &item, const QPoint &globalPos);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updateVisibleImages();
    void updateChevrons();
    void layoutChevrons();
    void scrollByPage(int direction);

    QLabel *m_titleLabel;
    SmoothScrollArea *m_scrollArea;
    QWidget *m_strip;
    QToolButton *m_leftChevron;
    QToolButton *m_rightChevron;

    ImageCache *m_imageCache = nullptr;
    QList<PosterCard *> m_cards;
    PosterCard::Shape m_shape = PosterCard::Shape::Poster;
    int m_posterWidth = 160;
    bool m_hovered = false;
};
