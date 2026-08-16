#include "ImageCache.h"

#include "NetworkQueue.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPixmapCache>
#include <QPointer>
#include <QStandardPaths>
#include <QUrlQuery>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <utility>

ImageCache::ImageCache(NetworkQueue *networkQueue, QObject *parent)
    : QObject(parent)
    , m_networkQueue(networkQueue)
{
    QPixmapCache::setCacheLimit(kMaxMemoryCacheKiB);

    m_diskCacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/images");
    QDir().mkpath(m_diskCacheDir);
}

void ImageCache::setServerUrl(const QString &url)
{
    m_serverUrl = url;
}

void ImageCache::setAccessToken(const QString &token)
{
    m_accessToken = token;
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

QString ImageCache::cacheKeyFor(const QString &itemId, ImageType type, const QString &tag, int maxWidth) const
{
    return itemId + QLatin1Char('|') + imageTypeToString(type) + QLatin1Char('|') + tag + QLatin1Char('|')
        + QString::number(maxWidth);
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
    query.addQueryItem(QStringLiteral("maxWidth"), QString::number(maxWidth));
    query.addQueryItem(QStringLiteral("quality"), QStringLiteral("90"));
    if (!m_accessToken.isEmpty())
        query.addQueryItem(QStringLiteral("api_key"), m_accessToken);
    url.setQuery(query);
    return url;
}

void ImageCache::pruneDiskCacheAsync()
{
    const QString dir = m_diskCacheDir;
    (void)QtConcurrent::run([dir]() {
        QDir cacheDir(dir);
        QFileInfoList files = cacheDir.entryInfoList(QDir::Files, QDir::Time); // newest first
        qint64 total = 0;
        for (const QFileInfo &fi : std::as_const(files))
            total += fi.size();

        while (total > ImageCache::kMaxDiskCacheBytes && !files.isEmpty()) {
            const QFileInfo oldest = files.takeLast(); // oldest is last with Time sort (newest first)
            total -= oldest.size();
            QFile::remove(oldest.absoluteFilePath());
        }
    });
}

void ImageCache::requestImage(const QString &itemId, ImageType type, const QString &tag, int maxWidth,
                               QObject *context, const Callback &callback)
{
    if (tag.isEmpty() || !callback)
        return;

    const QString key = cacheKeyFor(itemId, type, tag, maxWidth);

    QPixmap memoryHit;
    if (QPixmapCache::find(key, &memoryHit)) {
        QPointer<QObject> guard(context);
        QObject *receiver = context ? context : this;
        QMetaObject::invokeMethod(
            receiver,
            [guard, context, callback, memoryHit]() {
                if (context && guard.isNull())
                    return;
                callback(memoryHit);
            },
            Qt::QueuedConnection);
        return;
    }

    const QString path = diskPathFor(key);
    QPointer<QObject> guard(context);

    auto *watcher = new QFutureWatcher<QPixmap>(this);
    connect(watcher, &QFutureWatcher<QPixmap>::finished, this, [=]() {
        const QPixmap diskHit = watcher->result();
        watcher->deleteLater();

        if (context && guard.isNull())
            return;

        if (!diskHit.isNull()) {
            QPixmapCache::insert(key, diskHit);
            callback(diskHit);
            return;
        }

        // Not cached anywhere: fetch it over the network at Image priority.
        const QUrl url = buildUrl(itemId, type, tag, maxWidth);
        QNetworkRequest request(url);
        m_networkQueue->get(request, NetworkQueue::Priority::Image, context, [key, path, callback](QNetworkReply *reply) {
            // Note: NetworkQueue owns the reply's lifetime (deleteLater on completion).
            if (reply->error() != QNetworkReply::NoError)
                return;

            const QByteArray data = reply->readAll();
            QPixmap pixmap;
            if (!pixmap.loadFromData(data))
                return;

            QPixmapCache::insert(key, pixmap);
            (void)QtConcurrent::run([path, data]() {
                QFile file(path);
                if (file.open(QIODevice::WriteOnly))
                    file.write(data);
            });
            callback(pixmap);
        });
    });
    watcher->setFuture(QtConcurrent::run([path]() {
        QPixmap pixmap;
        pixmap.load(path);
        return pixmap;
    }));
}
