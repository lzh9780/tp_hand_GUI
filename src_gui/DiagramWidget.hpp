// DiagramWidget.hpp
#pragma once

#include <QWidget>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QMap>
#include <QScrollBar>
#include <deque>

#include "grip/can_defs.hpp"

QT_CHARTS_USE_NAMESPACE

class QComboBox;
class QVBoxLayout;
class QCheckBox;
class QLabel;
class QPushButton;
class QScrollBar;

class DiagramWidget : public QWidget
{
    Q_OBJECT
public:
    explicit DiagramWidget(QWidget *parent = nullptr);

    void setMaxPoints(int n) { m_maxPoints_ = n; }
    void setSamplePeriod(double s) { m_samplePeriodSec_ = s; }
    void setLiveMode(bool isLive);

    // Called only to build modules / checkboxes from the *latest* hand state
    void updateSignalListFromHand(const grip::HandStateMap &hand);

    // Render a time window [startIdxDeque .. endIdxDeque] from shared history
    void renderWindow(const std::deque<grip::HandStateMap> &history,
                      double samplePeriodSec,
                      int historyOffsetSamples,  // totalSamples - history.size()
                      int startIdxDeque,         // inclusive, in deque indices
                      int endIdxDeque,           // inclusive
                      double xMin, double xMax); // common x-axis for this panel

signals:
    void requestClose(DiagramWidget *self);

private slots:
    void onTypeChanged(int index);
    void onSignalToggled(bool checked);
    void onGlobalClicked(bool checked);
    void onModuleHeaderClicked(bool checked);
    void onModuleToggleClicked(bool checked);
    void onResetZoomClicked();

private:
    enum class DiagramType
    {
        JointPosition = 0,
        JointVelocity,
        MotorPosition,
        MotorVelocity,
        MotorCurrent
    };

    struct SignalInfo
    {
        QString id;
        QString moduleName;
        QString jointName;
        int motorId{-1};
        bool isJoint{true};

        QCheckBox *checkBox{nullptr};
        QLineSeries *series{nullptr};
    };

    struct ModuleUi
    {
        QString name;
        QCheckBox *headerCheck{nullptr};    // tri-state “select all in module”
        QPushButton *toggleButton{nullptr}; // show/hide signals
        QWidget *signalsWidget{nullptr};
        QVBoxLayout *signalsLayout{nullptr};
        QStringList signalIds;
    };

    DiagramType m_type_{DiagramType::JointPosition};
    bool m_checkboxesDirty_{true};

    int m_maxPoints_{250};
    double m_samplePeriodSec_{0.01};

    // UI
    QComboBox *m_typeCombo_{nullptr};
    QChart *m_chart_{nullptr};
    QChartView *m_chartView_{nullptr};
    QValueAxis *m_axisX_{nullptr};
    QValueAxis *m_axisY_{nullptr};

    QWidget *m_checksContainer_{nullptr};
    QVBoxLayout *m_checksLayout_{nullptr};
    QCheckBox *m_globalCheck_{nullptr};
    QLabel *m_emptyLabel_{nullptr};
    QScrollBar *chartHistoryScroll_{nullptr};

    QMap<QString, SignalInfo> m_signals_;
    QMap<QString, ModuleUi> m_modules_;
    bool m_updatingFromHeader_{false};
    bool updateRequest_{false};
    bool m_isLive_{true};

    void buildUi();
    bool isJointType() const;
    QColor colorForId(const QString &id) const;

    void clearSignalUi();
    void rebuildSignalList(const grip::HandStateMap &hand);

    void ensureSeries(SignalInfo &sig);
    bool getValueForSignalAtSample(const SignalInfo &sig,
                                   const grip::HandStateMap &hand,
                                   float &outValue) const;

    void updateModuleHeaderState(const QString &moduleName);
    void updateGlobalHeaderState();
    void timeRangeChanged(int start);
};
