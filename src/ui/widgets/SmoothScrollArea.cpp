#include "SmoothScrollArea.h"

#include <QScrollBar>
#include <QTimer>
#include <QWheelEvent>

namespace {
constexpr int kTickIntervalMs = 16;
constexpr qreal kEasingFactor = 0.22;
// Below this distance-to-target the timer stops instead of running forever
// at idle chasing a value it has effectively already reached. It must stay
// under 1.0 so it is reachable by the whole-pixel steps below.
constexpr qreal kSnapThreshold = 0.5;

// The eased step toward a target, floored at one whole pixel in the direction
// of travel so integer scrollbars always make progress.
qreal steppedTowards(qreal delta)
{
    const qreal step = delta * kEasingFactor;
    if (qAbs(step) < 1.0)
        return delta > 0 ? 1.0 : -1.0;
    return step;
}
} // namespace

SmoothScrollArea::SmoothScrollArea(QWidget *parent)
    : QScrollArea(parent)
    , m_timer(new QTimer(this))
{
    m_timer->setInterval(kTickIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &SmoothScrollArea::tick);
}

void SmoothScrollArea::syncTargetsToCurrent()
{
    m_targetH = horizontalScrollBar()->value();
    m_targetV = verticalScrollBar()->value();
}

void SmoothScrollArea::kickTimer()
{
    if (!m_timer->isActive())
        m_timer->start();
}

void SmoothScrollArea::tick()
{
    QScrollBar *hbar = horizontalScrollBar();
    QScrollBar *vbar = verticalScrollBar();

    // Scrollbar values are integers, so an eased step smaller than half a
    // pixel rounds away to nothing: at a 2px remaining distance the step is
    // 0.44px, the value never changes, and a threshold test against the
    // *target* never becomes true -- the timer then runs at 60Hz forever
    // without converging. Forcing a whole-pixel minimum step guarantees
    // arrival, which is what actually lets the timer stop.
    const qreal curH = hbar->value();
    const qreal curV = vbar->value();

    const qreal deltaH = m_targetH - curH;
    const qreal deltaV = m_targetV - curV;

    const bool arrivedH = qAbs(deltaH) < kSnapThreshold;
    const bool arrivedV = qAbs(deltaV) < kSnapThreshold;

    if (!arrivedH)
        hbar->setValue(qRound(curH + steppedTowards(deltaH)));
    if (!arrivedV)
        vbar->setValue(qRound(curV + steppedTowards(deltaV)));

    if (arrivedH && arrivedV) {
        // Land exactly on target and stop -- a timer left running at idle
        // burns a wakeup every 16ms for nothing (DESIGN.md perf budget).
        hbar->setValue(qRound(m_targetH));
        vbar->setValue(qRound(m_targetV));
        m_timer->stop();
    }
}

void SmoothScrollArea::scrollByPixels(int dxPixels, int dyPixels)
{
    // If the timer isn't running, the bars are already sitting at rest, so
    // the target is just "current + delta". If it IS running, a fresh target
    // is still correct: it means "current animated target, offset further".
    if (!m_timer->isActive())
        syncTargetsToCurrent();

    QScrollBar *hbar = horizontalScrollBar();
    QScrollBar *vbar = verticalScrollBar();

    m_targetH = qBound<qreal>(hbar->minimum(), m_targetH + dxPixels, hbar->maximum());
    m_targetV = qBound<qreal>(vbar->minimum(), m_targetV + dyPixels, vbar->maximum());

    kickTimer();
}

void SmoothScrollArea::wheelEvent(QWheelEvent *event)
{
    // Trackpad momentum already carries its own inertia curve from the
    // platform; easing on top of it would produce a visibly wrong,
    // double-damped motion. Let QScrollArea handle it directly.
    if (event->phase() != Qt::NoScrollPhase) {
        QScrollArea::wheelEvent(event);
        return;
    }

    if (!m_timer->isActive())
        syncTargetsToCurrent();

    QScrollBar *hbar = horizontalScrollBar();
    QScrollBar *vbar = verticalScrollBar();

    const QPoint pixelDelta = event->pixelDelta();
    const QPoint angleDelta = event->angleDelta();

    // Positive angleDelta/pixelDelta means "wheel rotated up" i.e. content
    // should scroll toward its start, which is a scrollbar value DECREASE --
    // the same convention QAbstractSlider::wheelEvent uses.
    int dx = 0;
    int dy = 0;
    if (!pixelDelta.isNull()) {
        dx = pixelDelta.x();
        dy = pixelDelta.y();
    } else {
        // A discrete wheel notch is 120 angle-delta units; translate to
        // roughly the same pixel step QScrollArea's own default uses.
        dx = angleDelta.x() / 120 * hbar->singleStep() * 3;
        dy = angleDelta.y() / 120 * vbar->singleStep() * 3;
    }

    // Shift+wheel is the conventional way to scroll a horizontal strip with
    // a vertical-only wheel; honor it the same way QScrollArea would.
    if (dx == 0 && dy != 0 && (event->modifiers() & Qt::ShiftModifier)) {
        dx = dy;
        dy = 0;
    }

    m_targetH = qBound<qreal>(hbar->minimum(), m_targetH - dx, hbar->maximum());
    m_targetV = qBound<qreal>(vbar->minimum(), m_targetV - dy, vbar->maximum());

    kickTimer();
    event->accept();
}
