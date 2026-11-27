#pragma once

#include "api/Models.h"

#include <QHash>
#include <QImage>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QThreadPool>

class JellyfinClient;
class NetworkQueue;

// Fetches and caches trickplay tile sheets as thread-safe QImages (decoded off
// the GUI thread), cropping single thumbnails on demand for TrickplayImageProvider.
// Bounded to maxSheets() decoded sheets in RAM (a decoded 10x10 sheet of
// 320px thumbnails is ~33 MB) and cleared outright on stop. Tiles bypass the
// HTTP disk cache: they are decoded straight into RAM and re-fetched cheaply,
// so writing every one to disk as well only costs I/O.
class TrickplayCache : public QObject
{
    Q_OBJECT
public:
    static constexpr int kDefaultMaxSheets = 4;
    static constexpr int kDualCoreMaxSheets = 3;

    explicit TrickplayCache(JellyfinClient *client, NetworkQueue *networkQueue, QObject *parent = nullptr);
    ~TrickplayCache() override;

    void setSource(const QString &itemId, const TrickplayInfo &info);

    // Dual-core mode keeps one sheet fewer resident.
    void setDualCoreMode(bool enabled);
    int maxSheets() const;
    bool isEnabled() const;
    TrickplayInfo info() const;

    // Thread-safe lookup of an already-decoded thumbnail. Returns null QImage
    // on miss without starting network work (call prefetchAt() on GUI thread).
    QImage thumbnailIfCached(qint64 positionMs) const;
    bool hasThumbnailAt(qint64 positionMs) const;

    // GUI-thread call: returns cached thumbnail if available, otherwise starts
    // an async fetch + off-thread decode and emits thumbnailReady() on completion.
    QImage thumbnailAt(qint64 positionMs);

    void clear();

signals:
    void thumbnailReady(qint64 positionMs);

private:
    int thumbnailIndexFor(qint64 positionMs) const;
    int sheetIndexFor(qint64 positionMs) const;
    void requestSheet(int sheetIndex, qint64 positionMs);
    void touchLocked(int sheetIndex);
    void evictIfNeededLocked();

    JellyfinClient *m_client;
    NetworkQueue *m_networkQueue;

    mutable QMutex m_mutex;
    QString m_itemId;
    TrickplayInfo m_info;
    QHash<int, QImage> m_sheets;
    QList<int> m_lru;
    QList<int> m_inFlight;
    QList<int> m_failedSheets;
    QThreadPool m_decodePool;
    int m_maxSheets = kDefaultMaxSheets;
};
