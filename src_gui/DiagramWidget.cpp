#include "DiagramWidget.hpp"

#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QLabel>
#include <QScrollArea>
#include <QPushButton>
#include <QPainter>
#include <QtMath>
#include <QSplitter>

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

    // --- top: type combo ---
    auto *topRow = new QHBoxLayout;
    auto *typeLabel = new QLabel(tr("Type:"), this);
    m_typeCombo = new QComboBox(this);
    m_typeCombo->addItem(tr("Joint position")); // 0
    m_typeCombo->addItem(tr("Joint velocity")); // 1
    m_typeCombo->addItem(tr("Motor position")); // 2
    m_typeCombo->addItem(tr("Motor velocity")); // 3
    m_typeCombo->addItem(tr("Motor current"));  // 4

    topRow->addWidget(typeLabel);
    topRow->addWidget(m_typeCombo, 1);
    topRow->addStretch();
    rootLayout->addLayout(topRow);

    // --- middle: chart + scroll area for checkboxes ---
    auto *middleRow = new QSplitter(Qt::Horizontal);
    auto *middleRowLyt = new QHBoxLayout(middleRow);
    rootLayout->addWidget(middleRow, 1);

    // chart
    m_chart = new QChart;
    m_chart->setTitle(tr("Telemetry"));
    m_chart->legend()->setVisible(true);
    m_chart->legend()->setAlignment(Qt::AlignBottom);

    m_axisX = new QValueAxis;
    m_axisX->setTitleText(tr("Sample"));
    m_axisY = new QValueAxis;
    m_axisY->setTitleText(tr("Value"));

    m_chart->addAxis(m_axisX, Qt::AlignBottom);
    m_chart->addAxis(m_axisY, Qt::AlignLeft);

    m_chartView = new QChartView(m_chart, this);
    m_chartView->setRenderHint(QPainter::Antialiasing);
    middleRowLyt->addWidget(m_chartView, 3);

    // scroll area
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    middleRowLyt->addWidget(scroll, 1);

    m_checksContainer = new QWidget(scroll);
    m_checksLayout = new QVBoxLayout(m_checksContainer);
    m_checksLayout->setContentsMargins(4, 4, 4, 4);
    m_checksLayout->setSpacing(2);
    scroll->setWidget(m_checksContainer);

    // global "select all" (created on first rebuild)
    m_globalCheck = nullptr;

    // bottom info label
    m_emptyLabel = new QLabel(tr("No data yet."), this);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    rootLayout->addWidget(m_emptyLabel);

    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DiagramWidget::onTypeChanged);
}

bool DiagramWidget::isJointType() const
{
    return m_type == DiagramType::JointPosition ||
           m_type == DiagramType::JointVelocity;
}

QColor DiagramWidget::colorForId(const QString &id) const
{
    uint h = qHash(id);
    int hue = static_cast<int>(h % 360);
    return QColor::fromHsv(hue, 200, 230);
}

void DiagramWidget::onTypeChanged(int index)
{
    m_type = static_cast<DiagramType>(index);
    m_checkboxesDirty = true; // rebuild groups on next updateFromHand()
}

void DiagramWidget::clearSignalUi()
{
    QLayoutItem *item;
    while ((item = m_checksLayout->takeAt(0)) != nullptr)
    {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }

    m_signals.clear();
    m_modules.clear();
    m_globalCheck = nullptr;
}

