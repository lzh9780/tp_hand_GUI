#pragma once

#include <QFrame>
#include <QVector>
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
 * pressure matrix as a point cloud, the right cell plots the force derived from
 * the same matrix over time.
 *
 *   +----------------------+----------------------+
 *   | Thumb                | Thumb                |
 *   |  point cloud         |  force plot          |
 *   +----------------------+----------------------+
 *   | Index                | Index                |
 *   ...
 *
 * Sensors are declared by name so the panel can be built before the first
 * telemetry frame arrives; unknown sensors reported later are appended.
 */
class TactilePanel : public QFrame
{
    Q_OBJECT
public:
    /* Must match TactileVisual::PressureMatrix / HandPressureData. */
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

    explicit TactilePanel(QWidget *parent = nullptr);

    /* Declare the sensors shown by the panel, in order. */
    void setSensors(const QStringList &sensorNames);
    /* Push one frame; sensors not declared yet are added on the fly. */
    void setHandPressureData(const HandPressureData &data);
    /* Raise the full-scale pressure used by the colour / height mapping. */
    void setPressureRange(float minPressure, float maxPressure);
    /* Drop every accumulated force history (on connect / disconnect). */
    void resetHistory();

private:
    struct SensorRow
    {
        TactilePointCloud *cloud{nullptr};
        TactileForcePlot *plot{nullptr};
    };

    SensorRow *rowFor(const QString &sensor);

    QGridLayout *grid_{nullptr};
    std::unordered_map<std::string, SensorRow> rows_;
    QStringList order_;

    float minPressure_{0.0f};
    float maxPressure_{30.0f};
};
