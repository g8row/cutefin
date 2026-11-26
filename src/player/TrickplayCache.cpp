#include "player/TrickplayCache.h"

#include "api/JellyfinClient.h"
#include "core/NetworkQueue.h"

#include <QMetaObject>
#include <QMutexLocker>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QThread>

namespace {

bool sameInfo(const TrickplayInfo &a, const TrickplayInfo &b)
{
    return a.width == b.width && a.height == b.height && a.tileWidth == b.tileWidth && a.tileHeight == b.tileHeight
        && a.intervalMs == b.intervalMs && a.thumbnailCount == b.thumbnailCount;
}

// Index maths per Models.h's TrickplayInfo comment.
QRect cropRectFor(const TrickplayInfo &info, int thumbnailIndex)
{
    const int row = (thumbnailIndex / info.tileWidth) % info.tileHeight;
    const int col = thumbnailIndex % info.tileWidth;
    return QRect(col * info.width, row * info.height, info.width, info.height);
}

} // namespace

TrickplayCache::TrickplayCache(JellyfinClient *client, NetworkQueue *networkQueue, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_networkQueue(networkQueue)
{
    // One low-priority decoder thread: a sheet is a single ~800 KB JPEG and
    // this must never compete with playback on a dual-core machine.
    m_decodePool.setMaxThreadCount(1);
    m_decodePool.setThreadPriority(QThread::LowPriority);
}

TrickplayCache::~TrickplayCache()
{
    // Decode tasks capture `this`; make sure none is running or pending
    // before members go away. Already-posted completion callbacks are
    // dropped by ~QObject.
    m_decodePool.clear();
    m_decodePool.waitForDone();
}

void TrickplayCache::setSource(const QString &itemId, const TrickplayInfo &info)
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_itemId == itemId && sameInfo(m_info, info))
            return;
    }

    clear();

    QMutexLocker locker(&m_mutex);
    m_itemId = itemId;
    m_info = info;
}

void TrickplayCache::setDualCoreMode(bool enabled)
{
    QMutexLocker locker(&m_mutex);
    m_maxSheets = enabled ? kDualCoreMaxSheets : kDefaultMaxSheets;
    evictIfNeededLocked();
}

int TrickplayCache::maxSheets() const
{
    QMutexLocker locker(&m_mutex);
    return m_maxSheets;
}

bool TrickplayCache::isEnabled() const
{
    QMutexLocker locker(&m_mutex);
    return !m_itemId.isEmpty() && m_info.isValid();
}

TrickplayInfo TrickplayCache::info() const
{
    QMutexLocker locker(&m_mutex);
    return m_info;
}

QImage TrickplayCache::thumbnailIfCached(qint64 positionMs) const
{
    QMutexLocker locker(&m_mutex);
    if (m_itemId.isEmpty() || !m_info.isValid() || positionMs < 0)
        return QImage();

    const int index = thumbnailIndexFor(positionMs);
    if (index < 0 || (m_info.thumbnailCount > 0 && index >= m_info.thumbnailCount))
        return QImage();

    const auto it = m_sheets.constFind(sheetIndexFor(positionMs));
    if (it == m_sheets.constEnd())
        return QImage();
    return it->copy(cropRectFor(m_info, index));
}

QImage TrickplayCache::thumbnailAt(qint64 positionMs)
{
    int sheet = -1;
    {
        QMutexLocker locker(&m_mutex);
        if (m_itemId.isEmpty() || !m_info.isValid() || positionMs < 0)
            return QImage();

        const int index = thumbnailIndexFor(positionMs);
        if (index < 0 || (m_info.thumbnailCount > 0 && index >= m_info.thumbnailCount))
            return QImage();

        sheet = sheetIndexFor(positionMs);
        const auto it = m_sheets.constFind(sheet);
        if (it != m_sheets.constEnd()) {
            touchLocked(sheet);
            return it->copy(cropRectFor(m_info, index));
        }
    }

    requestSheet(sheet, positionMs);
    return QImage();
}

