// PlotPanel.cpp
#include "PlotPanel.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <algorithm>

using grip::HandStateMap;

PlotPanel::PlotPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);

    auto *topRow = new QHBoxLayout;
    m_addButton_ = new QPushButton(tr("+ Add diagram"), this);
    topRow->addWidget(m_addButton_, 0);

    auto *freqLabel = new QLabel(tr("Update:"), this);
    m_freqCombo_ = new QComboBox(this);
    // m_freqCombo_->addItem("10 ms", 10);
    m_freqCombo_->addItem("20 ms", 20);
    m_freqCombo_->addItem("50 ms", 50);
    m_freqCombo_->addItem("100 ms", 100);
    m_freqCombo_->setCurrentIndex(0);

    topRow->addSpacing(8);
    topRow->addWidget(freqLabel);
    topRow->addWidget(m_freqCombo_);

    m_liveButton_ = new QToolButton(this);
    m_liveButton_->setText("Live");
    m_liveButton_->setCheckable(true);
    m_liveButton_->setChecked(true);
    topRow->addSpacing(8);
    topRow->addWidget(m_liveButton_);

    m_startButton_ = new QToolButton(this);
    m_startButton_->setText("Start");
    m_startButton_->setCheckable(true);
    m_startButton_->setChecked(false);
    topRow->addSpacing(8);
    topRow->addWidget(m_startButton_);

    topRow->addStretch();
    root->addLayout(topRow);

    m_splitter_ = new QSplitter(Qt::Vertical, this);
    root->addWidget(m_splitter_, 1);

    connect(m_addButton_, &QPushButton::clicked,
            this, &PlotPanel::onAddDiagram);
    connect(m_freqCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PlotPanel::onFrequencyChanged);
    connect(m_liveButton_, &QToolButton::toggled,
            this, &PlotPanel::onLiveToggled);
    connect(m_startButton_, &QToolButton::toggled,
            this, &PlotPanel::onStartToggled);
}

void PlotPanel::setUpdateTimer(QTimer *timer)
{
    m_timer_ = timer;
    if (!m_timer_ || !m_freqCombo_)
        return;

    int ms = m_freqCombo_->currentData().toInt();
    m_timer_->setInterval(ms);
    samplePeriodSec_ = ms / 1000.0;

    for (auto *d : m_diagrams_)
    {
        d->setSamplePeriod(samplePeriodSec_);
        d->setMaxPoints(m_maxPoints_);
    }
}

void PlotPanel::onAddDiagram()
{
    auto *diag = new DiagramWidget(m_splitter_);
    diag->setMaxPoints(m_maxPoints_);
    diag->setSamplePeriod(samplePeriodSec_);

    m_splitter_->addWidget(diag);
    m_diagrams_.append(diag);

    connect(diag, &DiagramWidget::requestClose,
            this, &PlotPanel::onDiagramRequestClose);
}

void PlotPanel::onDiagramRequestClose(DiagramWidget *which)
{
    auto it = std::find(m_diagrams_.begin(), m_diagrams_.end(), which);
    if (it != m_diagrams_.end())
        m_diagrams_.erase(it);
    which->deleteLater();
}

void PlotPanel::onFrequencyChanged(int)
{
    if (!m_timer_ || !m_freqCombo_)
        return;

    int ms = m_freqCombo_->currentData().toInt();
    m_timer_->setInterval(ms);
    samplePeriodSec_ = ms / 1000.0;
    m_maxPoints_ = 5.0 / samplePeriodSec_;
    history_.clear();
    totalSamples_ = 0;

    for (auto *d : m_diagrams_)
    {
        d->setSamplePeriod(samplePeriodSec_);
        d->setMaxPoints(m_maxPoints_);
    }
}

void PlotPanel::onLiveToggled(bool on)
{
    m_liveMode_ = on;
    m_liveButton_->setText(on ? "Live" : "History");
    for (auto *d : m_diagrams_)
        d->setLiveMode(on);
}

void PlotPanel::onStartToggled(bool on)
{
    m_startMode_ = on;
    m_startButton_->setText(on ? "Stop" : "Start");
    if (on)
    {
        history_.clear();
        totalSamples_ = 0;
    }
}

void PlotPanel::feedData(const HandStateMap &hand)
{
    // Update history
    if (m_startMode_)
    {
        history_.push_back(hand);
        ++totalSamples_;
        if (static_cast<int>(history_.size()) > historyLimit_)
        {
            history_.pop_front();
        }
    }

    // Let each diagram update its signal list from the latest hand
    for (auto *d : m_diagrams_)
    {
        d->updateSignalListFromHand(hand);
    }

    if (history_.empty() || m_diagrams_.isEmpty())
        return;

    const int historySize = static_cast<int>(history_.size());
    const int lastDequeIdx = historySize - 1;
    const int historyOffsetSamples = static_cast<int>(totalSamples_ - historySize); // base global index (0-based)

    int startDequeIdx = 0;
    int endDequeIdx = lastDequeIdx;

    // Live: show only last maxPoints_ samples
    if (historySize > m_maxPoints_)
    {
        startDequeIdx = historySize - m_maxPoints_;
    }
    else
    {
        startDequeIdx = 0;
    }
    endDequeIdx = lastDequeIdx;

    const int startGlobalIdx = historyOffsetSamples + startDequeIdx;
    const int endGlobalIdx = historyOffsetSamples + endDequeIdx;

    const double xMin = (startGlobalIdx + 1) * samplePeriodSec_; // 1-based
    const double xMax = (endGlobalIdx + 1) * samplePeriodSec_;

    for (auto *d : m_diagrams_)
    {
        d->renderWindow(history_,
                        samplePeriodSec_,
                        historyOffsetSamples,
                        startDequeIdx,
                        endDequeIdx,
                        xMin, xMax);
    }
}
