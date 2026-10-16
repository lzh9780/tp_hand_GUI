#pragma once
#include <QFrame>
#include <QList>
#include <QHash>
#include <QStringList>
#include <QJsonArray>
#include <QElapsedTimer>
#include "grip/settings.hpp"
#include "grip/data_def.hpp"
#include "LedIndicator.hpp"

class QSlider;
class QLabel;
class QScrollArea;
class QWidget;
class QVBoxLayout;
class QSplitter;
class QPushButton;
class QToolButton;
class QLineEdit;
class QImage;
class QComboBox;
class QLayoutItem;
class QCheckBox;

/**
 * SidePanel (GUI)
 * ---------------
 * "Joint control" panel: one slider row per actuated joint, with a numeric
 * entry, the limit labels and a per-joint status LED.
 *
 * The panel is deliberately limited to joint targets now (UI design notes #6
 * and #7):
 *   - the joint control-mode selector is gone; the connection bar's
 *     Enable/Disable button drives the whole hand between `joint_position`
 *     and `zero_torque`,
 *   - the joint feedback table is gone; bus telemetry moved to the log panel.
 *
 * The action library lives in ActionPanel; this panel only exposes the joint
 * targets.
 *
 * Emits:
 *  - valuesChanged(): a slider moved (targets are pushed to the hand immediately)
 *  - committed(): Execute clicked (batch of targets)
 *  - notify(text): status lines for MainWindow logging
 */
class SidePanel : public QFrame {
    Q_OBJECT
public:
    explicit SidePanel(QWidget *parent);

    // Read current slider values (angles in degrees, x100)
    QList<int> jointDegrees() const;

    void applyCameraData(std::unordered_map<std::string, float>& data, grip::HandSettings *hand_cfg);
    void loadHandControllor(grip::HandSettings *hand_cfg);
    void removeAll();
    void applyActionValues(const QVector<double> &values);

    // Set every joint either active (joint_position) or passive (zero_torque).
    // Drives slider enablement and the per-joint control mode list.
    void setAllJointsEnabled(bool enabled);

    QStringList getControlModes();

signals:
    void valuesChanged();
    void notify(const QString& msg);   // status/log message

private slots:
    void onAnySliderChanged();
    void onReset();
    void onValueEditChanged();

private:
    // Build sections
    void buildSliderPanel();

    // Helpers
    void loadSliderModule(QString name, float min, float max);
    void rebuildSliderHost();

private:
    // UI
    QPushButton* btnExecute_{nullptr};

    // joint name -> index into sliders_/currValueLabels_/leds_
    QHash<QString, int> sliderIndex_;
    // joint name -> hall-position scale factor, computed once per session
    std::unordered_map<std::string, float> hallScales_;

    // UI: sliders
    QWidget* sliderPanel_{nullptr};
    QScrollArea* sliderScroll_{nullptr};
    QVBoxLayout* sliderVBox_{nullptr};
    QList<QSlider*> sliders_{};
    QList<QLineEdit*>  valueLabels_{};  // number next to each slider
    QList<QLabel*>  currValueLabels_;
    QList<QLabel*>  nameLabels_;
    QList<LedIndicator*> leds_;
    QList<std::pair<QLabel*, QLabel*>> valueLbls;
    QStringList control_modes;
};
