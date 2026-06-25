#pragma once

#include <QWidget>
#include <QPixmap>
#include <QVector>
#include <QMap>
#include <QRectF>
#include <QString>

class TactileVisual : public QWidget
{
    Q_OBJECT

public:
    enum class Finger
    {
        Thumb,
        Index,
        Middle,
        Ring,
        Pinky
    };

    struct PressureMatrix
    {
        int rows = 1;
        int cols = 1;
        QVector<float> values;
    };

    struct HandPressureData
    {
        PressureMatrix thumb;
        PressureMatrix index;
        PressureMatrix middle;
        PressureMatrix ring;
        PressureMatrix pinky;
    };

    explicit TactileVisual(QWidget *parent = nullptr);

    void setPalmImage(const QPixmap &pixmap);

    void setPressureRange(float minPressure, float maxPressure);

    void setFingerPressure(Finger finger, const PressureMatrix &pressure);
    void setHandPressureData(const HandPressureData &data);

    void setShowText(bool enable);
    void setShowGrid(bool enable);
    void setShowCenterOfPressure(bool enable);

protected:
    void paintEvent(QPaintEvent *event) override;
    QSize sizeHint() const override;

private:
    struct FingerDrawInfo
    {
        QString name;

        /*
         * 归一化坐标，范围 0~1。
         * 表示压力区域在手掌图片中的相对位置。
         *
         * x = rect.x() * imageWidth
         * y = rect.y() * imageHeight
         * w = rect.width() * imageWidth
         * h = rect.height() * imageHeight
         */
        QRectF normalizedRect;
    };

private:
    QRectF imageDrawRect() const;

    QRectF fingerRectInWidget(Finger finger, const QRectF &imageRect) const;

    QColor pressureToColor(float value) const;

    void drawPalmImage(QPainter &painter, const QRectF &imageRect);
    void drawFingerPressure(QPainter &painter,
                            Finger finger,
                            const QRectF &imageRect);

    void drawPressureMatrix(QPainter &painter,
                            const PressureMatrix &matrix,
                            const QRectF &targetRect);

    void drawCenterOfPressure(QPainter &painter,
                              const PressureMatrix &matrix,
                              const QRectF &targetRect);

    float matrixValue(const PressureMatrix &matrix, int row, int col) const;
    float matrixAverage(const PressureMatrix &matrix) const;
    float matrixMax(const PressureMatrix &matrix) const;

private:
    QPixmap palmPixmap_;

    QMap<Finger, FingerDrawInfo> fingerInfo_;
    QMap<Finger, PressureMatrix> pressureData_;

    float minPressure_ = 0.0f;
    float maxPressure_ = 30.0f;

    bool showText_ = true;
    bool showGrid_ = false;
    bool showCenterOfPressure_ = true;
};
