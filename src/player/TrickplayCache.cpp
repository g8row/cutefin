#include "player/TrickplayCache.h"

#include "api/JellyfinClient.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace {

// TrickplayCache.h is fixed by contract and only carries a JellyfinClient*,
// not a NetworkQueue* -- and JellyfinClient deliberately has no "fetch raw
// bytes" method (see task brief; that file is owned elsewhere). trickplayTileUrl()
// already bakes `api_key` into the query string the way every other
// unauthenticated-header Jellyfin media URL does, so a plain manager needs no
// Authorization header to work. One instance shared by every TrickplayCache
// (there is only ever one alive per player) keeps HTTP/2 connections warm
// between sheet fetches, matching the "one QNAM for the app" spirit of
// NetworkQueue/ImageCache without requiring access to either.
QNetworkAccessManager *sharedNetworkManager()
{
    static auto *manager = new QNetworkAccessManager();
    return manager;
}

} // namespace

TrickplayCache::TrickplayCache(JellyfinClient *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
{
}

void TrickplayCache::setSource(const QString &itemId, const TrickplayInfo &info)
{
    if (m_itemId == itemId && m_info.width == info.width && m_info.height == info.height
        && m_info.tileWidth == info.tileWidth && m_info.tileHeight == info.tileHeight
        && m_info.intervalMs == info.intervalMs && m_info.thumbnailCount == info.thumbnailCount)
        return;

    clear();
    m_itemId = itemId;
    m_info = info;
}

QPixmap TrickplayCache::thumbnailAt(qint64 positionMs)
{
    if (!isEnabled() || positionMs < 0)
        return QPixmap();

    const int index = thumbnailIndexFor(positionMs);
    if (index < 0 || index >= m_info.thumbnailCount)
        return QPixmap();

    const int sheet = sheetIndexFor(positionMs);

    const auto it = m_sheets.constFind(sheet);
    if (it == m_sheets.constEnd()) {
        requestSheet(sheet, positionMs);
        return QPixmap();
    }

    touch(sheet);

    // Index maths per Models.h's TrickplayInfo comment (verified against the
    // live "Us" trickplay data: ms=1'234'000, intervalMs=10'000 -> index 123,
    // sheet 1, row (123/10)%10=2, column 123%10=3).
    const int row = (index / m_info.tileWidth) % m_info.tileHeight;
    const int col = index % m_info.tileWidth;
    const QRect cropRect(col * m_info.width, row * m_info.height, m_info.width, m_info.height);
    return it->copy(cropRect);
}

void TrickplayCache::clear()
{
    m_sheets.clear();
    m_lru.clear();
    // Replies already in flight are left to run to completion -- their
    // this-bound lambda re-checks m_itemId and discards the result if
    // setSource()/clear() has since moved the cache to a different item, so
    // nothing stale ever lands in m_sheets.
    m_inFlight.clear();
}

int TrickplayCache::thumbnailIndexFor(qint64 positionMs) const
{
    if (!isEnabled())
        return -1;
    return static_cast<int>(positionMs / m_info.intervalMs);
}

int TrickplayCache::sheetIndexFor(qint64 positionMs) const
{
    const int index = thumbnailIndexFor(positionMs);
    if (index < 0)
        return -1;
    return index / m_info.thumbnailsPerSheet();
}

void TrickplayCache::requestSheet(int sheetIndex, qint64 positionMs)
{
    if (!m_client || sheetIndex < 0)
        return;
    // The most important performance detail here: dragging back and forth
    // across a sheet boundary must not re-fire the same ~800KB request every
    // time thumbnailAt() misses the cache.
    if (m_inFlight.contains(sheetIndex))
        return;

    m_inFlight.append(sheetIndex);

    // The "tileWidth" JellyfinClient::trickplayTileUrl() wants is the
    // thumbnail pixel width (info.width, e.g. 320) -- it is the key the
    // server's /Videos/{id}/Trickplay/{width}/{index}.jpg path is keyed by,
    // as MediaItem::trickplay's comment spells out. TrickplayInfo::tileWidth
    // (e.g. 10) is the grid column count and plays no part in the URL.
    const QUrl url = m_client->trickplayTileUrl(m_itemId, m_info.width, sheetIndex);
    QNetworkReply *reply = sharedNetworkManager()->get(QNetworkRequest(url));

    const QString requestedItemId = m_itemId;
    connect(reply, &QNetworkReply::finished, this, [this, reply, sheetIndex, positionMs, requestedItemId]() {
        reply->deleteLater();
        m_inFlight.removeAll(sheetIndex);

        if (requestedItemId != m_itemId)
            return; // source changed mid-flight; this sheet no longer means anything
        if (reply->error() != QNetworkReply::NoError)
            return;

        // One decode per arriving sheet, never per hover -- thumbnailAt()
        // only ever crops an already-decoded QPixmap.
        QPixmap pixmap;
        if (!pixmap.loadFromData(reply->readAll()))
            return;

        m_sheets.insert(sheetIndex, pixmap);
        touch(sheetIndex);
        evictIfNeeded();

        emit thumbnailReady(positionMs);
    });
}

void TrickplayCache::touch(int sheetIndex)
{
    m_lru.removeAll(sheetIndex);
    m_lru.append(sheetIndex);
}

void TrickplayCache::evictIfNeeded()
{
    while (m_sheets.size() > kMaxSheets && !m_lru.isEmpty()) {
        const int oldest = m_lru.takeFirst();
        m_sheets.remove(oldest);
    }
}
