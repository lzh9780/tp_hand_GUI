// DiagramWidget.cpp
#include "DiagramWidget.hpp"

#include <QPainter>
#include <QPaintEvent>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QLinearGradient>
#include <QPolygonF>
#include <QFont>
#include <QPen>
#include <QCursor>
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
/* Same palette as the tactile plots so both panels look alike. */
const QColor kBackground(22, 24, 28);
const QColor kGrid(70, 76, 88, 150);
const QColor kAxis(110, 118, 132, 200);
const QColor kCurve(120, 200, 255);
const QColor kFillTop(90, 170, 235, 150);
const QColor kFillBottom(90, 170, 235, 20);
const QColor kText(214, 219, 226);
const QColor kTextDim(150, 158, 170);

/* Zoom limits, in visible samples. */
constexpr int kMinWindow = 20;
constexpr int kMaxWindow = 5000;

/* Rounds a value to a "nice" grid step (1, 2, 5 x 10^n). */
double niceStep(double raw)
{
    if (raw <= 0.0)
        return 1.0;
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    const double norm = raw / mag;
    double step = 1.0;
    if (norm > 5.0)
        step = 10.0;
    else if (norm > 2.0)
        step = 5.0;
    else if (norm > 1.0)
        step = 2.0;
    return step * mag;
}
} // namespace

DiagramWidget::DiagramWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(120, 90);
    // The widget repaints itself whenever the buffer changes.
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setMouseTracking(false);

    // Zoom and history browsing are frozen-only, and the tooltip says so instead
    // of leaving the user wondering why the wheel does nothing.
    setToolTip(tr("Live: the plot follows the newest sample.\n"
                  "Press Freeze to zoom (wheel) and drag through the history."));
}

void DiagramWidget::setJointName(const QString &module, const QString &joint)
{
    moduleName_ = module;
    jointName_ = joint;
    window_.clear();
    update();
}

void DiagramWidget::setWindowSamples(int samples)
{
    const int clamped = std::clamp(samples, kMinWindow, kMaxWindow);
    if (clamped == windowSamples_)
        return;
    windowSamples_ = clamped;
    update();
    emit viewChanged(this);
}

void DiagramWidget::zoomBy(double factor)
{
    if (factor <= 0.0)
        return;
    setWindowSamples(static_cast<int>(std::lround(windowSamples_ / factor)));
}

void DiagramWidget::setFrozen(bool frozen)
{
    if (frozen_ == frozen)
        return;
    frozen_ = frozen;

    // Leaving the frozen state returns the diagram to the plain live view:
    // whatever zoom / history position the operator had set is dropped, so the
    // live mode always shows the standard window at the newest sample instead of
    // silently keeping a stale, zoomed-in or scrolled-back view.
    if (!frozen_)
    {
        windowSamples_ = kDefaultWindowSamples;
        historyOffset_ = 0;
    }

    // Advertise the interaction only when it is actually available: the open-hand
    // cursor in the frozen state tells the operator the curve can be dragged.
    if (frozen_)
        setCursor(Qt::OpenHandCursor);
    else
        unsetCursor();

    update();
    emit viewChanged(this);
}

void DiagramWidget::setHistoryOffset(int offset)
{
    const int clamped = std::clamp(offset, 0, std::max(0, maxHistoryOffset_));
    if (clamped == historyOffset_)
        return;
    historyOffset_ = clamped;
    update();
    emit viewChanged(this);
}

bool DiagramWidget::valueAt(const HandStateMap &hand, float &outValue) const
{
    if (jointName_.isEmpty())
        return false;

    auto modIt = hand.find(moduleName_.toStdString());
    if (modIt == hand.end())
        return false;

    auto jIt = modIt->second.joints.find(jointName_.toStdString());
    if (jIt == modIt->second.joints.end())
        return false;

    // Joint position is the only quantity plotted.
    outValue = jIt->second.position;
    return true;
}

void DiagramWidget::renderWindow(const std::deque<HandStateMap> &history)
{
    // A frozen diagram does not follow new samples: it keeps the sample indices
    // it is showing, so the view stays put while data keeps arriving. Freezing
    // must not, however, block an explicit browse (dragging), which is requested
    // by changing historyOffset_ — that is why the captured indices, not the
    // frozen flag alone, decide whether there is anything to do.
    const int requestedEnd = static_cast<int>(history.size()) - 1 - historyOffset_;

    if (frozen_ && requestedEnd == frozenEnd_)
        return; // nothing changed and we are not following live

    const int total = static_cast<int>(history.size());
    if (total <= 0)
    {
        window_.clear();
        windowStartSample_ = 0;
        update();
        return;
    }

    // Expose how far back the user may scroll.
    maxHistoryOffset_ = std::max(0, total - windowSamples_);

    // The window ends `historyOffset_` samples before the newest one.
    const int end = total - 1 - historyOffset_; // inclusive, newest last
    const int begin = std::max(0, end - windowSamples_ + 1);
    if (end < begin)
    {
        window_.clear();
        windowStartSample_ = 0;
        update();
        return;
    }

    window_.clear();
    for (int i = begin; i <= end; ++i)
    {
        float v = 0.0f;
        if (valueAt(history[static_cast<size_t>(i)], v))
            window_.push_back(v);
    }

    // Index of the first plotted sample inside the whole recorded run: the X axis
    // is labelled with when that sample was taken, not with its position in the
    // window, so the labels stay meaningful while browsing the history.
    windowStartSample_ = historyOffsetSamples_ + begin;

    // Remember which sample the frozen view is pinned to.
    frozenEnd_ = end;

    update();
}

