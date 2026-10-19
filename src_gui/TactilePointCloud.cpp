#include "TactilePointCloud.hpp"

#include <QPainter>
#include <QPaintEvent>
#include <QLinearGradient>
#include <QFont>
#include <QPen>
#include <algorithm>
#include <cmath>
#include <limits>

TactilePointCloud::TactilePointCloud(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(140, 140);
}

void TactilePointCloud::setTitle(const QString &title)
{
    title_ = title;
    update();
}

void TactilePointCloud::setPoints(const QVector<QPointF> &points)
{
    points_ = points;
    update();
}

void TactilePointCloud::setPressure(float value)
{
    value_ = value;

    // Auto-track the full scale: follow the observed peak but never collapse to
    // zero, and decay slowly so a single spike does not flatten the colours.
    peak_ = std::max(value_, peak_ * 0.995f);
    if (peak_ < 1.0f)
        peak_ = 1.0f;

    update();
}

void TactilePointCloud::setPressureRange(float minPressure, float maxPressure)
{
    minPressure_ = minPressure;
    maxPressure_ = maxPressure;
    update();
}

QSize TactilePointCloud::sizeHint() const { return QSize(220, 200); }
QSize TactilePointCloud::minimumSizeHint() const { return QSize(120, 120); }

void TactilePointCloud::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(22, 24, 28));

    const QRectF area = QRectF(rect()).adjusted(8, 8, -8, -8);
    if (area.width() <= 10 || area.height() <= 10)
        return;

    const QPointF center = area.center();

    // ---- main axis through the origin ----
    // (0, 0) is the centre of the panel; +x goes right, +y goes up. Only the
    // main X/Y axes are drawn, no extra grid.
    painter.setPen(QPen(QColor(90, 98, 110, 150), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(QPointF(area.left(), center.y()), QPointF(area.right(), center.y()));
    painter.drawLine(QPointF(center.x(), area.top()), QPointF(center.x(), area.bottom()));

    // ---- pressure matrix ----
    // Every configured contact point becomes one cell of the matrix; the cell
    // size is derived from the spacing between neighbouring points, so adjacent
    // cells of a regular layout touch and read as a grid.
    const float fullScale = std::max({peak_, maxPressure_, 1.0f});

    // Smallest coordinate distance between neighbouring points -> cell spacing.
    double spacing = std::numeric_limits<double>::infinity();
    for (int i = 0; i < points_.size(); ++i)
    {
        for (int j = i + 1; j < points_.size(); ++j)
        {
            const double dx = std::fabs(points_[i].x() - points_[j].x());
            const double dy = std::fabs(points_[i].y() - points_[j].y());
            const double d = (dx > 1e-6 && dy > 1e-6) ? std::min(dx, dy)
                           : (dx > 1e-6 ? dx : dy);
            if (d > 1e-6)
                spacing = std::min(spacing, d);
        }
    }
    if (!std::isfinite(spacing))
        spacing = 1.0; // single point / coincident points

    // Matrix spread: how many cells the point layout spans along each axis.
    double spanX = 0.0, spanY = 0.0;
    if (!points_.isEmpty())
    {
        double minX = points_[0].x(), maxX = points_[0].x();
        double minY = points_[0].y(), maxY = points_[0].y();
        for (const QPointF &p : points_)
        {
            minX = std::min(minX, p.x());
            maxX = std::max(maxX, p.x());
            minY = std::min(minY, p.y());
            maxY = std::max(maxY, p.y());
        }
        spanX = maxX - minX;
        spanY = maxY - minY;
    }
    const double gridSpan = std::max({spanX, spanY, 1.0});
    const double cellPx = std::clamp(
        std::min(area.width(), area.height()) * 0.30 / gridSpan, 5.0, 28.0);
    const double scale = cellPx / spacing;

    const QColor color = pressureToColor(value_);
    for (const QPointF &p : points_)
    {
        const QPointF c(center.x() + p.x() * scale,
                        center.y() - p.y() * scale);
        const QRectF cell(c.x() - cellPx / 2.0, c.y() - cellPx / 2.0,
                          cellPx, cellPx);
        // A light outline separates the matrix cells and keeps them visible on
        // the dark panel; the bright fills also read on lighter backgrounds.
        painter.setPen(QPen(QColor(255, 255, 255, 235), 1.5));
        painter.setBrush(color);
        painter.drawRoundedRect(cell, 2.0, 2.0);
    }

    // ---- readouts ----
    painter.setPen(QColor(210, 214, 220));
    QFont f = painter.font();
    f.setBold(true);
    painter.setFont(f);
    painter.drawText(area.adjusted(2, 0, 0, 0), Qt::AlignTop | Qt::AlignLeft, title_);

    f.setBold(false);
    painter.setFont(f);
    painter.setPen(QColor(150, 158, 170));
    // painter.drawText(area, Qt::AlignTop | Qt::AlignRight,
    //                  tr("now %1").arg(QString::number(value_, 'f', 1)));

    // Colour legend: 0 .. full scale.
    const QRectF legend(area.left(), area.bottom() - 8, std::min(area.width() * 0.6, 120.0), 8.0);
    QLinearGradient grad(legend.topLeft(), legend.topRight());
    grad.setColorAt(0.0, pressureToColor(minPressure_));
    grad.setColorAt(1.0, pressureToColor(fullScale));
    painter.setPen(Qt::NoPen);
    painter.setBrush(grad);
    painter.drawRect(legend);
    painter.setPen(QPen(QColor(140, 148, 160), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(legend);
}

QColor TactilePointCloud::pressureToColor(float value) const
{
    float t = 0.0f;
    const float fullScale = std::max({peak_, maxPressure_, 1.0f});
    if (fullScale > minPressure_)
        t = (value - minPressure_) / (fullScale - minPressure_);
    t = std::clamp(t, 0.0f, 1.0f);

    // Bright, high-luminance stops: the colours stay obvious both on the dark
    // panel and against any lighter backdrop (no deep navy that sinks into a
    // dark background, no pure yellow that disappears on white).
    constexpr float stops[4][4] = {
        {0.00f, 0.0f, 170.0f, 255.0f},  // vivid cyan-blue
        {0.35f, 0.0f, 235.0f, 160.0f},  // spring green
        {0.70f, 255.0f, 220.0f, 40.0f}, // amber
        {1.00f, 255.0f, 70.0f, 40.0f},  // hot red-orange
    };
    int seg = 0;
    while (seg < 3 && t > stops[seg + 1][0])
        ++seg;
    const float k = (t - stops[seg][0]) / (stops[seg + 1][0] - stops[seg][0]);
    const int r = static_cast<int>(stops[seg][1] + (stops[seg + 1][1] - stops[seg][1]) * k);
    const int g = static_cast<int>(stops[seg][2] + (stops[seg + 1][2] - stops[seg][2]) * k);
    const int b = static_cast<int>(stops[seg][3] + (stops[seg + 1][3] - stops[seg][3]) * k);
    return QColor(r, g, b);
}
