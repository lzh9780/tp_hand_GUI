#include "TactileVisual.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QFont>
#include <QPen>
#include <algorithm>
#include <cmath>

TactileVisual::TactileVisual(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(500, 500);

    /*
     * 默认加载手掌图片。
     * 如果你不用 Qt Resource，也可以在外部调用 setPalmImage(QPixmap(path))。
     */
    palmPixmap_ = QPixmap(":/images/resources/hand_palm.png");

    /*
     * 五个指尖压力区域的位置。
     *
     * 这里使用归一化坐标，和 widget 实际大小无关。
     * 你需要根据你的手掌图片实际调整这些数字。
     *
     * QRectF(x, y, w, h)
     *
     * x, y 是左上角相对图片宽高的位置。
     * w, h 是压力显示区域相对图片宽高的大小。
     */
    fingerInfo_[Finger::Thumb] = {
        "Thumb",
        QRectF(0.75, 0.34, 0.075, 0.13)};

    fingerInfo_[Finger::Index] = {
        "Index",
        QRectF(0.60, 0.34, 0.075, 0.13)};

    fingerInfo_[Finger::Middle] = {
        "Middle",
        QRectF(0.45, 0.34, 0.075, 0.13)};

    fingerInfo_[Finger::Ring] = {
        "Ring",
        QRectF(0.30, 0.34, 0.075, 0.13)};

    fingerInfo_[Finger::Pinky] = {
        "Pinky",
        QRectF(0.15, 0.34, 0.075, 0.13)};

    /*
     * 当前压力为 1x1。
     * 默认初始化为 0。
     */
    PressureMatrix zero;
    zero.rows = 1;
    zero.cols = 1;
    zero.values = {0.0f};

    pressureData_[Finger::Thumb] = zero;
    pressureData_[Finger::Index] = zero;
    pressureData_[Finger::Middle] = zero;
    pressureData_[Finger::Ring] = zero;
    pressureData_[Finger::Pinky] = zero;
}

void TactileVisual::setPalmImage(const QPixmap &pixmap)
{
    palmPixmap_ = pixmap;
    update();
}

void TactileVisual::setPressureRange(float minPressure, float maxPressure)
{
    minPressure_ = minPressure;
    maxPressure_ = maxPressure;
    update();
}

void TactileVisual::setFingerPressure(Finger finger,
                                           const PressureMatrix &pressure)
{
    if (pressure.rows <= 0 || pressure.cols <= 0)
        return;

    if (pressure.values.size() != pressure.rows * pressure.cols)
        return;

    pressureData_[finger] = pressure;
    update();
}

void TactileVisual::setHandPressureData(const HandPressureData &data)
{
    setFingerPressure(Finger::Thumb, data.thumb);
    setFingerPressure(Finger::Index, data.index);
    setFingerPressure(Finger::Middle, data.middle);
    setFingerPressure(Finger::Ring, data.ring);
    setFingerPressure(Finger::Pinky, data.pinky);
}

void TactileVisual::setShowText(bool enable)
{
    showText_ = enable;
    update();
}

void TactileVisual::setShowGrid(bool enable)
{
    showGrid_ = enable;
    update();
}

void TactileVisual::setShowCenterOfPressure(bool enable)
{
    showCenterOfPressure_ = enable;
    update();
}

QSize TactileVisual::sizeHint() const
{
    return QSize(600, 600);
}

void TactileVisual::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    painter.fillRect(rect(), QColor(30, 30, 30));

    QRectF imgRect = imageDrawRect();

    drawPalmImage(painter, imgRect);

    drawFingerPressure(painter, Finger::Thumb, imgRect);
    drawFingerPressure(painter, Finger::Index, imgRect);
    drawFingerPressure(painter, Finger::Middle, imgRect);
    drawFingerPressure(painter, Finger::Ring, imgRect);
    drawFingerPressure(painter, Finger::Pinky, imgRect);
}

QRectF TactileVisual::imageDrawRect() const
{
    QRectF widgetRect = rect().adjusted(10, 10, -10, -10);

    if (palmPixmap_.isNull())
    {
        return widgetRect;
    }

    QSizeF imageSize = palmPixmap_.size();

    double scale = std::min(widgetRect.width() / imageSize.width(),
                            widgetRect.height() / imageSize.height());

    double drawW = imageSize.width() * scale;
    double drawH = imageSize.height() * scale;

    double x = widgetRect.center().x() - drawW * 0.5;
    double y = widgetRect.center().y() - drawH * 0.5;

    return QRectF(x, y, drawW, drawH);
}

