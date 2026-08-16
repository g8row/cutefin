#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

class NetworkQueue;
class QPixmap;

// Two-level image cache: an in-process QPixmapCache (64 MB) backed by an
// on-disk LRU (256 MB, under QStandardPaths::CacheLocation/images). Requests
// are keyed by the tuple that makes a Jellyfin image immutable: the item id,
// the image type, and its tag. Because the tag changes whenever the
// underlying image does, cached entries never need invalidation, only
// eviction for space.
//
// requestImage() is always asynchronous and never touches the disk or the
// network on the calling thread. The callback is bound to `context` and
// therefore never fires after `context` is destroyed.
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

    using Callback = std::function<void(const QPixmap &pixmap)>;

    static constexpr qint64 kMaxDiskCacheBytes = 256LL * 1024 * 1024;
    static constexpr int kMaxMemoryCacheKiB = 64 * 1024;

    explicit ImageCache(NetworkQueue *networkQueue, QObject *parent = nullptr);

    void setServerUrl(const QString &url);
    void setAccessToken(const QString &token);

    // Scans the disk cache directory and evicts the oldest files until it is
    // back under the cap. Runs on a QtConcurrent thread; call once at
    // startup.
    void pruneDiskCacheAsync();

    // `tag` must be non-empty (from ImageTags/BackdropImageTags); requests
    // with an empty tag are ignored since the item has no such image.
    // `maxWidth` should already be display-width * devicePixelRatio, rounded
    // up to the next 100, so the server downscales instead of the client.
    void requestImage(const QString &itemId, ImageType type, const QString &tag, int maxWidth, QObject *context,
                       const Callback &callback);

    static QString imageTypeToString(ImageType type);

private:
    QString cacheKeyFor(const QString &itemId, ImageType type, const QString &tag, int maxWidth) const;
    QString diskPathFor(const QString &cacheKey) const;
    QUrl buildUrl(const QString &itemId, ImageType type, const QString &tag, int maxWidth) const;

    NetworkQueue *m_networkQueue;
    QString m_serverUrl;
    QString m_accessToken;
    QString m_diskCacheDir;
};
