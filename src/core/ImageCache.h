#pragma once

#include "core/NetworkQueue.h"

#include <QCache>
#include <QHash>
#include <QImage>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QThreadPool>
#include <QUrl>

#include <functional>

// Two-level QImage cache designed for low-core-count systems:
//   1. In-process QCache<QString, QImage> (64 MiB byte-budgeted, mutex-guarded)
//   2. On-disk LRU (256 MiB under QStandardPaths::CacheLocation/images)
//
// All disk reads, disk writes, and compressed image decodes (QImage::fromData)
// run on a dedicated low-priority QThreadPool capped at <= 2 threads so poster
// decoding can never starve the GUI/SceneGraph thread or libmpv on dual-core
// CPUs. Concurrent requests for the same image share one disk lookup and one
// download.
//
// Apart from memoryCachedImage(), members are to be called on the thread that
// owns the cache, and all callbacks are invoked there.
class ImageCache : public QObject
{
    Q_OBJECT
public:
    enum class ImageType {
        Primary,
        Backdrop,
        Thumb,
        Logo,
    };

    using Callback = std::function<void(const QImage &image)>;
    using CancelToken = NetworkQueue::CancelToken;

    static constexpr qint64 kMaxDiskCacheBytes = 256LL * 1024 * 1024;
    static constexpr qint64 kMaxMemoryCacheBytes = 64LL * 1024 * 1024;

    explicit ImageCache(NetworkQueue *networkQueue, QObject *parent = nullptr);
    ~ImageCache() override;

    void setServerUrl(const QString &url);
    void setAccessToken(const QString &token);
    void setMemoryLimitBytes(qint64 bytes);

    // Scans the disk cache directory and evicts the least recently used files
    // until it is back under the cap. Runs on the low-priority worker pool.
    void pruneDiskCacheAsync();

    // Clears both the in-memory and on-disk caches. The synchronous form does
    // disk I/O on the calling thread; GUI code should use the async one.
    void clearCache();
    void clearCacheAsync(QObject *context, const std::function<void()> &done = {});

    // Approximate disk cache usage in bytes (fast directory scan). The same
    // threading advice applies.
    qint64 diskCacheSizeBytes() const;
    void diskCacheSizeBytesAsync(QObject *context, const std::function<void(qint64)> &callback);

    // `tag` must be non-empty (from ImageTags/BackdropImageTags).
    // `maxWidth` is bucketed to 100px steps so the server downscales instead of
    // sending full-resolution artwork over the wire.
    // Callback is always invoked on the Qt GUI/owner thread, guarded by `context`.
    // It is not invoked once `cancelToken` (optional) has been set; follow that
    // with discardCancelledRequests() to also drop work nobody is waiting for.
    void requestImage(const QString &itemId, ImageType type, const QString &tag, int maxWidth, QObject *context,
                      const Callback &callback, const CancelToken &cancelToken = {});

    // Abandons queued loads whose every requester has cancelled or been
    // destroyed. Downloads already on the wire finish and are cached.
    void discardCancelledRequests();

    // Synchronous memory-only lookup (thread-safe). Returns null QImage on miss.
    QImage memoryCachedImage(const QString &itemId, ImageType type, const QString &tag, int maxWidth) const;

    static QString imageTypeToString(ImageType type);
    static ImageType imageTypeFromString(const QString &str, bool *ok = nullptr);
    static int bucketWidth(int width);

private:
    // One caller waiting for an image.
    struct PendingConsumer {
        bool hasContext = false;
        QPointer<QObject> context;
        CancelToken cancelToken;
        Callback callback;
        QMetaObject::Connection contextWatch;

        bool isActive() const;
    };

    // The shared load behind every consumer of one cache key.
    struct PendingRequest {
        quint64 id = 0;           // tells a load apart from an abandoned predecessor
        CancelToken abandonToken; // read by the disk worker and by NetworkQueue
        bool onWire = false;
        QList<PendingConsumer> consumers;
    };

    QString cacheKeyFor(const QString &itemId, ImageType type, const QString &tag, int maxWidth) const;
    QString diskPathFor(const QString &cacheKey) const;
    QUrl buildUrl(const QString &itemId, ImageType type, const QString &tag, int maxWidth) const;
    void insertMemory(const QString &key, const QImage &image);
    void clearMemory();
    void deliverLater(const PendingConsumer &consumer, const QImage &image);
    void startDiskLookup(const QString &key, quint64 id, const CancelToken &abandonToken, const QUrl &url);
    void fetchFromNetwork(const QString &key, quint64 id, const QUrl &url);
    void handleReply(const QString &key, quint64 id, QNetworkReply *reply);
    void finishRequest(const QString &key, quint64 id, const QImage &image);
    void discardIfAbandoned(const QString &key);

    NetworkQueue *m_networkQueue;
    QString m_serverUrl;
    QString m_accessToken;
    QString m_diskCacheDir;

    mutable QMutex m_cacheMutex;
    QCache<QString, QImage> m_memoryCache;
    QHash<QString, PendingRequest> m_inFlight;
    quint64 m_lastRequestId = 0;
    QThreadPool m_decodePool; // keep last: its tasks use the members above
};
