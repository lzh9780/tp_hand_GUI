#pragma once

#include <QWidget>
#include <QVector>
#include <QPointF>
#include <QString>

/**
 * TactilePointCloud (GUI)
 * -----------------------
 * Left half of one tactile sensor pair: the raw pressure distribution rendered
 * as a 3D point cloud (isometric projection of the pressure matrix).
 *
 * The sensor reports a flat pressure matrix; each cell maps onto a point whose
 * height is the pressure value, which makes the shape of the contact visible at
 * a glance. No OpenGL context is needed, so the widget works on every platform
 * the rest of the GUI supports.
 */
class TactilePointCloud : public QWidget
{
    Q_OBJECT
public:
    // Must match TactileVisual::PressureMatrix.
    struct PressureMatrix
    {
        int rows = 1;
        int cols = 1;
        QVector<float> values;
    };

    explicit TactilePointCloud(QWidget *parent = nullptr);

    /* Sensor label drawn in the corner (e.g. "Thumb"). */
    void setTitle(const QString &title);
    /* Push a new pressure frame. */
    void setPressureMatrix(const PressureMatrix &matrix);
    /* Full-scale value used by the colour / height mapping. */
    void setPressureRange(float minPressure, float maxPressure);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    float cellValue(int row, int col) const;
    QColor pressureToColor(float value) const;

    QString title_;
    PressureMatrix matrix_{};

    float minPressure_{0.0f};
    float maxPressure_{30.0f};
};
