#pragma once

#include "api/Models.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPixmap>

class JellyfinClient;

// Fetches and caches trickplay tile sheets, and crops single thumbnails out
// of them for the seek bar's hover preview.
//
// Each sheet is a ~800KB JPEG holding tileWidth*tileHeight thumbnails, so the
// cache is deliberately small and bounded: at most kMaxSheets decoded sheets,
// evicted least-recently-used, and cleared outright on stop (FEATURES.md §E).
// Holding a whole movie's sheets would be tens of megabytes for a feature the
// user only sees while dragging.
class TrickplayCache : public QObject
{
    Q_OBJECT
public:
    static constexpr int kMaxSheets = 8;

    explicit TrickplayCache(JellyfinClient *client, QObject *parent = nullptr);

    // Selects the item and the tile width to use. Passing an invalid info
    // disables the cache (isEnabled() false), which is the normal state for
    // content the server has not generated trickplay for.
    void setSource(const QString &itemId, const TrickplayInfo &info);
    bool isEnabled() const { return m_info.isValid(); }
    const TrickplayInfo &info() const { return m_info; }

    // The thumbnail for a position, if its sheet is already decoded. Returns
    // a null pixmap and starts a fetch otherwise -- callers draw nothing (or
    // keep the previous frame) until thumbnailReady() fires.
    QPixmap thumbnailAt(qint64 positionMs);

    // Drops every decoded sheet. Call on playback stop.
    void clear();

signals:
    // A sheet arrived; `positionMs` is the position originally asked for, so
    // a hovering seek bar can re-query and repaint only if still relevant.
    void thumbnailReady(qint64 positionMs);

private:
    int sheetIndexFor(qint64 positionMs) const;
    int thumbnailIndexFor(qint64 positionMs) const;
    void requestSheet(int sheetIndex, qint64 positionMs);
    void touch(int sheetIndex);
    void evictIfNeeded();

    JellyfinClient *m_client;
    QString m_itemId;
    TrickplayInfo m_info;

    QHash<int, QPixmap> m_sheets;
    QList<int> m_lru;          // most-recently-used last
    QList<int> m_inFlight;
};