void TactileVisual::drawPalmImage(QPainter &painter,
                                       const QRectF &imageRect)
{
    if (!palmPixmap_.isNull())
    {
        painter.drawPixmap(imageRect.toRect(), palmPixmap_);
    }
    else
    {
        /*
         * 没有图片时，画一个简单手掌占位图，方便调试。
         */
        painter.setPen(QPen(Qt::white, 2));
        painter.setBrush(QColor(80, 80, 80));
        painter.drawRoundedRect(imageRect, 30, 30);

        painter.setPen(Qt::white);
        painter.drawText(imageRect, Qt::AlignCenter, "No Palm Image");
    }
}

QRectF TactileVisual::fingerRectInWidget(Finger finger,
                                              const QRectF &imageRect) const
{
    if (!fingerInfo_.contains(finger))
    {
        return QRectF();
    }

    QRectF nr = fingerInfo_[finger].normalizedRect;

    double x = imageRect.left() + nr.x() * imageRect.width();
    double y = imageRect.top() + nr.y() * imageRect.height();
    double w = nr.width() * imageRect.width();
    double h = nr.height() * imageRect.height();

    return QRectF(x, y, w, h);
}

void TactileVisual::drawFingerPressure(QPainter &painter,
                                            Finger finger,
                                            const QRectF &imageRect)
{
    if (!pressureData_.contains(finger))
        return;

    QRectF targetRect = fingerRectInWidget(finger, imageRect);
    if (!targetRect.isValid())
        return;

    const PressureMatrix &matrix = pressureData_[finger];

    /*
     * 用椭圆裁剪压力显示区域。
     * 当前 1x1 时显示为一个圆形/椭圆色块。
     * 未来 mxn 时，矩阵热力图会被限制在这个椭圆内部。
     */
    QPainterPath clipPath;
    clipPath.addEllipse(targetRect);

    painter.save();
    painter.setClipPath(clipPath);

    drawPressureMatrix(painter, matrix, targetRect);

    painter.restore();

    /*
     * 外边框
     */
    painter.setPen(QPen(QColor(255, 255, 255, 180), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(targetRect);

    if (showCenterOfPressure_)
    {
        drawCenterOfPressure(painter, matrix, targetRect);
    }

    if (showText_)
    {
        QString name = fingerInfo_.contains(finger)
                           ? fingerInfo_[finger].name
                           : QString("Finger");

        float avgPressure = matrixAverage(matrix);
        float maxPressure = matrixMax(matrix);

        QString text = QString("%1\nAvg:%2\nMax:%3")
                           .arg(name)
                           .arg(avgPressure, 0, 'f', 1)
                           .arg(maxPressure, 0, 'f', 1);

        QRectF textRect(targetRect.left() - targetRect.width() * 0.3,
                        targetRect.bottom() + 4,
                        targetRect.width() * 1.6,
                        48);

        painter.setPen(QColor(0, 0, 0));
        painter.setFont(QFont("Arial", 8));
        painter.drawText(textRect, Qt::AlignCenter, text);
    }
}

void TactileVisual::drawPressureMatrix(QPainter &painter,
                                            const PressureMatrix &matrix,
                                            const QRectF &targetRect)
{
    if (matrix.rows <= 0 || matrix.cols <= 0)
        return;

    if (matrix.values.size() != matrix.rows * matrix.cols)
        return;

    double cellW = targetRect.width() / matrix.cols;
    double cellH = targetRect.height() / matrix.rows;

    for (int r = 0; r < matrix.rows; ++r)
    {
        for (int c = 0; c < matrix.cols; ++c)
        {
            float value = matrixValue(matrix, r, c);
            QColor color = pressureToColor(value);

            QRectF cell(targetRect.left() + c * cellW,
                        targetRect.top() + r * cellH,
                        cellW,
                        cellH);

            painter.fillRect(cell, color);
        }
    }

    if (showGrid_ && matrix.rows > 1 && matrix.cols > 1)
    {
        painter.setPen(QPen(QColor(255, 255, 255, 80), 1));

        for (int r = 1; r < matrix.rows; ++r)
        {
            double y = targetRect.top() + r * cellH;
            painter.drawLine(QPointF(targetRect.left(), y),
                             QPointF(targetRect.right(), y));
        }

        for (int c = 1; c < matrix.cols; ++c)
        {
            double x = targetRect.left() + c * cellW;
            painter.drawLine(QPointF(x, targetRect.top()),
                             QPointF(x, targetRect.bottom()));
        }
    }
}

void TactileVisual::drawCenterOfPressure(QPainter &painter,
                                              const PressureMatrix &matrix,
                                              const QRectF &targetRect)
{
    if (matrix.rows <= 0 || matrix.cols <= 0)
        return;

    if (matrix.values.size() != matrix.rows * matrix.cols)
        return;

    double sum = 0.0;
    double cx = 0.0;
    double cy = 0.0;

    for (int r = 0; r < matrix.rows; ++r)
    {
        for (int c = 0; c < matrix.cols; ++c)
        {
            float p = matrixValue(matrix, r, c);

            if (p <= 0.0f)
                continue;

            sum += p;
            cx += static_cast<double>(c) * p;
            cy += static_cast<double>(r) * p;
        }
    }

    if (sum <= 1e-6)
        return;

    cx /= sum;
    cy /= sum;

    /*
     * 对于 1x1，中心点自然在中心。
     * 对于 mxn，中心点是 COP。
     */
    double normX = 0.5;
    double normY = 0.5;

    if (matrix.cols > 1)
    {
        normX = cx / static_cast<double>(matrix.cols - 1);
    }

    if (matrix.rows > 1)
    {
        normY = cy / static_cast<double>(matrix.rows - 1);
    }

    QPointF cop(targetRect.left() + normX * targetRect.width(),
                targetRect.top() + normY * targetRect.height());

    painter.setPen(QPen(QColor(0, 0, 0, 220), 2));
    painter.setBrush(QColor(255, 255, 255, 220));

    painter.drawEllipse(cop, 4, 4);

    painter.drawLine(QPointF(cop.x() - 7, cop.y()),
                     QPointF(cop.x() + 7, cop.y()));

    painter.drawLine(QPointF(cop.x(), cop.y() - 7),
                     QPointF(cop.x(), cop.y() + 7));
}

QColor TactileVisual::pressureToColor(float value) const
{
    float t = 0.0f;

    if (maxPressure_ > minPressure_)
    {
        t = (value - minPressure_) / (maxPressure_ - minPressure_);
    }

    t = std::clamp(t, 0.0f, 1.0f);

    /*
     * 颜色映射：
     *
     * 0.0   → 蓝色，低压力
     * 0.33  → 绿色
     * 0.66  → 黄色
     * 1.0   → 红色，高压力
     */
    int red = 0;
    int green = 0;
    int blue = 0;

    if (t < 0.33f)
    {
        float k = t / 0.33f;
        red = 0;
        green = static_cast<int>(255.0f * k);
        blue = 255;
    }
    else if (t < 0.66f)
    {
        float k = (t - 0.33f) / 0.33f;
        red = static_cast<int>(255.0f * k);
        green = 255;
        blue = static_cast<int>(255.0f * (1.0f - k));
    }
    else
    {
        float k = (t - 0.66f) / 0.34f;
        red = 255;
        green = static_cast<int>(255.0f * (1.0f - k));
        blue = 0;
    }

    /*
     * 压力越大，透明度越高。
     * 低压力也保留一定透明度，避免完全看不见。
     */
    int alpha = static_cast<int>(60.0f + 180.0f * t);

    return QColor(red, green, blue, alpha);
}

float TactileVisual::matrixValue(const PressureMatrix &matrix,
                                      int row,
                                      int col) const
{
    int index = row * matrix.cols + col;

    if (index < 0 || index >= matrix.values.size())
        return 0.0f;

    return matrix.values[index];
}

float TactileVisual::matrixAverage(const PressureMatrix &matrix) const
{
    if (matrix.values.isEmpty())
        return 0.0f;

    double sum = 0.0;

    for (float v : matrix.values)
    {
        sum += v;
    }

    return static_cast<float>(sum / matrix.values.size());
}

float TactileVisual::matrixMax(const PressureMatrix &matrix) const
{
    if (matrix.values.isEmpty())
        return 0.0f;

    float maxValue = matrix.values[0];

    for (float v : matrix.values)
    {
        if (v > maxValue)
        {
            maxValue = v;
        }
    }

    return maxValue;
}