void DiagramWidget::rebuildSignalList(const HandStateMap &hand)
{
    clearSignalUi();

    if (hand.empty())
    {
        m_emptyLabel->setVisible(true);
        return;
    }

    m_emptyLabel->setVisible(false);

    // global tri-state "Select all"
    m_globalCheck = new QCheckBox(tr("Select all"), m_checksContainer);
    m_globalCheck->setTristate(true);
    m_globalCheck->setCheckState(Qt::Unchecked);
    m_checksLayout->addWidget(m_globalCheck);
    connect(m_globalCheck, &QCheckBox::clicked,
            this, &DiagramWidget::onGlobalClicked);

    // per-module groups
    for (const auto &modPair : hand)
    {
        const std::string &modNameStd = modPair.first;
        const grip::ModuleState &mod = modPair.second;
        QString modName = QString::fromStdString(modNameStd);

        ModuleUi &mui = m_modules[modName];
        if (!mui.headerCheck)
        {
            mui.name = modName;

            // header row: [tri-state checkbox] [module name] [toggle button]
            QWidget *headerRow = new QWidget(m_checksContainer);
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
            m_checksLayout->addWidget(headerRow);

            // container for the actual joint/motor checkboxes
            mui.signalsWidget = new QWidget(m_checksContainer);
            mui.signalsLayout = new QVBoxLayout(mui.signalsWidget);
            mui.signalsLayout->setContentsMargins(20, 0, 0, 0); // indent
            mui.signalsLayout->setSpacing(2);
            mui.signalsWidget->setVisible(false); // hidden by default
            m_checksLayout->addWidget(mui.signalsWidget);

            connect(mui.headerCheck, &QCheckBox::clicked,
                    this, &DiagramWidget::onModuleHeaderClicked);
            connect(mui.toggleButton, &QPushButton::toggled,
                    this, &DiagramWidget::onModuleToggleClicked);
        }

        // individual signals for this module
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
                info.motorId = -1;
                info.checkBox = cb;
                info.series = nullptr;
                m_signals.insert(id, info);

                mui.signalIds.append(id);

                connect(cb, &QCheckBox::toggled,
                        this, &DiagramWidget::onSignalToggled);
            }
        }
        else
        {
            for (const auto &mPair : mod.motors)
            {
                int motorId = mPair.first;
                QString id = modName + QString(".M%1").arg(motorId);

                QString label = QString("Motor %1").arg(motorId);
                QCheckBox *cb = new QCheckBox(label, mui.signalsWidget);
                cb->setChecked(false);
                cb->setProperty("signalId", id);
                mui.signalsLayout->addWidget(cb);

                SignalInfo info;
                info.id = id;
                info.moduleName = modName;
                info.jointName = QString("");
                info.isJoint = false;
                info.motorId = motorId;
                info.checkBox = cb;
                info.series = nullptr;
                m_signals.insert(id, info);

                mui.signalIds.append(id);

                connect(cb, &QCheckBox::toggled,
                        this, &DiagramWidget::onSignalToggled);
            }
        }
    }

    // stretch at the bottom so items stay at the top
    m_checksLayout->addStretch();

    updateGlobalHeaderState();
}

void DiagramWidget::onModuleToggleClicked(bool checked)
{
    QPushButton *btn = qobject_cast<QPushButton *>(sender());
    if (!btn)
        return;
    QString moduleName = btn->property("moduleName").toString();
    auto it = m_modules.find(moduleName);
    if (it == m_modules.end())
        return;

    ModuleUi &mui = it.value();
    if (mui.signalsWidget)
        mui.signalsWidget->setVisible(checked);
    btn->setText(checked ? "-" : "+");
}

void DiagramWidget::onSignalToggled(bool checked)
{
    QCheckBox *cb = qobject_cast<QCheckBox *>(sender());
    if (!cb)
        return;
    QString id = cb->property("signalId").toString();
    auto it = m_signals.find(id);
    if (it == m_signals.end())
        return;
    SignalInfo &sig = it.value();

    // 1. add / show / hide line
    if (checked)
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

    // 2. update module & global tri-state (unless we are in a bulk operation)
    if (!m_updatingFromHeader)
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

    QString moduleName = cb->text(); // label is module name
    auto it = m_modules.find(moduleName);
    if (it == m_modules.end())
        return;
    ModuleUi &mui = it.value();

    m_updatingFromHeader = true;
    for (const QString &id : mui.signalIds)
    {
        auto sit = m_signals.find(id);
        if (sit == m_signals.end())
            continue;
        if (sit->checkBox)
            sit->checkBox->setChecked(checked);
    }
    m_updatingFromHeader = false;

    updateModuleHeaderState(moduleName);
    updateGlobalHeaderState();
}

void DiagramWidget::onGlobalClicked(bool checked)
{
    if (!m_globalCheck)
        return;

    m_updatingFromHeader = true;
    for (auto mit = m_modules.begin(); mit != m_modules.end(); ++mit)
    {
        ModuleUi &mui = mit.value();
        for (const QString &id : mui.signalIds)
        {
            auto sit = m_signals.find(id);
            if (sit == m_signals.end())
                continue;
            if (sit->checkBox)
                sit->checkBox->setChecked(checked);
        }
    }
    m_updatingFromHeader = false;

    // recompute states
    for (auto mit = m_modules.begin(); mit != m_modules.end(); ++mit)
        updateModuleHeaderState(mit.key());
    updateGlobalHeaderState();
}

