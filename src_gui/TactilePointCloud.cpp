#include "TactilePointCloud.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QLinearGradient>
#include <QPolygonF>
#include <QFont>
#include <QPen>
#include <algorithm>
#include <cmath>

namespace
{
/* Isometric projection: grid (col, row) + height -> screen offset. */
struct Projector
{
    QPointF origin;   // screen position of grid cell (0, 0) at height 0
    double cw{1.0};   // projected width of one column step
    double ch{0.5};   // projected height of one row step (depth)
    double zh{1.0};   // projected height of one pressure unit

    QPointF operator()(double col, double row, double z) const
    {
        return origin + QPointF((col - row) * cw * 0.5 + 0.0,
                                (col + row) * ch * 0.5 - z * zh);
    }
};
} // namespace

TactilePointCloud::TactilePointCloud(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(140, 140);
    matrix_.rows = 4;
    matrix_.cols = 4;
    matrix_.values = QVector<float>(16, 0.0f);
}

void TactilePointCloud::setTitle(const QString &title)
{
    title_ = title;
    update();
}

void TactilePointCloud::setPressureMatrix(const PressureMatrix &matrix)
{
    if (matrix.rows <= 0 || matrix.cols <= 0)
        return;
    if (matrix.values.size() != matrix.rows * matrix.cols)
        return;
    matrix_ = matrix;
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

float TactilePointCloud::cellValue(int row, int col) const
{
    const int index = row * matrix_.cols + col;
    if (index < 0 || index >= matrix_.values.size())
        return 0.0f;
    return matrix_.values[index];
}

void TactilePointCloud::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(22, 24, 28));

    const QRectF area = QRectF(rect()).adjusted(8, 8, -8, -8);
    if (area.width() <= 10 || area.height() <= 10 || matrix_.rows <= 0 || matrix_.cols <= 0)
        return;

    // Full-scale height for the projection: use a fixed reference so a light
    // touch does not look identical to a firm one.
    const double zMax = std::max<double>(maxPressure_, 1.0);
    const double gridSpan = std::max(matrix_.rows, matrix_.cols);

    Projector proj;
    proj.cw = area.width() / (gridSpan + 1.0);
    proj.ch = proj.cw * 0.55;
    // Reserve a corner of the box for the tallest possible column.
    proj.zh = std::min(area.height() * 0.55 / zMax, proj.cw * 6.0);

    const double gridW = (matrix_.cols + matrix_.rows) * proj.cw * 0.5;
    const double gridH = (matrix_.cols + matrix_.rows) * proj.ch * 0.5;
    proj.origin = QPointF(area.center().x() - gridW * 0.5 + proj.cw * 0.5,
                          area.bottom() - (area.height() - gridH - zMax * proj.zh) * 0.5);

    // Back-to-front painter's algorithm: sort by (row + col) so nearer cells are
    // drawn last and correctly occlude the ones behind them.
    QVector<QPair<int, int>> cells;
    cells.reserve(matrix_.rows * matrix_.cols);
    for (int r = 0; r < matrix_.rows; ++r)
        for (int c = 0; c < matrix_.cols; ++c)
            cells.append({r, c});
    std::sort(cells.begin(), cells.end(), [](const QPair<int, int> &a, const QPair<int, int> &b)
              { return (a.first + a.second) < (b.first + b.second); });

    // Baseline grid so an idle sensor still shows its footprint.
    painter.setPen(QPen(QColor(90, 98, 110, 120), 1.0));
    for (int r = 0; r <= matrix_.rows; ++r)
        painter.drawLine(proj(0, r, 0), proj(matrix_.cols, r, 0));
    for (int c = 0; c <= matrix_.cols; ++c)
        painter.drawLine(proj(c, 0, 0), proj(c, matrix_.rows, 0));

    for (const auto &cell : cells)
    {
        const int r = cell.first;
        const int c = cell.second;
        const float value = cellValue(r, c);
        const double z = std::clamp<double>(value, 0.0, zMax);

        const QPointF base = proj(c, r, z);
        const QPointF nextCol = proj(c + 1, r, z);
        const QPointF nextRow = proj(c, r + 1, z);
        const QPointF diag = proj(c + 1, r + 1, z);

        // Column side (darker) then top face (coloured by pressure) to fake a
        // shaded 3D extrusion of the cell.
        QPolygonF side;
        side << nextCol << diag << proj(c + 1, r + 1, 0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(pressureToColor(value).darker(180));
        painter.drawPolygon(side);

        QPolygonF top;
        top << base << nextCol << diag << nextRow;
        painter.setBrush(pressureToColor(value));
        painter.drawPolygon(top);
    }

    // Small orientation hint: the sensor name plus a colour scale legend.
    if (!title_.isEmpty())
    {
        painter.setPen(QColor(210, 214, 220));
        QFont f = painter.font();
        f.setBold(true);
        painter.setFont(f);
        painter.drawText(area.adjusted(2, 0, 0, 0), Qt::AlignTop | Qt::AlignLeft, title_);
    }

    const QRectF legend(area.left(), area.bottom() - 8, std::min(area.width() * 0.6, 120.0), 8.0);
    QLinearGradient grad(legend.topLeft(), legend.topRight());
    grad.setColorAt(0.0, pressureToColor(minPressure_));
    grad.setColorAt(0.5, pressureToColor(minPressure_ + (maxPressure_ - minPressure_) * 0.5f));
    grad.setColorAt(1.0, pressureToColor(maxPressure_));
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
    if (maxPressure_ > minPressure_)
        t = (value - minPressure_) / (maxPressure_ - minPressure_);
    t = std::clamp(t, 0.0f, 1.0f);

    int red = 0, green = 0, blue = 0;
    if (t < 0.33f)
    {
        const float k = t / 0.33f;
        red = 0;
        green = static_cast<int>(255.0f * k);
        blue = 255;
    }
    else if (t < 0.66f)
    {
        const float k = (t - 0.33f) / 0.33f;
        red = static_cast<int>(255.0f * k);
        green = 255;
        blue = static_cast<int>(255.0f * (1.0f - k));
    }
    else
    {
        const float k = (t - 0.66f) / 0.34f;
        red = 255;
        green = static_cast<int>(255.0f * (1.0f - k));
        blue = 0;
    }
    return QColor(red, green, blue);
}
