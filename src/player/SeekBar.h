#pragma once

#include "api/Models.h"

#include <QList>
#include <QSlider>

class TrickplayCache;

// The player's seek slider, with chapter markers painted into the groove and
// a hover preview showing the trickplay thumbnail (falling back to just a
// timestamp when the server has no trickplay for the item).
//
// Kept as a QSlider subclass so it keeps native keyboard/step behaviour and
// the platform's own groove/handle metrics; only the markers and the preview
// popup are custom-drawn.
class SeekBar : public QSlider
{
    Q_OBJECT
public:
    explicit SeekBar(QWidget *parent = nullptr);

    // Chapter markers are drawn at these positions. Pass an empty list for
    // content without chapters.
    void setChapters(const QList<Chapter> &chapters, qint64 durationSeconds);

    // Optional; when set and enabled, hovering shows a thumbnail above the
    // cursor. The bar does not own the cache.
    void setTrickplayCache(TrickplayCache *cache);

    // The chapter containing `seconds`, or -1. Used by prev/next chapter.
    int chapterIndexAt(qint64 seconds) const;
    const QList<Chapter> &chapters() const { return m_chapters; }

signals:
    // Emitted when the user clicks the groove directly rather than dragging
    // the handle -- QSlider's default is a page-step, which feels broken on a
    // seek bar where a click should mean "go there".
    void seekRequested(qint64 seconds);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    bool event(QEvent *event) override;

private:
    qint64 secondsAtX(int x) const;
    int xForSeconds(qint64 seconds) const;
    void updatePreview(const QPoint &pos);
    void hidePreview();

    QList<Chapter> m_chapters;
    qint64 m_durationSeconds = 0;

    TrickplayCache *m_trickplay = nullptr;
    class TrickplayPopup *m_preview = nullptr;
    qint64 m_hoverSeconds = -1;
};
