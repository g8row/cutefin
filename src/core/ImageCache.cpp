#include "ImageCache.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QUrlQuery>

#include <utility>

namespace {

// Runs user-initiated cache maintenance ahead of queued decodes.
constexpr int kMaintenancePriority = 1;

// Returns a null image on a miss. A hit refreshes the file's modification time,
// which is what pruning orders by; an undecodable file is deleted.
QImage loadFromDisk(const QString &path)
{
    QFile file(path);
    // Changing the file time needs a writable handle on some platforms.
    if (!file.open(QIODevice::ReadWrite | QIODevice::ExistingOnly) && !file.open(QIODevice::ReadOnly))
        return QImage();

    QImage image;
    image.loadFromData(file.readAll());
    if (image.isNull()) {
        file.close();
        file.remove();
        return image;
    }
    file.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileModificationTime);
    return image;
}

// Readers only ever see a complete file: the data goes to a temporary file that
// replaces `path` on commit. Failure just means the image is not cached.
void writeToDisk(const QString &path, const QByteArray &data)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    if (file.write(data) != data.size())
        file.cancelWriting();
    file.commit();
}

void removeFiles(const QString &dir)
{
    const QFileInfoList files = QDir(dir).entryInfoList(QDir::Files);
    for (const QFileInfo &fi : files)
        QFile::remove(fi.absoluteFilePath());
}

qint64 totalFileSize(const QString &dir)
{
    qint64 total = 0;
    const QFileInfoList files = QDir(dir).entryInfoList(QDir::Files);
    for (const QFileInfo &fi : files)
        total += fi.size();
    return total;
}

} // namespace

bool ImageCache::PendingConsumer::isActive() const
{
    if (hasContext && context.isNull())
        return false;
    return !cancelToken || !cancelToken->load();
}

ImageCache::ImageCache(NetworkQueue *networkQueue, QObject *parent)
    : QObject(parent)
    , m_networkQueue(networkQueue)
{
    m_memoryCache.setMaxCost(static_cast<qsizetype>(kMaxMemoryCacheBytes / 1024));

    // Cap decode workers to <= 2 low-priority threads so dual-core systems stay
    // responsive while scrolling large libraries or playing video.
    const int threads = qBound(1, QThread::idealThreadCount(), 2);
    m_decodePool.setMaxThreadCount(threads);
    m_decodePool.setThreadPriority(QThread::LowPriority);

    m_diskCacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/images");
    QDir().mkpath(m_diskCacheDir);
}

ImageCache::~ImageCache()
{
    for (const PendingRequest &pending : std::as_const(m_inFlight)) {
        pending.abandonToken->store(true);
        for (const PendingConsumer &consumer : pending.consumers)
            disconnect(consumer.contextWatch);
    }
    m_inFlight.clear();

    // Running tasks use `this`, so wait for them without a timeout.
    m_decodePool.clear();
    m_decodePool.waitForDone();
}

void ImageCache::setServerUrl(const QString &url)
{
    m_serverUrl = url;
}

void ImageCache::setAccessToken(const QString &token)
{
    m_accessToken = token;
}

void ImageCache::setMemoryLimitBytes(qint64 bytes)
{
    QMutexLocker locker(&m_cacheMutex);
    const qint64 kib = qMax<qint64>(4096, bytes / 1024);
    m_memoryCache.setMaxCost(static_cast<qsizetype>(kib));
}

QString ImageCache::imageTypeToString(ImageType type)
{
    switch (type) {
    case ImageType::Primary:
        return QStringLiteral("Primary");
    case ImageType::Backdrop:
        return QStringLiteral("Backdrop");
    case ImageType::Thumb:
        return QStringLiteral("Thumb");
    case ImageType::Logo:
        return QStringLiteral("Logo");
    }
    return QString();
}

ImageCache::ImageType ImageCache::imageTypeFromString(const QString &str, bool *ok)
{
    if (ok)
        *ok = true;
    if (str.compare(QLatin1String("Primary"), Qt::CaseInsensitive) == 0)
        return ImageType::Primary;
    if (str.compare(QLatin1String("Backdrop"), Qt::CaseInsensitive) == 0)
        return ImageType::Backdrop;
    if (str.compare(QLatin1String("Thumb"), Qt::CaseInsensitive) == 0)
        return ImageType::Thumb;
    if (str.compare(QLatin1String("Logo"), Qt::CaseInsensitive) == 0)
        return ImageType::Logo;
    if (ok)
        *ok = false;
    return ImageType::Primary;
}

int ImageCache::bucketWidth(int width)
{
    if (width <= 0)
        return 300;
    const int rounded = ((width + 99) / 100) * 100;
    return qBound(200, rounded, 1920);
}

QString ImageCache::cacheKeyFor(const QString &itemId, ImageType type, const QString &tag, int maxWidth) const
{
    return itemId + QLatin1Char('|') + imageTypeToString(type) + QLatin1Char('|') + tag + QLatin1Char('|')
        + QString::number(bucketWidth(maxWidth));
}

