#pragma once

#include <QFrame>
#include <QVector>
#include <QPointF>
#include <QList>
#include <QString>
#include <QStringList>
#include <unordered_map>

class QGridLayout;
class QLabel;

class TactilePointCloud;
class TactileForcePlot;

/**
 * TactilePanel (GUI)
 * ------------------
 * One row per tactile sensor (UI design note #10): the left cell renders the
 * contact points configured for the sensor as a 2D point cloud, the right cell
 * plots the pressure reported by the same sensor over time.
 *
 *   +----------------------+----------------------+
 *   | Thumb Tip            | Thumb Tip            |
 *   |  point cloud (2D)    |  force plot          |
 *   +----------------------+----------------------+
 *   | Index Tip            | Index Tip            |
 *   ...
 *
 * The sensor list and each sensor's 2D contact layout come from the tactile
 * section of the hand config ("tactiles" -> TactileConfig::point_coord), so
 * the panel is built from the config instead of a hard-coded finger list.
 * related_link / link_offset are not used.
 */
class TactilePanel : public QFrame
{
    Q_OBJECT
public:
    /* Sensor geometry from the config: name + 2D contact points (z ignored).
       (0, 0) is rendered at the centre of the point-cloud panel. */
    struct SensorLayout
    {
        QString name;
        QVector<QPointF> points;
    };

    /* One telemetry frame: a scalar pressure reading per configured sensor. */
    struct SensorFrame
    {
        QString name;
        float value = 0.0f;
    };

    explicit TactilePanel(QWidget *parent = nullptr);

    /* (Re)build the sensor rows from the config layouts. */
    void setSensors(const QVector<SensorLayout> &sensors);
    /* Push one frame; unknown sensor names are ignored. */
    void setHandPressureData(const QVector<SensorFrame> &frames);
    /* Raise the full-scale pressure used by the colour mapping. */
    void setPressureRange(float minPressure, float maxPressure);
    /* Drop every accumulated force history (on connect / disconnect). */
    void resetHistory();

private:
    struct SensorRow
    {
        TactilePointCloud *cloud{nullptr};
        TactileForcePlot *plot{nullptr};
        QVector<QPointF> points;
    };

    SensorRow *rowFor(const QString &sensor);

    QGridLayout *grid_{nullptr};
    std::unordered_map<std::string, SensorRow> rows_;
    QStringList order_;

    float minPressure_{0.0f};
    float maxPressure_{30.0f};
};