void TrickplayCache::clear()
{
    QMutexLocker locker(&m_mutex);
    m_sheets.clear();
    m_lru.clear();
    // Replies/decodes still in flight re-check the item id when they land
    // and discard themselves once the cache has moved on.
    m_inFlight.clear();
    m_itemId.clear();
    m_info = TrickplayInfo();
}

// Both helpers below expect m_mutex to be held by the caller.
int TrickplayCache::thumbnailIndexFor(qint64 positionMs) const
{
    if (!m_info.isValid() || positionMs < 0)
        return -1;
    return static_cast<int>(positionMs / m_info.intervalMs);
}

int TrickplayCache::sheetIndexFor(qint64 positionMs) const
{
    const int index = thumbnailIndexFor(positionMs);
    if (index < 0 || m_info.thumbnailsPerSheet() <= 0)
        return -1;
    return index / m_info.thumbnailsPerSheet();
}

void TrickplayCache::requestSheet(int sheetIndex, qint64 positionMs)
{
    if (!m_client || !m_networkQueue || sheetIndex < 0)
        return;

    QString itemId;
    int thumbnailWidth = 0;
    {
        QMutexLocker locker(&m_mutex);
        // Dragging back and forth across a sheet boundary must not re-fire
        // the same request on every miss.
        if (m_inFlight.contains(sheetIndex) || m_sheets.contains(sheetIndex))
            return;
        m_inFlight.append(sheetIndex);
        itemId = m_itemId;
        // The URL is keyed by the thumbnail *pixel* width (info.width), not
        // the grid column count (info.tileWidth).
        thumbnailWidth = m_info.width;
    }

    QNetworkRequest request(m_client->trickplayTileUrl(itemId, thumbnailWidth, sheetIndex));
    // Sheets live decoded in RAM; keeping the JPEG in the disk cache as well
    // would just write every tile twice.
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);

    m_networkQueue->get(
        request, NetworkQueue::Priority::Image, this, [this, sheetIndex, positionMs, itemId](QNetworkReply *reply) {
            const bool ok = reply && reply->error() == QNetworkReply::NoError;
            const QByteArray data = ok ? reply->readAll() : QByteArray();

            if (!ok || data.isEmpty()) {
                QMutexLocker locker(&m_mutex);
                if (m_itemId == itemId)
                    m_inFlight.removeAll(sheetIndex);
                return;
            }

            // Decode off the GUI thread; publish the result back on it.
            m_decodePool.start([this, sheetIndex, positionMs, itemId, data]() {
                QImage image = QImage::fromData(data);
                if (!image.isNull() && image.format() != QImage::Format_RGB32
                    && image.format() != QImage::Format_ARGB32_Premultiplied)
                    image = image.convertToFormat(QImage::Format_RGB32);

                QMetaObject::invokeMethod(
                    this,
                    [this, sheetIndex, positionMs, itemId, image]() {
                        {
                            QMutexLocker locker(&m_mutex);
                            if (m_itemId != itemId)
                                return; // source changed mid-flight
                            m_inFlight.removeAll(sheetIndex);
                            if (image.isNull())
                                return;
                            m_sheets.insert(sheetIndex, image);
                            touchLocked(sheetIndex);
                            evictIfNeededLocked();
                        }
                        emit thumbnailReady(positionMs);
                    },
                    Qt::QueuedConnection);
            });
        });
}

void TrickplayCache::touchLocked(int sheetIndex)
{
    m_lru.removeAll(sheetIndex);
    m_lru.append(sheetIndex);
}

void TrickplayCache::evictIfNeededLocked()
{
    while (m_sheets.size() > m_maxSheets && !m_lru.isEmpty()) {
        const int oldest = m_lru.takeFirst();
        m_sheets.remove(oldest);
    }
}