void DiagramWidget::clearWindow()
{
    window_.clear();
    windowStartSample_ = 0;
    // Invalidate the "already showing this" marker, so the next renderWindow()
    // call is not skipped as a no-op.
    frozenEnd_ = -1;
    update();
}

QSize DiagramWidget::sizeHint() const { return QSize(300, 260); }
QSize DiagramWidget::minimumSizeHint() const { return QSize(160, 140); }

void DiagramWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.fillRect(rect(), kBackground);

    /* The plotting area leaves room for the axis scales: tick labels on the left
       (values) and along the bottom (time). */
    const QFontMetrics fm(p.font());
    const int labelH = fm.height();
    const int yLabelW = fm.horizontalAdvance(QStringLiteral("-0.000")) + 6;
    const QRectF area = QRectF(rect()).adjusted(
        yLabelW + 4,                  // left: Y tick labels
        labelH + 6,                   // top: title row
        -8,                           // right
        -(labelH + 6));               // bottom: X tick labels
    if (area.width() <= 20 || area.height() <= 20)
        return;

    /* ---- title (top-left) and readout (top-right) ---- */
    const QRectF header(4, 2, width() - 8, labelH);
    const QString title = jointName_.isEmpty() ? moduleName_
                                               : (moduleName_ + "." + jointName_);
    QFont f = p.font();
    f.setBold(true);
    p.setFont(f);
    p.setPen(kText);
    p.drawText(header, Qt::AlignTop | Qt::AlignLeft, title);

    f.setBold(false);
    p.setFont(f);
    if (!window_.empty())
    {
        p.setPen(kTextDim);
        p.drawText(header, Qt::AlignTop | Qt::AlignRight,
                   QStringLiteral("%1 rad").arg(double(window_.back()), 0, 'f', 3));
    }

    /* ---- value range (kept as a base for the 1-2-5 tick walk below) ---- */
    double vMin = 0.0, vMax = 0.0;
    if (!window_.empty())
    {
        vMin = *std::min_element(window_.begin(), window_.end());
        vMax = *std::max_element(window_.begin(), window_.end());
        if (std::abs(vMax - vMin) < 1e-9)
        {
            // Flat trace: keep it in the middle of a small band.
            vMin -= 0.5;
            vMax += 0.5;
        }
        else
        {
            const double pad = (vMax - vMin) * 0.12;
            vMin -= pad;
            vMax += pad;
        }
    }
    else
    {
        vMin = -1.0;
        vMax = 1.0;
    }

    /* ---- X range: absolute time of the first / last plotted sample ----
       With no data there is no meaningful time to label, so a default span is
       used and the tick labels are suppressed below (otherwise every tick would
       collapse onto 0.0). */
    const double period = samplePeriodSec_ > 0.0 ? samplePeriodSec_ : 0.02;
    const int n = static_cast<int>(window_.size());
    const bool hasData = n >= 2;
    const double t0 = hasData ? windowStartSample_ * period : 0.0;
    const double t1 = hasData ? (windowStartSample_ + (n - 1)) * period
                              : windowSamples_ * period;
    const double tSpan = std::max(1e-9, t1 - t0);

    /* Snap the Y range outwards to a whole number of grid steps so the tick
       labels are round numbers and the top/bottom grid lines are on-grid. */
    const double yStep = niceStep((vMax - vMin) / 4.0);
    const double gridMin = std::floor(vMin / yStep) * yStep;
    const double gridMax = std::ceil(vMax / yStep) * yStep;
    const double vSpan = std::max(1e-9, gridMax - gridMin);

    /* ---- grid ---- */
    p.setPen(QPen(kGrid, 1.0));
    for (double v = gridMin; v <= gridMax + yStep * 0.5; v += yStep)
    {
        const double y = area.bottom() - (v - gridMin) / vSpan * area.height();
        p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
    }
    const double xStep = niceStep(tSpan / 4.0);
    for (double t = std::ceil(t0 / xStep) * xStep; t <= t1 + xStep * 0.5; t += xStep)
    {
        const double x = area.left() + (t - t0) / tSpan * area.width();
        p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
    }

    /* ---- plot border (the two axes) ---- */
    p.setPen(QPen(kAxis, 1.2));
    p.drawLine(area.bottomLeft(), area.bottomRight()); // X axis
    p.drawLine(area.topLeft(), area.bottomLeft());     // Y axis

    /* ---- Y tick labels (left of the axis) ---- */
    p.setPen(kTextDim);
    for (double v = gridMin; v <= gridMax + yStep * 0.5; v += yStep)
    {
        const double y = area.bottom() - (v - gridMin) / vSpan * area.height();
        const QRectF box(area.left() - yLabelW - 2, y - labelH / 2.0, yLabelW, labelH);
        p.drawText(box, Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(v, 'f', 2));
    }

    /* ---- X tick labels (below the axis), in seconds ----
       Only drawn once there is data: with an empty window the range is the
       default span and labelling it would just print placeholder times. */
    if (hasData)
    {
        for (double t = std::ceil(t0 / xStep) * xStep; t <= t1 + xStep * 0.5; t += xStep)
        {
            const double x = area.left() + (t - t0) / tSpan * area.width();
            const QRectF box(x - 40, area.bottom() + 2, 80, labelH);
            p.drawText(box, Qt::AlignHCenter | Qt::AlignTop,
                       QString::number(t, 'f', 1));
        }
    }

    /* ---- curve ---- */
    if (n >= 2)
    {
        const double stepX = area.width() / double(n - 1);

        QPolygonF line;
        line.reserve(n);
        for (int i = 0; i < n; ++i)
        {
            const double x = area.left() + i * stepX;
            const double y = area.bottom() -
                             std::clamp((double(window_[static_cast<size_t>(i)]) - gridMin) / vSpan,
                                        0.0, 1.0) * area.height();
            line << QPointF(x, y);
        }

        // Filled area under the curve, same treatment as the tactile force plot.
        QPolygonF filled = line;
        filled << QPointF(line.last().x(), area.bottom())
               << QPointF(line.first().x(), area.bottom());

        QLinearGradient grad(area.topLeft(), area.bottomLeft());
        grad.setColorAt(0.0, kFillTop);
        grad.setColorAt(1.0, kFillBottom);
        p.setPen(Qt::NoPen);
        p.setBrush(grad);
        p.drawPolygon(filled);

        p.setPen(QPen(kCurve, 1.6));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(line);
    }
    else if (window_.empty())
    {
        p.setPen(kTextDim);
        p.drawText(area, Qt::AlignCenter, tr("Waiting for data..."));
    }

    /* ---- freeze / history badge, so the state is never ambiguous ---- */
    QString badge;
    if (frozen_)
        badge = tr("FROZEN");
    if (historyOffset_ > 0)
        badge += (badge.isEmpty() ? QString() : QStringLiteral("  ")) +
                 tr("-%1").arg(historyOffset_);
    if (!badge.isEmpty())
    {
        f.setBold(true);
        p.setFont(f);
        p.setPen(QColor(240, 190, 90)); // amber: "you are not looking at live"
        const QRectF box(area.right() - 140, area.top() - labelH - 4, 140, labelH);
        p.drawText(box, Qt::AlignTop | Qt::AlignRight, badge);
    }
}

