#pragma once

#include <QScrollArea>

class QTimer;
class QWheelEvent;

// QScrollArea where discrete mouse-wheel events accumulate into a target
// offset that a 16ms timer eases the actual scrollbar value toward
// (exponential, ~0.22/frame) -- DESIGN.md §5. Native macOS trackpad
// momentum/gesture scrolling (QWheelEvent::phase() != Qt::NoScrollPhase) is
// passed straight through to QScrollArea's own handling instead of being
// double-eased on top of the platform's own inertia.
class SmoothScrollArea : public QScrollArea
{
    Q_OBJECT
public:
    explicit SmoothScrollArea(QWidget *parent = nullptr);

    // Smoothly scrolls by `dxPixels`/`dyPixels` using the same easing as a
    // mouse-wheel step. Used by MediaRow's overflow chevrons.
    void scrollByPixels(int dxPixels, int dyPixels = 0);

protected:
    void wheelEvent(QWheelEvent *event) override;

private:
    void tick();
    void kickTimer();
    void syncTargetsToCurrent();

    QTimer *m_timer;
    qreal m_targetH = 0;
    qreal m_targetV = 0;
};
