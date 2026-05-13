// DiagramWidget.cpp
#include "DiagramWidget.hpp"

#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QPushButton>
#include <QToolButton>
#include <QPainter>
#include <QtMath>
#include <QSplitter>
#include <QDebug>

// using grip::HandStateMap;
// using grip::JointState;
// using grip::ModuleState;
// using grip::Telemetry;

DiagramWidget::DiagramWidget(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

void DiagramWidget::buildUi()
{
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(4, 4, 4, 4);
    rootLayout->setSpacing(4);

    // --- top: close, reset, type combo ---
    auto *topRow = new QHBoxLayout;

    auto *closeBtn = new QPushButton("×", this);
    closeBtn->setFixedSize(20, 20);
    topRow->addWidget(closeBtn);
    connect(closeBtn, &QPushButton::clicked, this, [this]()
            { emit requestClose(this); });

    auto *resetBtn = new QPushButton(tr("Reset"), this);
    resetBtn->setFixedHeight(20);
    topRow->addWidget(resetBtn);
    connect(resetBtn, &QPushButton::clicked,
            this, &DiagramWidget::onResetZoomClicked);

    auto *typeLabel = new QLabel(tr("Type:"), this);
    m_typeCombo_ = new QComboBox(this);
    m_typeCombo_->addItem(tr("Joint position"));
    m_typeCombo_->addItem(tr("Joint velocity"));
    m_typeCombo_->addItem(tr("Motor position"));
    m_typeCombo_->addItem(tr("Motor velocity"));
    m_typeCombo_->addItem(tr("Motor current"));

    topRow->addWidget(typeLabel);
    topRow->addWidget(m_typeCombo_, 1);
    topRow->addStretch();
    rootLayout->addLayout(topRow);

    // --- middle: chart + scroll area for checkboxes ---
    auto *middleRow = new QSplitter(Qt::Horizontal);
    auto *middleRowLyt = new QHBoxLayout(middleRow);
    rootLayout->addWidget(middleRow, 1);

    // Chart
    m_chart_ = new QChart;
    m_chart_->setTitle(tr("Telemetry"));
    m_chart_->legend()->setVisible(true);
    m_chart_->legend()->setAlignment(Qt::AlignBottom);

    m_axisX_ = new QValueAxis;
    m_axisX_->setTitleText(tr("Time [s]"));
    m_axisX_->setLabelFormat("%.0f");
    m_axisX_->setTickType(QValueAxis::TicksDynamic);
    m_axisX_->setTickInterval(1.0); // vertical grid every 1s
    m_axisX_->setMinorTickCount(0);

    m_axisY_ = new QValueAxis;
    m_axisY_->setTitleText(tr("Joint Pos [rad]"));

    m_chart_->addAxis(m_axisX_, Qt::AlignBottom);
    m_chart_->addAxis(m_axisY_, Qt::AlignLeft);

    m_chartView_ = new QChartView(m_chart_, this);
    m_chartView_->setRenderHint(QPainter::Antialiasing);
    m_chartView_->setRubberBand(QChartView::RectangleRubberBand);
    m_chartView_->setDragMode(QGraphicsView::ScrollHandDrag);

    auto *chartWgt = new QWidget();
    auto *chartLyt = new QVBoxLayout(chartWgt);
    chartLyt->addWidget(m_chartView_);
    chartHistoryScroll_ = new QScrollBar(Qt::Horizontal);
    chartHistoryScroll_->setEnabled(false);
    chartHistoryScroll_->setRange(0, 0);
    chartLyt->addWidget(chartHistoryScroll_);
    chartLyt->setContentsMargins(0, 0, 0, 0);
    middleRowLyt->addWidget(chartWgt, 3);

    // Scroll area for checkboxes
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    middleRowLyt->addWidget(scroll, 1);

    m_checksContainer_ = new QWidget(scroll);
    m_checksLayout_ = new QVBoxLayout(m_checksContainer_);
    m_checksLayout_->setContentsMargins(4, 4, 4, 4);
    m_checksLayout_->setSpacing(2);
    scroll->setWidget(m_checksContainer_);

    m_globalCheck_ = nullptr;

    // bottom label
    m_emptyLabel_ = new QLabel(tr("No data yet."), this);
    m_emptyLabel_->setAlignment(Qt::AlignCenter);
    rootLayout->addWidget(m_emptyLabel_);

    connect(m_typeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DiagramWidget::onTypeChanged);
    connect(chartHistoryScroll_, &QScrollBar::valueChanged, this, &DiagramWidget::timeRangeChanged);
}

void DiagramWidget::onResetZoomClicked()
{
    m_chart_->zoomReset();
}

bool DiagramWidget::isJointType() const
{
    return m_type_ == DiagramType::JointPosition ||
           m_type_ == DiagramType::JointVelocity;
}

QColor DiagramWidget::colorForId(const QString &id) const
{
    uint h = qHash(id);
    int hue = static_cast<int>(h % 360);
    return QColor::fromHsv(hue, 200, 230);
}

void DiagramWidget::onTypeChanged(int index)
{
    m_type_ = static_cast<DiagramType>(index);
    m_checkboxesDirty_ = true;
}

void DiagramWidget::clearSignalUi()
{
    QLayoutItem *item;
    while ((item = m_checksLayout_->takeAt(0)) != nullptr)
    {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }

    m_signals_.clear();
    m_modules_.clear();
    m_globalCheck_ = nullptr;
}

void DiagramWidget::rebuildSignalList(const HandStateMap &hand)
{
    clearSignalUi();

    if (hand.empty())
    {
        m_emptyLabel_->setVisible(true);
        return;
    }
    m_emptyLabel_->setVisible(false);

    // global tri-state "Select all"
    m_globalCheck_ = new QCheckBox(tr("Select all"), m_checksContainer_);
    m_globalCheck_->setTristate(true);
    m_globalCheck_->setCheckState(Qt::Unchecked);
    m_checksLayout_->addWidget(m_globalCheck_);
    connect(m_globalCheck_, &QCheckBox::clicked,
            this, &DiagramWidget::onGlobalClicked);

    // per-module groups
    for (const auto &modPair : hand)
    {
        const std::string &modNameStd = modPair.first;
        const JointGroup &mod = modPair.second;
        QString modName = QString::fromStdString(modNameStd);

        ModuleUi &mui = m_modules_[modName];
        if (!mui.headerCheck)
        {
            mui.name = modName;

            QWidget *headerRow = new QWidget(m_checksContainer_);
            auto *hLayout = new QHBoxLayout(headerRow);
            hLayout->setContentsMargins(0, 0, 0, 0);
            hLayout->setSpacing(2);

            mui.headerCheck = new QCheckBox(modName, headerRow);
            mui.headerCheck->setTristate(true);
            mui.headerCheck->setCheckState(Qt::Unchecked);

            mui.toggleButton = new QPushButton("+", headerRow);
            mui.toggleButton->setCheckable(true);
            mui.toggleButton->setMaximumWidth(24);
            mui.toggleButton->setProperty("moduleName", modName);

            hLayout->addWidget(mui.headerCheck);
            hLayout->addStretch();
            hLayout->addWidget(mui.toggleButton);
            headerRow->setLayout(hLayout);
            m_checksLayout_->addWidget(headerRow);

            mui.signalsWidget = new QWidget(m_checksContainer_);
            mui.signalsLayout = new QVBoxLayout(mui.signalsWidget);
            mui.signalsLayout->setContentsMargins(20, 0, 0, 0);
            mui.signalsLayout->setSpacing(2);
            mui.signalsWidget->setVisible(false);
            m_checksLayout_->addWidget(mui.signalsWidget);

            connect(mui.headerCheck, &QCheckBox::clicked,
                    this, &DiagramWidget::onModuleHeaderClicked);
            connect(mui.toggleButton, &QPushButton::toggled,
                    this, &DiagramWidget::onModuleToggleClicked);
        }

        // signals
        if (isJointType())
        {
            for (const auto &jPair : mod.joints)
            {
                const std::string &jNameStd = jPair.first;
                QString jointName = QString::fromStdString(jNameStd);
                QString id = modName + "." + jointName;

                QCheckBox *cb = new QCheckBox(jointName, mui.signalsWidget);
                cb->setChecked(false);
                cb->setProperty("signalId", id);
                mui.signalsLayout->addWidget(cb);

                SignalInfo info;
                info.id = id;
                info.moduleName = modName;
                info.jointName = jointName;
                info.isJoint = true;
                info.motorName = "";
                info.checkBox = cb;
                info.series = nullptr;

                m_signals_.insert(id, info);
                mui.signalIds.append(id);

                connect(cb, &QCheckBox::toggled,
                        this, &DiagramWidget::onSignalToggled);
            }
        }
        else
        {
            for (const auto &mPair : mod.motors)
            {
                QString motorName = QString::fromStdString(mPair.first);
                QString id = modName + QString(".M")+motorName;

                QString label = QString("Motor ")+motorName;
                QCheckBox *cb = new QCheckBox(label, mui.signalsWidget);
                cb->setChecked(false);
                cb->setProperty("signalId", id);
                mui.signalsLayout->addWidget(cb);

                SignalInfo info;
                info.id = id;
                info.moduleName = modName;
                info.jointName = QString("");
                info.isJoint = false;
                info.motorName = motorName;
                info.checkBox = cb;
                info.series = nullptr;

                m_signals_.insert(id, info);
                mui.signalIds.append(id);

                connect(cb, &QCheckBox::toggled,
                        this, &DiagramWidget::onSignalToggled);
            }
        }
    }

    m_checksLayout_->addStretch();
    updateGlobalHeaderState();
}

void DiagramWidget::updateSignalListFromHand(const HandStateMap &hand)
{
    if (m_checkboxesDirty_)
    {
        if (!hand.empty())
            rebuildSignalList(hand);
        m_checkboxesDirty_ = false;
    }
}

void DiagramWidget::onModuleToggleClicked(bool checked)
{
    QPushButton *btn = qobject_cast<QPushButton *>(sender());
    if (!btn)
        return;
    QString moduleName = btn->property("moduleName").toString();
    auto it = m_modules_.find(moduleName);
    if (it == m_modules_.end())
        return;

    ModuleUi &mui = it.value();
    if (mui.signalsWidget)
        mui.signalsWidget->setVisible(checked);
    btn->setText(checked ? "-" : "+");
}

void DiagramWidget::onSignalToggled(bool)
{
    QCheckBox *cb = qobject_cast<QCheckBox *>(sender());
    if (!cb)
        return;
    QString id = cb->property("signalId").toString();
    auto it = m_signals_.find(id);
    if (it == m_signals_.end())
        return;
    SignalInfo &sig = it.value();

    if (cb->isChecked())
    {
        ensureSeries(sig);
        if (sig.series)
            sig.series->setVisible(true);
    }
    else
    {
        if (sig.series)
            sig.series->setVisible(false);
    }

    if (!m_updatingFromHeader_)
    {
        updateModuleHeaderState(sig.moduleName);
        updateGlobalHeaderState();
    }
}

void DiagramWidget::onModuleHeaderClicked(bool checked)
{
    QCheckBox *cb = qobject_cast<QCheckBox *>(sender());
    if (!cb)
        return;
    QString moduleName = cb->text();
    auto it = m_modules_.find(moduleName);
    if (it == m_modules_.end())
        return;

    ModuleUi &mui = it.value();

    m_updatingFromHeader_ = true;
    for (const QString &id : mui.signalIds)
    {
        auto sit = m_signals_.find(id);
        if (sit == m_signals_.end())
            continue;
        if (sit->checkBox)
            sit->checkBox->setChecked(checked);
    }
    m_updatingFromHeader_ = false;

    updateModuleHeaderState(moduleName);
    updateGlobalHeaderState();
}

void DiagramWidget::onGlobalClicked(bool checked)
{
    if (!m_globalCheck_)
        return;
    m_updatingFromHeader_ = true;
    for (auto mit = m_modules_.begin(); mit != m_modules_.end(); ++mit)
    {
        ModuleUi &mui = mit.value();
        for (const QString &id : mui.signalIds)
        {
            auto sit = m_signals_.find(id);
            if (sit == m_signals_.end())
                continue;
            if (sit->checkBox)
                sit->checkBox->setChecked(checked);
        }
    }
    m_updatingFromHeader_ = false;

    for (auto mit = m_modules_.begin(); mit != m_modules_.end(); ++mit)
        updateModuleHeaderState(mit.key());
    updateGlobalHeaderState();
}

void DiagramWidget::updateModuleHeaderState(const QString &moduleName)
{
    auto it = m_modules_.find(moduleName);
    if (it == m_modules_.end())
        return;
    ModuleUi &mui = it.value();
    if (!mui.headerCheck)
        return;

    int total = 0;
    int checked = 0;

    for (const QString &id : mui.signalIds)
    {
        auto sit = m_signals_.find(id);
        if (sit == m_signals_.end())
            continue;
        QCheckBox *cb = sit->checkBox;
        if (!cb)
            continue;
        ++total;
        if (cb->isChecked())
            ++checked;
    }

    Qt::CheckState st = Qt::Unchecked;
    if (total > 0)
    {
        if (checked == 0)
            st = Qt::Unchecked;
        else if (checked == total)
            st = Qt::Checked;
        else
            st = Qt::PartiallyChecked;
    }
    mui.headerCheck->setCheckState(st);
}

void DiagramWidget::updateGlobalHeaderState()
{
    if (!m_globalCheck_)
        return;

    int total = 0;
    int checked = 0;

    for (auto it = m_signals_.begin(); it != m_signals_.end(); ++it)
    {
        QCheckBox *cb = it->checkBox;
        if (!cb)
            continue;
        ++total;
        if (cb->isChecked())
            ++checked;
    }

    Qt::CheckState st = Qt::Unchecked;
    if (total > 0)
    {
        if (checked == 0)
            st = Qt::Unchecked;
        else if (checked == total)
            st = Qt::Checked;
        else
            st = Qt::PartiallyChecked;
    }
    m_globalCheck_->setCheckState(st);
}

void DiagramWidget::ensureSeries(SignalInfo &sig)
{
    if (sig.series)
    {
        return;
    }

    auto *series = new QLineSeries(m_chart_);
    series->setName(sig.checkBox ? sig.checkBox->text() : sig.id);
    series->setColor(colorForId(sig.id));

    m_chart_->addSeries(series);
    series->attachAxis(m_axisX_);
    series->attachAxis(m_axisY_);

    sig.series = series;
}

bool DiagramWidget::getValueForSignalAtSample(const SignalInfo &sig,
                                              const HandStateMap &hand,
                                              float &outValue) const
{
    auto modIt = hand.find(sig.moduleName.toStdString());
    if (modIt == hand.end())
        return false;
    const JointGroup &mod = modIt->second;

    if (sig.isJoint)
    {
        auto jIt = mod.joints.find(sig.jointName.toStdString());
        if (jIt == mod.joints.end())
            return false;
        const DevState &js = jIt->second;
        switch (m_type_)
        {
        case DiagramType::JointPosition:
            outValue = js.position;
            return true;
        case DiagramType::JointVelocity:
            outValue = js.velocity;
            return true;
        default:
            return false;
        }
    }
    else
    {
        auto mIt = mod.motors.find(sig.motorName.toStdString());
        if (mIt == mod.motors.end())
            return false;
        const DevState &t = mIt->second;
        switch (m_type_)
        {
        case DiagramType::MotorPosition:
            outValue = static_cast<float>(t.position);
            return true;
        case DiagramType::MotorVelocity:
            outValue = static_cast<float>(t.velocity);
            return true;
        case DiagramType::MotorCurrent:
            outValue = static_cast<float>(t.effort);
            return true;
        default:
            return false;
        }
    }
}

void DiagramWidget::renderWindow(const std::deque<HandStateMap> &history,
                                 double samplePeriodSec,
                                 int historyOffsetSamples,
                                 int startIdxDeque,
                                 int endIdxDeque,
                                 double xMin, double xMax)
{
    if (history.empty() || m_signals_.isEmpty())
        return;
    if (startIdxDeque < 0 || endIdxDeque < startIdxDeque)
        return;

    int startIdx = startIdxDeque;
    int endIdx = endIdxDeque;
    double xMin_ = xMin;
    double xMax_ = xMax;

    if (m_isLive_)
    {
        chartHistoryScroll_->setRange(0, startIdx);
        chartHistoryScroll_->setValue(startIdx);
    }
    else if (updateRequest_)
    {
        startIdx = chartHistoryScroll_->value();
        endIdx = startIdx + m_maxPoints_ - 1;
        xMin_ = startIdx * m_samplePeriodSec_;
        xMax_ = endIdx * m_samplePeriodSec_;
        updateRequest_ = false;
    }
    else
        return;

    const int windowLen = endIdx - startIdx + 1;
    if (windowLen <= 0)
        return;

    double globalMin = std::numeric_limits<double>::max();
    double globalMax = std::numeric_limits<double>::lowest();
    bool haveAny = false;

    // For each active signal, rebuild its series from this window
    for (auto it = m_signals_.begin(); it != m_signals_.end(); ++it)
    {
        SignalInfo &sig = it.value();
        if (!sig.checkBox || !sig.checkBox->isChecked())
            continue;

        ensureSeries(sig);
        if (!sig.series)
            continue;

        QLineSeries *series = sig.series;
        series->clear();

        for (int idx = 0; idx < windowLen; ++idx)
        {
            int dequeIndex = startIdx + idx;
            int globalIdx = historyOffsetSamples + dequeIndex + 1; // 1-based
            double t = globalIdx * samplePeriodSec;

            float v;
            if (!getValueForSignalAtSample(sig, history[dequeIndex], v))
                continue;

            series->append(t, v);

            if (v < globalMin)
                globalMin = v;
            if (v > globalMax)
                globalMax = v;
            haveAny = true;
        }
    }

    if (haveAny)
    {
        if (qFuzzyCompare(globalMin, globalMax))
        {
            globalMin -= 1.0;
            globalMax += 1.0;
        }
        m_axisY_->setRange(globalMin, globalMax);
    }

    // Shared x-axis range
    m_axisX_->setRange(xMin_, xMax_);
}

void DiagramWidget::setLiveMode(bool isLive)
{
    m_isLive_ = isLive;
    chartHistoryScroll_->setEnabled(!isLive);
}

void DiagramWidget::timeRangeChanged(int start)
{
    updateRequest_ = true;
}
