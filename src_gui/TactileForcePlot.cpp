#include "TactileForcePlot.hpp"

#include <QPainter>
#include <QPaintEvent>
#include <QLinearGradient>
#include <QFont>
#include <QPen>
#include <algorithm>

TactileForcePlot::TactileForcePlot(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(140, 140);
}

void TactileForcePlot::setTitle(const QString &title)
{
    title_ = title;
    update();
}

void TactileForcePlot::setHistoryLimit(int samples)
{
    historyLimit_ = std::max(10, samples);
    while (static_cast<int>(history_.size()) > historyLimit_)
        history_.pop_front();
}

void TactileForcePlot::clearHistory()
{
    history_.clear();
    peak_ = 1.0;
    update();
}

void TactileForcePlot::appendSample(double force)
{
    history_.push_back(force);
    while (static_cast<int>(history_.size()) > historyLimit_)
        history_.pop_front();

    // The Y range follows the observed peak but never collapses to zero, and it
    // decays slowly so a single spike does not flatten the rest of the trace.
    const double observed = history_.empty() ? 0.0 : *std::max_element(history_.begin(), history_.end());
    peak_ = std::max(observed, peak_ * 0.995);
    if (peak_ < 1e-6)
        peak_ = 1.0;

    update();
}

QSize TactileForcePlot::sizeHint() const { return QSize(220, 200); }
QSize TactileForcePlot::minimumSizeHint() const { return QSize(120, 120); }

void TactileForcePlot::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(22, 24, 28));

    const QRectF area = QRectF(rect()).adjusted(8, 8, -8, -8);
    if (area.width() <= 10 || area.height() <= 10)
        return;

    // --- grid ---
    painter.setPen(QPen(QColor(70, 76, 88, 150), 1.0));
    for (int i = 0; i <= 4; ++i)
    {
        const double y = area.top() + area.height() * i / 4.0;
        painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
    }
    for (int i = 0; i <= 6; ++i)
    {
        const double x = area.left() + area.width() * i / 6.0;
        painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
    }

    // --- trace ---
    if (history_.size() >= 2)
    {
        const double yMax = peak_ * 1.15;
        const int n = static_cast<int>(history_.size());
        const double stepX = area.width() / double(std::max(1, historyLimit_ - 1));

        QPolygonF line;
        line.reserve(n);
        for (int i = 0; i < n; ++i)
        {
            const double x = area.right() - (n - 1 - i) * stepX;
            const double y = area.bottom() - std::clamp(history_[i] / yMax, 0.0, 1.0) * area.height();
            line << QPointF(x, y);
        }

        // Filled area under the curve for readability.
        QPolygonF filled = line;
        filled << QPointF(line.last().x(), area.bottom())
               << QPointF(line.first().x(), area.bottom());

        QLinearGradient grad(area.topLeft(), area.bottomLeft());
        grad.setColorAt(0.0, QColor(80, 200, 160, 150));
        grad.setColorAt(1.0, QColor(80, 200, 160, 20));
        painter.setPen(Qt::NoPen);
        painter.setBrush(grad);
        painter.drawPolygon(filled);

        painter.setPen(QPen(QColor(120, 235, 190), 1.6));
        painter.setBrush(Qt::NoBrush);
        painter.drawPolyline(line);
    }

    // --- readouts ---
    painter.setPen(QColor(210, 214, 220));
    QFont f = painter.font();
    f.setBold(true);
    painter.setFont(f);
    painter.drawText(area.adjusted(2, 0, 0, 0), Qt::AlignTop | Qt::AlignLeft, title_);

    const QString current = history_.empty()
                                ? QStringLiteral("--")
                                : QString::number(history_.back(), 'f', 1);
    const QString peakText = QString::number(peak_, 'f', 1);

    f.setBold(false);
    painter.setFont(f);
    painter.setPen(QColor(150, 158, 170));
    painter.drawText(area, Qt::AlignTop | Qt::AlignRight,
                     tr("now %1 / peak %2").arg(current, peakText));
}