void DiagramWidget::updateModuleHeaderState(const QString &moduleName)
{
    auto it = m_modules.find(moduleName);
    if (it == m_modules.end())
        return;
    ModuleUi &mui = it.value();
    if (!mui.headerCheck)
        return;

    int total = 0;
    int checked = 0;

    for (const QString &id : mui.signalIds)
    {
        auto sit = m_signals.find(id);
        if (sit == m_signals.end())
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
    if (!m_globalCheck)
        return;

    int total = 0;
    int checked = 0;

    for (auto it = m_signals.begin(); it != m_signals.end(); ++it)
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
    m_globalCheck->setCheckState(st);
}

void DiagramWidget::ensureSeries(SignalInfo &sig)
{
    if (sig.series)
        return;

    auto *series = new QLineSeries(m_chart);
    series->setName(sig.checkBox ? sig.checkBox->text() : sig.id);
    series->setColor(colorForId(sig.id));

    // build fixed-length buffer in chart
    for (int i = 0; i < m_maxPoints; ++i)
        series->append(i, 0.0);

    m_chart->addSeries(series);
    series->attachAxis(m_axisX);
    series->attachAxis(m_axisY);

    sig.series = series;
}

bool DiagramWidget::getValueForSignal(const SignalInfo &sig,
                                      const HandStateMap &hand,
                                      float &outValue) const
{
    auto modIt = hand.find(sig.moduleName.toStdString());
    if (modIt == hand.end())
        return false;

    const grip::ModuleState &mod = modIt->second;

    if (sig.isJoint)
    {
        auto jIt = mod.joints.find(sig.jointName.toStdString());
        if (jIt == mod.joints.end())
            return false;

        const grip::JointState &js = jIt->second;
        switch (m_type)
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
        auto mIt = mod.motors.find(sig.motorId);
        if (mIt == mod.motors.end())
            return false;

        const grip::Telemetry &t = mIt->second;
        switch (m_type)
        {
        case DiagramType::MotorPosition:
            outValue = static_cast<float>(t.encoder_counts);
            return true;
        case DiagramType::MotorVelocity:
            outValue = static_cast<float>(t.speed);
            return true;
        case DiagramType::MotorCurrent:
            outValue = static_cast<float>(t.iq_mA);
            return true;
        default:
            return false;
        }
    }
}

void DiagramWidget::updateFromHand(const HandStateMap &hand)
{
    if (m_checkboxesDirty)
    {
        if (!hand.empty())
            rebuildSignalList(hand); // do nothing if not initialized
        m_checkboxesDirty = false;
    }

    if (hand.empty() || m_signals.isEmpty())
        return;

    double globalMin = std::numeric_limits<double>::max();
    double globalMax = std::numeric_limits<double>::lowest();
    bool haveAny = false;

    for (auto it = m_signals.begin(); it != m_signals.end(); ++it)
    {
        SignalInfo &sig = it.value();
        if (!sig.checkBox || !sig.checkBox->isChecked())
            continue;

        float v;
        if (!getValueForSignal(sig, hand, v))
            continue;

        // update values buffer
        sig.values.push_back(v);
        if (static_cast<int>(sig.values.size()) > m_maxPoints)
            sig.values.pop_front();

        ensureSeries(sig);
        if (!sig.series)
            continue;

        // maintain fixed-size series; newest samples at the right
        int size = static_cast<int>(sig.values.size());
        int start = m_maxPoints - size;
        int idx = 0;

        // old part = 0
        for (int i = 0; i < start; ++i)
            sig.series->replace(i, i, 0.0);

        // recent values
        for (float val : sig.values)
        {
            int i = start + idx;
            sig.series->replace(i, i, val);
            ++idx;

            if (val < globalMin)
                globalMin = val;
            if (val > globalMax)
                globalMax = val;
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
        m_axisY->setRange(globalMin, globalMax);
    }

    // X axis (fixed)
    m_axisX->setRange(0, m_maxPoints > 0 ? m_maxPoints * 0.01 : 1);
}