QString ImageCache::diskPathFor(const QString &cacheKey) const
{
    const QByteArray hash = QCryptographicHash::hash(cacheKey.toUtf8(), QCryptographicHash::Sha1).toHex();
    return m_diskCacheDir + QLatin1Char('/') + QString::fromLatin1(hash);
}

QUrl ImageCache::buildUrl(const QString &itemId, ImageType type, const QString &tag, int maxWidth) const
{
    QUrl url(m_serverUrl);
    url.setPath(url.path() + QStringLiteral("/Items/%1/Images/%2").arg(itemId, imageTypeToString(type)));

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("tag"), tag);
    query.addQueryItem(QStringLiteral("maxWidth"), QString::number(bucketWidth(maxWidth)));
    query.addQueryItem(QStringLiteral("quality"), QStringLiteral("88"));
    if (!m_accessToken.isEmpty())
        query.addQueryItem(QStringLiteral("api_key"), m_accessToken);
    url.setQuery(query);
    return url;
}

void ImageCache::insertMemory(const QString &key, const QImage &image)
{
    if (image.isNull())
        return;
    const qsizetype costKiB = qMax<qsizetype>(1, static_cast<qsizetype>(image.sizeInBytes() / 1024));
    QMutexLocker locker(&m_cacheMutex);
    m_memoryCache.insert(key, new QImage(image), costKiB);
}

QImage ImageCache::memoryCachedImage(const QString &itemId, ImageType type, const QString &tag, int maxWidth) const
{
    if (tag.isEmpty())
        return QImage();
    const QString key = cacheKeyFor(itemId, type, tag, maxWidth);
    QMutexLocker locker(&m_cacheMutex);
    if (const QImage *hit = m_memoryCache.object(key))
        return *hit;
    return QImage();
}

void ImageCache::pruneDiskCacheAsync()
{
    const QString dir = m_diskCacheDir;
    m_decodePool.start([dir]() {
        QDir cacheDir(dir);
        QFileInfoList files = cacheDir.entryInfoList(QDir::Files, QDir::Time); // most recently used first
        qint64 total = 0;
        for (const QFileInfo &fi : std::as_const(files))
            total += fi.size();

        while (total > ImageCache::kMaxDiskCacheBytes && !files.isEmpty()) {
            const QFileInfo oldest = files.takeLast();
            total -= oldest.size();
            QFile::remove(oldest.absoluteFilePath());
        }
    });
}

void ImageCache::clearMemory()
{
    QMutexLocker locker(&m_cacheMutex);
    m_memoryCache.clear();
}

void ImageCache::clearCache()
{
    clearMemory();
    removeFiles(m_diskCacheDir);
}

void ImageCache::clearCacheAsync(QObject *context, const std::function<void()> &done)
{
    clearMemory();

    const QString dir = m_diskCacheDir;
    const bool hasContext = context != nullptr;
    const QPointer<QObject> guard(context);
    m_decodePool.start(
        [this, dir, hasContext, guard, done]() {
            removeFiles(dir);
            if (!done)
                return;
            QMetaObject::invokeMethod(
                this,
                [hasContext, guard, done]() {
                    if (hasContext && guard.isNull())
                        return;
                    done();
                },
                Qt::QueuedConnection);
        },
        kMaintenancePriority);
}

qint64 ImageCache::diskCacheSizeBytes() const
{
    return totalFileSize(m_diskCacheDir);
}

void ImageCache::diskCacheSizeBytesAsync(QObject *context, const std::function<void(qint64)> &callback)
{
    if (!callback)
        return;

    const QString dir = m_diskCacheDir;
    const bool hasContext = context != nullptr;
    const QPointer<QObject> guard(context);
    m_decodePool.start(
        [this, dir, hasContext, guard, callback]() {
            const qint64 bytes = totalFileSize(dir);
            QMetaObject::invokeMethod(
                this,
                [hasContext, guard, callback, bytes]() {
                    if (hasContext && guard.isNull())
                        return;
                    callback(bytes);
                },
                Qt::QueuedConnection);
        },
        kMaintenancePriority);
}

