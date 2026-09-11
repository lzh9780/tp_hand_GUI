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
class QTableWidget;
class QTableWidgetItem;
class QCheckBox;
class QDoubleSpinBox;

/**
 * SidePanel (GUI)
 * ---------------
 * - Top:    Feedback section - live joint / motor telemetry in one table.
 * - Bottom: Slider area - one row per joint, labels + numeric readout + min/max.
 *
 * The action library lives in ActionPanel; this panel only renders feedback and
 * exposes the joint targets.
 *
 * Emits:
 *  - valuesChanged(): slider moved (send immediately in Follow mode)
 *  - committed(): Execute clicked (send batch in Setup mode)
 *  - notify(text): status lines for MainWindow logging
 *  - controlChanged(name, mode): global finger / wrist control mode changed
 *  - openAdvancePanel(): user asked for the per-joint motor panel
 *
 * NOTE: the feedback-filter settings now live in the Settings pop-up.
 */
class SidePanel : public QFrame {
    Q_OBJECT
public:
    explicit SidePanel(QWidget *parent);

    // Read current slider values (angles in degrees)
    QList<int> jointDegrees() const;

    // Mode string exactly as UI shows it ("Mode: Follow"/"Mode: Setup")
    QString modeText() const;
    void modeChange(bool teleopActive);
    void applyCameraData(std::unordered_map<std::string, float>& data, grip::HandSettings *hand_cfg);
    void loadHandControllor(grip::HandSettings *hand_cfg);
    void loadActuatorControllor(grip::ActuatorSettings act_cfg);
    std::string get_control_mode();
    void removeAll();
    void applyActionValues(const QVector<double> &values);
    void applyModeChange(grip::HandSettings *hand_cfg, QString name, QString mode);
    QStringList getControlModes();

    // --- feedback ---
    // Rebuild the feedback table for the connected hand.
    void loadFeedbackTable(grip::HandSettings *hand_cfg);
    // Push one telemetry snapshot into the table and the slider readouts.
    void updateFeedback(const HandStateMap &state);
    // Push the latest bus telemetry (voltage / current / temps / faults).
    void updateBusInfo(const BusData &bus);

signals:
    void valuesChanged();
    void committed();                  // Execute in Setup mode
    void notify(const QString& msg);   // status/log message
    void controlChanged(const QString& name, const QString& mode);
    void openAdvancePanel();

private slots:
    void onAnySliderChanged();
    void onToggleMode();
    void onExecute();
    void onReset();
    void onControlChanged();
    void onValueEditChanged();
    void onMaxVelocityChanged();
    void onMaxCurrentChanged();

private:
    // Build sections
    void buildFeedbackPanel();
    void buildSliderPanel();

    // Helpers
    void loadSliderModule(QString name, float min, float max);
    void rebuildSliderHost();

    // Feedback table helpers
    struct FeedbackRow
    {
        QTableWidgetItem *pos{nullptr};
        QTableWidgetItem *vel{nullptr};
        QTableWidgetItem *trq{nullptr};
        LedIndicator *led{nullptr};
    };
    void addFeedbackSection(const QString &title);
    FeedbackRow addFeedbackRow(const QString &name, const QString &kind, bool withLed);
    void clearFeedbackTable();
    void buildBusTable(QVBoxLayout *root);

private:
    // UI: control row
    QPushButton* btnMode_{nullptr};
    QPushButton* btnExecute_{nullptr};

    // UI: feedback
    QWidget* feedbackPanel_{nullptr};
    QTableWidget* feedbackTable_{nullptr};
    QElapsedTimer feedbackClock_;

    // Bus telemetry: Voltage | Current | Temp1 | Temp2 | Fault
    QTableWidget* busTable_{nullptr};
    // joint name -> table row cells (joint telemetry, no data for the wrist)
    QHash<QString, FeedbackRow> jointRows_;
    // joint name -> table row cells (motor telemetry)
    QHash<QString, FeedbackRow> motorRows_;
    // joint name -> index into sliders_/currValueLabels_/leds_
    QHash<QString, int> sliderIndex_;
    // joint name -> hall-position scale factor, computed once per session
    std::unordered_map<std::string, float> hallScales_;

    // UI: Image
    QLabel* img_label_{nullptr};

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

    // UI: actuator
    QComboBox* mode_box{nullptr};
    grip::ActuatorSettings act_cfg_;



    // cache joint count
    int jointCount_{0};
};
