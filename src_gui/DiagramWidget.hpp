// DiagramWidget.hpp
#pragma once

#include <QWidget>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QMap>
#include <QScrollBar>
#include <QStringList>
#include <QVector>
#include <deque>

#include "grip/data_def.hpp"

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
    void updateSignalListFromHand(const HandStateMap &hand);

    // Render a time window [startIdxDeque .. endIdxDeque] from shared history
    void renderWindow(const std::deque<HandStateMap> &history,
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
    void onExportCsvClicked();

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
        QString motorName;
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

    /* The chart shows the newest window (m_maxPoints_, ~5 s), while the Y range
       additionally covers this many seconds of older history so the axis stays
       stable when a peak leaves the visible window. */
    static constexpr double kRangeLookbackSec = 10.0;

    /* Y-axis behaviour: the target range is recomputed every N-th frame only,
       the visible range then eases towards it (0 = snap, 1 = instant), and the
       target is padded by this ratio on each side (0.1 => +20 % total). */
    static constexpr int kRangeRefreshFrames = 10;
    static constexpr double kRangeEase = 0.25;
    static constexpr double kRangePadRatio = 0.2;

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

    /* Y-axis: cached target range + easing state. */
    double m_yTargetMin_{0.0};
    double m_yTargetMax_{0.0};
    bool m_yRangeValid_{false};
    bool m_rangeDirty_{true};
    unsigned int m_rangeFrame_{0};

    /* Shared history of the owning PlotPanel. The CSV export dumps all of it,
       not only the window that happens to be rendered. */
    const std::deque<HandStateMap> *m_history_{nullptr};
    double m_historyPeriodSec_{0.01};
    int m_historyOffsetSamples_{0};

    /* Type the current signal list was built for — the combo box can change
       before the list is rebuilt on the next incoming sample. */
    DiagramType m_signalListType_{DiagramType::JointPosition};

    void buildUi();
    bool isJointType() const;
    QColor colorForId(const QString &id) const;

    void clearSignalUi();
    void rebuildSignalList(const HandStateMap &hand);

    void ensureSeries(SignalInfo &sig);
    bool getValueForSignalAtSample(const SignalInfo &sig,
                                   const HandStateMap &hand,
                                   float &outValue) const;

    void updateModuleHeaderState(const QString &moduleName);
    void updateGlobalHeaderState();
    void timeRangeChanged(int start);

    /* CSV dump of the whole recorded history (may throw — never call directly
       from a slot, go through onExportCsvClicked()). */
    void exportCsv();
};
