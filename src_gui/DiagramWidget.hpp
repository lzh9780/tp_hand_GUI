#pragma once

#include <QWidget>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QMap>
#include <QVector>
#include <deque>

#include "grip/can_defs.hpp"

QT_CHARTS_USE_NAMESPACE

class QComboBox;
class QVBoxLayout;
class QCheckBox;
class QLabel;
class QPushButton;

using HandStateMap = std::unordered_map<std::string, grip::ModuleState>;

class DiagramWidget : public QWidget
{
    Q_OBJECT
public:
    explicit DiagramWidget(QWidget *parent = nullptr);

    void setMaxPoints(int n) { m_maxPoints = n; }
    void updateFromHand(const HandStateMap &hand);

private slots:
    void onTypeChanged(int index);
    void onSignalToggled(bool checked);
    void onGlobalClicked(bool checked);
    void onModuleHeaderClicked(bool checked);
    void onModuleToggleClicked(bool checked);

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
        std::deque<float> values;
    };

    struct ModuleUi
    {
        QString name;
        QCheckBox *headerCheck{nullptr};    // tri-state “select all in module”
        QPushButton *toggleButton{nullptr}; // show/hide joints/motors
        QWidget *signalsWidget{nullptr};    // container for joint/motor boxes
        QVBoxLayout *signalsLayout{nullptr};
        QStringList signalIds; // ids belonging to this module
    };

    // configuration / state
    DiagramType m_type{DiagramType::JointPosition};
    bool m_checkboxesDirty{true};
    int m_maxPoints{200};

    // UI
    QComboBox *m_typeCombo{nullptr};
    QChart *m_chart{nullptr};
    QChartView *m_chartView{nullptr};
    QValueAxis *m_axisX{nullptr};
    QValueAxis *m_axisY{nullptr};

    QWidget *m_checksContainer{nullptr};
    QVBoxLayout *m_checksLayout{nullptr};
    QCheckBox *m_globalCheck{nullptr}; // tri-state global “select all”
    QLabel *m_emptyLabel{nullptr};

    // data / bookkeeping
    QMap<QString, SignalInfo> m_signals; // signal id -> info
    QMap<QString, ModuleUi> m_modules;   // module name -> UI info
    bool m_updatingFromHeader{false};    // avoid recursion

    // helpers
    void buildUi();
    bool isJointType() const;
    QColor colorForId(const QString &id) const;

    void clearSignalUi();
    void rebuildSignalList(const HandStateMap &hand);

    void ensureSeries(SignalInfo &sig);
    bool getValueForSignal(const SignalInfo &sig,
                           const HandStateMap &hand,
                           float &outValue) const;

    void updateModuleHeaderState(const QString &moduleName);
    void updateGlobalHeaderState();
};
