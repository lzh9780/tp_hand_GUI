#pragma once

#include <QWidget>
#include <QVector>
#include <QPointF>
#include <QString>

/**
 * TactilePointCloud (GUI)
 * -----------------------
 * Left half of one tactile sensor pair: the contact points configured for the
 * sensor (2D layout from the tactile config) rendered on a plane whose origin
 * (0, 0) sits at the centre of the widget.
 *
 * The sensor reports a scalar pressure reading; every configured point carries
 * the same value, and the colour of each point follows the pressure. The
 * related_link / link_offset fields of the config are deliberately ignored:
 * only the 2D contact coordinates matter here.
 */
class TactilePointCloud : public QWidget
{
    Q_OBJECT
public:
    explicit TactilePointCloud(QWidget *parent = nullptr);

    /* Sensor label drawn in the corner (e.g. "Thumb Tip"). */
    void setTitle(const QString &title);
    /* Configure the 2D point layout (sensor-local coordinates, z dropped). */
    void setPoints(const QVector<QPointF> &points);
    /* Push the current pressure reading for the sensor. */
    void setPressure(float value);
    /* Full-scale value used by the colour mapping (fallback ceiling). */
    void setPressureRange(float minPressure, float maxPressure);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QColor pressureToColor(float value) const;

    QString title_;
    QVector<QPointF> points_;

    float value_{0.0f};

    float minPressure_{0.0f};
    float maxPressure_{30.0f};

    /* Auto-tracked full scale (decayed) so the colour map adapts to the raw
       range reported by the sensor; the configured range is only a ceiling. */
    float peak_{1.0f};
};
