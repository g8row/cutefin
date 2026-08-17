#pragma once

#include "api/Models.h"
#include "core/ImageCache.h"

#include <QSize>
#include <QWidget>

class AsyncImage;
class QVariantAnimation;

// A single browsable tile: an AsyncImage poster, a title line, a subtitle
// line, and (when partially watched) a progress strip across the bottom of
// the artwork.
//
// Hover/focus animate a single `qreal hoverProgress` 0->1 over 140ms
// (OutCubic) which paintEvent() lerps into a 1.0->1.03 scale and a focus ring
// in palette().highlight() -- DESIGN.md §5. Nothing here animates geometry or
// triggers a relayout, so a row of these stays at 60fps while scrolling.
class PosterCard : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal hoverProgress READ hoverProgress WRITE setHoverProgress)
public:
    // Which artwork shape the card draws. Poster is 2:3 (movies, series),
    // Thumb is 16:9 (episodes, library tiles).
    enum class Shape {
        Poster,
        Thumb,
    };

    // Left/Right/Home/End step focus to the previous/next/first/last card.
    // PosterCard has no notion of an ordering of its own -- only whoever owns
    // that ordering (MediaRow) connects to focusStepRequested(), and this
    // enum is public so that connection can be written without magic ints.
    enum class FocusDirection {
        Previous,
        Next,
        First,
        Last,
    };

    explicit PosterCard(QWidget *parent = nullptr);

    void setImageCache(ImageCache *cache);

    // Populates the card from an item. Picks the image type/tag/blurhash
    // appropriate for `shape` and the item's own type, sets title/subtitle,
    // and computes the progress strip from item.userData. Does not start the
    // image fetch -- MediaRow/LibraryPage drive that via loadImage() so only
    // cards near the viewport hit the network.
    void setItem(const MediaItem &item, Shape shape = Shape::Poster);
    const MediaItem &item() const { return m_item; }

    // Viewport-lazy loading hooks used by the owning row/grid.
    void loadImage();
    void releaseImage();

    // Fires activated() as if the card had been clicked. Public so the
    // accessibility "press" action can drive it the same way a mouse or
    // Return key does, rather than reimplementing the emit.
    void activate();

    // Overrides the automatic title/subtitle derived from the item.
    void setTitleText(const QString &title);
    void setSubtitleText(const QString &subtitle);

    // Artwork width in logical pixels; the card sizes its height from the
    // shape's aspect ratio plus room for the two text lines.
    void setPosterWidth(int width);
    int posterWidth() const { return m_posterWidth; }

    qreal hoverProgress() const { return m_hoverProgress; }
    void setHoverProgress(qreal progress);

    QSize sizeHint() const override;

signals:
    void activated(const MediaItem &item);
    // Right-click / context-menu key. `globalPos` is where to pop the menu.
    void contextMenuRequested(const MediaItem &item, const QPoint &globalPos);
    // Arrow/Home/End pressed while focused. Only emitted when something is
    // actually listening (see keyPressEvent()) so a PosterCard sitting in
    // PosterGrid, which never connects to this, behaves exactly as before --
    // the key event falls through to QWidget's default handling instead of
    // being silently swallowed.
    void focusStepRequested(PosterCard::FocusDirection direction);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void animateHoverTo(qreal target);
    void applyFixedSize();
    void relayoutChildren();
    QRect artworkRect() const;
    // Pushes title/subtitle/watched-state into accessibleName/
    // accessibleDescription and the tooltip. Called once per item
    // population, never from paintEvent -- FEATURES.md §E's per-frame budget
    // doesn't apply here, but there is no reason to redo it every repaint
    // either.
    void updateAccessibility();

    MediaItem m_item;
    Shape m_shape = Shape::Poster;
    int m_posterWidth = 160;

    AsyncImage *m_image;
    QString m_title;
    QString m_subtitle;
    double m_progressFraction = 0.0; // 0 => no progress strip
    bool m_played = false;

    qreal m_hoverProgress = 0.0;
    QVariantAnimation *m_hoverAnimation;
};
