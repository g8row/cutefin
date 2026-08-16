#pragma once

#include "core/ImageCache.h"

#include <QPixmap>
#include <QString>
#include <QWidget>

class QVariantAnimation;

// QLabel-like widget: paints a BlurHash placeholder immediately, requests the
// real image through ImageCache, and cross-fades it in over 180ms on arrival
// (DESIGN.md §5). Cancels its in-flight request on destruction (the ImageCache
// callback is bound to `this` as context) and guards against stale replies
// when re-targeted at a different item with setSource() -- ImageCache does
// not itself cancel a request just because the same widget asked for a
// different image, so a per-widget generation counter is required here.
class AsyncImage : public QWidget
{
    Q_OBJECT
public:
    explicit AsyncImage(QWidget *parent = nullptr);

    void setImageCache(ImageCache *cache);

    // Sets what this widget should display. Always (re)shows the BlurHash
    // placeholder immediately. If `autoLoad` is false, the real image is not
    // fetched until load() is called explicitly -- used by PosterCard /
    // MediaRow for viewport-lazy loading.
    void setSource(const QString &itemId, ImageCache::ImageType type, const QString &tag, const QString &blurHash,
                    bool autoLoad = true);

    // Starts (or re-starts) the network/disk fetch for the current source.
    // Idempotent: a no-op if already loaded or already in flight.
    void load();

    // Drops the decoded real pixmap (keeps the BlurHash placeholder showing)
    // so a card scrolled far out of view releases memory back to the shared
    // caches; a later load() re-fetches, which is normally a fast cache hit.
    void release();

    // Full reset: clears source, placeholder and pixmap, cancels any pending
    // request by bumping the generation counter.
    void clear();

    bool hasRealImage() const { return m_hasRealImage; }

    QSize sizeHint() const override;

signals:
    // Emitted whenever this widget's content changed and needs repainting.
    // A host that renders an AsyncImage itself rather than letting Qt
    // composite it as a child (PosterCard does, so it can draw overlays on
    // top and apply a hover transform) gets no repaint from update() while
    // this widget is hidden -- it connects here instead. Without it the only
    // way to see the 180ms cross-fade is to poll on a timer, which at one
    // timer per on-screen card is exactly the per-frame cost FEATURES.md §E
    // forbids.
    void repaintNeeded();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    // update() plus repaintNeeded(), so a manual renderer stays in sync.
    void notifyRepaint();
    void rebuildPlaceholder();
    int computeMaxWidth() const;
    void beginCrossFade(const QPixmap &incoming);

    ImageCache *m_cache = nullptr;

    QString m_itemId;
    ImageCache::ImageType m_type = ImageCache::ImageType::Primary;
    QString m_tag;
    QString m_blurHash;

    QPixmap m_placeholder;
    QPixmap m_currentPixmap; // what's fully shown right now (placeholder or a previously-arrived real image)
    QPixmap m_incomingPixmap; // being cross-faded in
    bool m_hasRealImage = false;
    bool m_loading = false;

    quint64 m_generation = 0;

    QVariantAnimation *m_fadeAnimation;
};