void ImageCache::requestImage(const QString &itemId, ImageType type, const QString &tag, int maxWidth,
                              QObject *context, const Callback &callback, const CancelToken &cancelToken)
{
    if (!callback)
        return;

    PendingConsumer consumer;
    consumer.hasContext = context != nullptr;
    consumer.context = context;
    consumer.cancelToken = cancelToken;
    consumer.callback = callback;
    if (!consumer.isActive())
        return;

    if (tag.isEmpty()) {
        deliverLater(consumer, QImage());
        return;
    }

    const int width = bucketWidth(maxWidth);
    const QString key = cacheKeyFor(itemId, type, tag, width);

    const QImage memoryHit = memoryCachedImage(itemId, type, tag, width);
    if (!memoryHit.isNull()) {
        deliverLater(consumer, memoryHit);
        return;
    }

    if (context) {
        consumer.contextWatch =
            connect(context, &QObject::destroyed, this, [this, key]() { discardIfAbandoned(key); });
    }

    const auto existing = m_inFlight.find(key);
    if (existing != m_inFlight.end()) {
        existing->consumers.append(consumer);
        return;
    }

    PendingRequest pending;
    pending.id = ++m_lastRequestId;
    pending.abandonToken = std::make_shared<std::atomic_bool>(false);
    pending.consumers.append(consumer);
    m_inFlight.insert(key, pending);

    const QUrl url = m_serverUrl.isEmpty() ? QUrl() : buildUrl(itemId, type, tag, width);
    startDiskLookup(key, pending.id, pending.abandonToken, url);
}

void ImageCache::deliverLater(const PendingConsumer &consumer, const QImage &image)
{
    QMetaObject::invokeMethod(
        this,
        [consumer, image]() {
            if (consumer.isActive())
                consumer.callback(image);
        },
        Qt::QueuedConnection);
}

void ImageCache::startDiskLookup(const QString &key, quint64 id, const CancelToken &abandonToken, const QUrl &url)
{
    const QString path = diskPathFor(key);
    m_decodePool.start([this, key, id, abandonToken, url, path]() {
        if (abandonToken->load())
            return;

        const QImage image = loadFromDisk(path);
        insertMemory(key, image);
        QMetaObject::invokeMethod(
            this,
            [this, key, id, url, image]() {
                if (image.isNull())
                    fetchFromNetwork(key, id, url);
                else
                    finishRequest(key, id, image);
            },
            Qt::QueuedConnection);
    });
}

void ImageCache::fetchFromNetwork(const QString &key, quint64 id, const QUrl &url)
{
    discardIfAbandoned(key);
    const auto pending = m_inFlight.constFind(key);
    if (pending == m_inFlight.constEnd() || pending->id != id)
        return;

    if (url.isEmpty() || !url.isValid()) {
        finishRequest(key, id, QImage());
        return;
    }

    // Copied: get() may call back into m_inFlight before it returns. The cache
    // itself is the context, as the download is shared between consumers.
    const CancelToken abandonToken = pending->abandonToken;
    m_networkQueue->get(
        QNetworkRequest(url), NetworkQueue::Priority::Image, this,
        [this, key, id](QNetworkReply *reply) { handleReply(key, id, reply); }, abandonToken,
        [this, key, id]() {
            const auto it = m_inFlight.find(key);
            if (it != m_inFlight.end() && it->id == id)
                it->onWire = true;
        });
}

void ImageCache::handleReply(const QString &key, quint64 id, QNetworkReply *reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        finishRequest(key, id, QImage());
        return;
    }

    const QByteArray data = reply->readAll();
    const QString path = diskPathFor(key);
    // Decode compressed bytes and write the disk cache off the GUI thread.
    m_decodePool.start([this, key, id, path, data]() {
        QImage image;
        image.loadFromData(data);
        if (!image.isNull()) {
            insertMemory(key, image);
            writeToDisk(path, data);
        }
        QMetaObject::invokeMethod(
            this, [this, key, id, image]() { finishRequest(key, id, image); }, Qt::QueuedConnection);
    });
}

void ImageCache::finishRequest(const QString &key, quint64 id, const QImage &image)
{
    const auto it = m_inFlight.find(key);
    if (it == m_inFlight.end() || it->id != id)
        return;

    // Removed before calling out, so a callback may request the image again.
    const QList<PendingConsumer> consumers = it->consumers;
    m_inFlight.erase(it);

    for (const PendingConsumer &consumer : consumers)
        disconnect(consumer.contextWatch);
    for (const PendingConsumer &consumer : consumers) {
        // Checked one by one: an earlier callback may cancel a later consumer.
        if (consumer.isActive())
            consumer.callback(image);
    }
}

void ImageCache::discardCancelledRequests()
{
    const QList<QString> keys = m_inFlight.keys();
    for (const QString &key : keys)
        discardIfAbandoned(key);
}

void ImageCache::discardIfAbandoned(const QString &key)
{
    const auto it = m_inFlight.find(key);
    if (it == m_inFlight.end())
        return;

    for (qsizetype i = it->consumers.size() - 1; i >= 0; --i) {
        if (!it->consumers.at(i).isActive()) {
            disconnect(it->consumers.at(i).contextWatch);
            it->consumers.removeAt(i);
        }
    }

    // A download on the wire stays registered so that a new request for the
    // same image joins it instead of starting another.
    if (it->consumers.isEmpty() && !it->onWire) {
        it->abandonToken->store(true);
        m_inFlight.erase(it);
    }
}
