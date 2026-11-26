#pragma once

#include <QCache>
#include <QImage>
#include <QMutex>
#include <QQuickAsyncImageProvider>
#include <QQuickImageProvider>

class ImageCache;
class TrickplayCache;

// image://jellyfin/<itemId>/<type>/<tag>/<maxWidth>
// Uses ImageCache's low-priority decode pool and NetworkQueue; never blocks
// the QML GUI or SceneGraph threads.
class JellyfinImageProvider : public QQuickAsyncImageProvider
{
public:
    explicit JellyfinImageProvider(ImageCache *imageCache);
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;

private:
    ImageCache *m_imageCache;
};

// image://blurhash/<urlEncodedHash>
// Decodes to a tiny 16x16 QImage (cached in memory, ~1 KB each) and lets the
// GPU upscale smoothly — practically zero CPU/RAM overhead on dual-core CPUs.
class BlurHashImageProvider : public QQuickImageProvider
{
public:
    BlurHashImageProvider();
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    mutable QMutex m_mutex;
    QCache<QString, QImage> m_cache;
};

// image://trickplay/<positionMs>/<revision>
// Serves cropped seekbar hover thumbnails from TrickplayCache.
class TrickplayImageProvider : public QQuickImageProvider
{
public:
    explicit TrickplayImageProvider(TrickplayCache *trickplayCache);
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    TrickplayCache *m_trickplayCache;
};