/* ---- interactions: zoom / history, only while frozen ----
 *
 * While the view is live the curve must keep following the newest sample, so
 * changing the zoom or dragging through the history is refused: the panel would
 * immediately overwrite the change on the next sample anyway, and an operator
 * would be left fighting the view. Freeze first, then zoom / drag.
 */

void DiagramWidget::wheelEvent(QWheelEvent *event)
{
    if (!frozen_)
    {
        // Let the event bubble: the scroll area can use it to scroll the list.
        event->ignore();
        return;
    }

    // Wheel up = zoom in (fewer samples in the window).
    const int steps = event->angleDelta().y();
    if (steps == 0)
    {
        event->ignore();
        return;
    }
    // Consume the event *before* touching the view: setWindowSamples() emits
    // viewChanged(), and the panel may re-enter and re-render in response. Doing
    // the bookkeeping first keeps this handler free of any window-system state
    // once the signal is out.
    event->accept();

    const double factor = std::pow(1.15, steps / 120.0);
    setWindowSamples(static_cast<int>(std::lround(windowSamples_ / factor)));
}

void DiagramWidget::mousePressEvent(QMouseEvent *event)
{
    // Left/right dragging browses the recorded history, but only while frozen:
    // in live mode the view must stay pinned to the newest sample. Browsing also
    // needs more history than one window holds, otherwise there is nothing to
    // walk through.
    if (event->button() == Qt::LeftButton && frozen_ && maxHistoryOffset_ > 0)
    {
        dragging_ = true;
        dragStartX_ = event->pos().x();
        dragStartOffset_ = historyOffset_;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void DiagramWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!dragging_)
    {
        QWidget::mouseMoveEvent(event);
        return;
    }

    const int dx = event->pos().x() - dragStartX_;
    const double perPx = double(windowSamples_) / std::max(1, width());
    // Dragging to the right moves towards older samples (the content follows the
    // cursor), so the offset grows.
    setHistoryOffset(dragStartOffset_ + static_cast<int>(std::lround(dx * perPx)));
    event->accept();
}

void DiagramWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (dragging_ && event->button() == Qt::LeftButton)
    {
        dragging_ = false;
        unsetCursor();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}
