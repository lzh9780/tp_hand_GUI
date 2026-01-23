// PlotPanel.hpp
#pragma once

#include <QWidget>
#include <QVector>
#include <QSplitter>
#include <QTimer>
#include <QComboBox>
#include <QToolButton>
#include <QPushButton>
#include <deque>

#include "DiagramWidget.hpp"
#include "grip/can_defs.hpp"

class PlotPanel : public QWidget
{
    Q_OBJECT
public:
    explicit PlotPanel(QWidget *parent = nullptr);

    // Called by MainWindow’s timer
    void feedData(const grip::HandStateMap &hand);

    void setUpdateTimer(QTimer *timer);

private slots:
    void onAddDiagram();
    void onFrequencyChanged(int index);
    void onLiveToggled(bool on);
    void onStartToggled(bool on);
    void onDiagramRequestClose(DiagramWidget *which);

private:
    QPushButton *m_addButton_{nullptr};
    QComboBox *m_freqCombo_{nullptr};
    QToolButton *m_liveButton_{nullptr};
    QToolButton *m_startButton_{nullptr};
    QToolButton *m_clearButton_{nullptr};
    QSplitter *m_splitter_{nullptr};
    QVector<DiagramWidget *> m_diagrams_;
    QTimer *m_timer_{nullptr};

    int m_maxPoints_{250};
    bool m_liveMode_{true};
    bool m_startMode_{false};

    // Shared history for this panel
    std::deque<grip::HandStateMap> history_;
    int historyLimit_{5000};
    long long totalSamples_{0}; // total samples ever seen
    double samplePeriodSec_{0.02};
};
