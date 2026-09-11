#include "SidePanel.h"
#include <algorithm>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QScrollArea>
#include <QSlider>
#include <QLabel>
#include <QWidget>
#include <QPushButton>
#include <QToolButton>
#include <QLineEdit>
#include <QApplication>
#include <QMessageBox>
#include <QTimer>
#include <QImage>
#include <QPixmap>
#include <QComboBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QFormLayout>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QVector>
#include <cmath>

namespace
{
// Signal | T | Pos | Vel | Trq | St
constexpr int kFeedbackColumns = 6;

// The read timer ticks at 100 Hz. Rewriting every cell that often would flood
// the event loop with repaint requests, so the table is refreshed at 10 Hz.
constexpr qint64 kFeedbackIntervalMs = 100;

// Feedback table does not need sub-pixel precision; two decimals keeps the
// columns narrow enough for the side panel.
QString formatValue(float v)
{
    return QString::number(double(v), 'f', 2);
}

void setCell(QTableWidgetItem *item, float v)
{
    if (item)
        item->setText(formatValue(v));
}
}

SidePanel::SidePanel(QWidget *parent)
    : QFrame(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    buildFeedbackPanel();
    buildSliderPanel();

    // Splitter: feedback (top) + sliders (bottom)
    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(feedbackPanel_);
    splitter->addWidget(sliderPanel_);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    root->addWidget(splitter);
}

void SidePanel::buildFeedbackPanel()
{
    feedbackPanel_ = new QWidget(this);
    auto *feedbackRoot = new QVBoxLayout(feedbackPanel_);
    feedbackRoot->setContentsMargins(6, 6, 6, 6);
    feedbackRoot->setSpacing(4);

    auto *title = new QLabel("Feedback:");
    auto bold = title->font();
    bold.setBold(true);
    title->setFont(bold);
    feedbackRoot->addWidget(title);

    // Columns: Signal | Kind | Pos | Vel | Trq | Status
    feedbackTable_ = new QTableWidget(0, kFeedbackColumns, this);
    feedbackTable_->setHorizontalHeaderLabels(
        {QStringLiteral("Signal"), QStringLiteral("T"),
         QStringLiteral("Pos"), QStringLiteral("Vel"),
         QStringLiteral("Trq"), QStringLiteral("St")});
    feedbackTable_->horizontalHeader()->setToolTip(
        "Live telemetry. T identifies the source: J = joint sensor, M = motor.\n"
        "Pos/Vel are the reported position and velocity.\n"
        "Trq is the motor current feedback from TPDO1, in mA (joint rows show '-').");
    feedbackTable_->verticalHeader()->setVisible(false);
    feedbackTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    feedbackTable_->setSelectionMode(QAbstractItemView::NoSelection);
    feedbackTable_->setFocusPolicy(Qt::NoFocus);
    feedbackTable_->setAlternatingRowColors(true);
    feedbackTable_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *header = feedbackTable_->horizontalHeader();
    header->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < kFeedbackColumns; ++c)
        header->setSectionResizeMode(c, QHeaderView::Fixed);
    feedbackTable_->setColumnWidth(1, 22);  // T
    feedbackTable_->setColumnWidth(2, 66);  // Pos
    feedbackTable_->setColumnWidth(3, 66);  // Vel
    feedbackTable_->setColumnWidth(4, 66);  // Trq
    feedbackTable_->setColumnWidth(5, 26);  // St

    feedbackRoot->addWidget(feedbackTable_, 1);

    buildBusTable(feedbackRoot);
}

void SidePanel::buildBusTable(QVBoxLayout *root)
{
    auto *title = new QLabel(QStringLiteral("Bus Info"));
    auto bold = title->font();
    bold.setBold(true);
    title->setFont(bold);
    title->setContentsMargins(0, 6, 0, 0);
    root->addWidget(title);

    // Columns: Voltage | Current | Temp1 | Temp2 | Fault
    busTable_ = new QTableWidget(1, 5, this);
    busTable_->setHorizontalHeaderLabels(
        {QStringLiteral("Voltage"), QStringLiteral("Current"),
         QStringLiteral("Temp1"), QStringLiteral("Temp2"),
         QStringLiteral("Fault")});
    busTable_->verticalHeader()->setVisible(false);
    busTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    busTable_->setSelectionMode(QAbstractItemView::NoSelection);
    busTable_->setFocusPolicy(Qt::NoFocus);
    busTable_->setAlternatingRowColors(true);
    busTable_->setToolTip("Bus telemetry from the sensor frame:\n"
                          "bus voltage / current, board temperatures and the fault word.");
    busTable_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    busTable_->setFixedHeight(48);

    auto *header = busTable_->horizontalHeader();
    for (int c = 0; c < 5; ++c)
    {
        header->setSectionResizeMode(c, QHeaderView::Stretch);
        auto *item = new QTableWidgetItem(QStringLiteral("-"));
        item->setFlags(Qt::ItemIsEnabled);
        item->setTextAlignment(Qt::AlignCenter);
        busTable_->setItem(0, c, item);
    }
    busTable_->setVerticalHeaderLabels({QStringLiteral("bus")});

    root->addWidget(busTable_);
}

void SidePanel::updateBusInfo(const BusData &bus)
{
    if (!busTable_)
        return;

    const QString fault = bus.fault_code == 0
                              ? QStringLiteral("OK")
                              : QStringLiteral("0x%1").arg(bus.fault_code, 4, 16, QLatin1Char('0'));
    const QStringList values = {
        QString::number(bus.bus_voltage, 'f', 2) + QStringLiteral(" V"),
        QString::number(bus.bus_current) + QStringLiteral(" mA"),
        QString::number(bus.temp1_cC) + QStringLiteral(" °C"),
        QString::number(bus.temp2_cC) + QStringLiteral(" °C"),
        fault};

    for (int c = 0; c < values.size(); ++c)
    {
        auto *item = busTable_->item(0, c);
        if (!item)
            continue;
        item->setText(values.at(c));
        item->setToolTip(c == 4 ? QString::fromStdString([&]
                                                         {
                                                             auto codes = grip::fault_code_str(bus.fault_code);
                                                             std::string joined;
                                                             for (size_t i = 0; i < codes.size(); ++i)
                                                                 joined += (i ? ", " : "") + codes[i];
                                                             return joined.empty() ? std::string("no fault") : joined;
                                                         }())
                                               : QString());
    }
}

void SidePanel::clearFeedbackTable()
{
    if (!feedbackTable_)
        return;
    // clearContents() leaves the row count intact but orphans every item, so
    // reset the rows explicitly. Cell widgets are removed with their rows.
    feedbackTable_->setRowCount(0);
    feedbackTable_->clearContents();
    jointRows_.clear();
    motorRows_.clear();
}

void SidePanel::addFeedbackSection(const QString &title)
{
    const int row = feedbackTable_->rowCount();
    feedbackTable_->insertRow(row);

    auto *item = new QTableWidgetItem(title);
    item->setFlags(Qt::NoItemFlags);
    item->setBackground(QColor("#dcdcdc"));
    auto f = item->font();
    f.setBold(true);
    item->setFont(f);
    feedbackTable_->setItem(row, 0, item);
    feedbackTable_->setSpan(row, 0, 1, kFeedbackColumns);
}

SidePanel::FeedbackRow SidePanel::addFeedbackRow(const QString &name,
                                                 const QString &kind,
                                                 bool withLed)
{
    const int row = feedbackTable_->rowCount();
    feedbackTable_->insertRow(row);

    auto *nameItem = new QTableWidgetItem(name);
    nameItem->setFlags(Qt::ItemIsEnabled);
    nameItem->setToolTip(name);
    feedbackTable_->setItem(row, 0, nameItem);

    auto *kindItem = new QTableWidgetItem(kind);
    kindItem->setFlags(Qt::ItemIsEnabled);
    kindItem->setTextAlignment(Qt::AlignCenter);
    kindItem->setForeground(QColor("#666666"));
    kindItem->setToolTip(kind == QStringLiteral("J") ? QStringLiteral("Joint sensor")
                                                     : QStringLiteral("Motor"));
    feedbackTable_->setItem(row, 1, kindItem);

    FeedbackRow cells;
    for (int c = 2; c <= 4; ++c)
    {
        auto *item = new QTableWidgetItem(QStringLiteral("-"));
        item->setFlags(Qt::ItemIsEnabled);
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        feedbackTable_->setItem(row, c, item);
        if (c == 2)
            cells.pos = item;
        else if (c == 3)
            cells.vel = item;
        else
            cells.trq = item;
    }

    if (withLed)
    {
        cells.led = new LedIndicator();
        cells.led->setFixedSize(16, 16);
        feedbackTable_->setCellWidget(row, 5, cells.led);
    }

    return cells;
}

void SidePanel::loadFeedbackTable(grip::HandSettings *hand_cfg)
{
    if (!feedbackTable_ || !hand_cfg)
        return;

    clearFeedbackTable();

    for (const auto &s : hand_cfg->structs)
    {
        // Skip modules without a single actuated joint.
        bool hasJoint = false;
        for (const auto &j : s.second)
        {
            if (j.id >= 0)
            {
                hasJoint = true;
                break;
            }
        }
        if (!hasJoint)
            continue;

        addFeedbackSection(QString::fromStdString(s.first));

        // Joint rows: every configured joint (the wrist reports no joint data,
        // it simply stays at "-" until the first snapshot arrives).
        for (const auto &j : s.second)
        {
            if (j.id < 0)
                continue;
            const QString name = QString::fromStdString(j.name);
            jointRows_.insert(name, addFeedbackRow(name, QStringLiteral("J"), true));
        }

        // Motor rows: only actuated joints have a motor behind them.
        for (const auto &j : s.second)
        {
            if (j.id <= 0)
                continue;
            const QString name = QString::fromStdString(j.name);
            motorRows_.insert(name, addFeedbackRow(name, QStringLiteral("M"), false));
        }
    }
}

void SidePanel::updateFeedback(const HandStateMap &state)
{
    if (!feedbackTable_)
        return;

    if (!feedbackClock_.isValid())
        feedbackClock_.start();
    else if (feedbackClock_.elapsed() < kFeedbackIntervalMs)
        return;
    feedbackClock_.restart();

    for (const auto &[module, group] : state)
    {
        Q_UNUSED(module)

        for (const auto &[jointName, data] : group.joints)
        {
            const QString name = QString::fromStdString(jointName);
            auto it = jointRows_.constFind(name);
            if (it == jointRows_.constEnd())
                continue;

            setCell(it->pos, data.position);
            setCell(it->vel, data.velocity);
            if (it->trq)
                it->trq->setText(QStringLiteral("-"));

            // Keep the "(value)" readout next to the joint slider in sync.
            const int idx = sliderIndex_.value(name, -1);
            if (idx >= 0 && idx < currValueLabels_.size())
                currValueLabels_[idx]->setText(
                    QString("  (%1)").arg(formatValue(data.position)));
        }

        for (const auto &[jointName, data] : group.motors)
        {
            const QString name = QString::fromStdString(jointName);
            auto it = motorRows_.constFind(name);
            if (it == motorRows_.constEnd())
                continue;

            setCell(it->pos, data.position);
            setCell(it->vel, data.velocity);
            // Motor current reported by the drive (TPDO1 "torq", mA).
            setCell(it->trq, data.effort);
        }

        for (const auto &[jointName, mode] : group.control_mode)
        {
            const QString name = QString::fromStdString(jointName);
            const bool enabled = (mode & 0x01) == 1;

            auto it = jointRows_.constFind(name);
            if (it != jointRows_.constEnd() && it->led)
                it->led->setGreen(enabled);

            const int idx = sliderIndex_.value(name, -1);
            if (idx >= 0 && idx < leds_.size())
                leds_[idx]->setGreen(enabled);
        }
    }
}

void SidePanel::buildSliderPanel()
{
    sliderPanel_ = new QWidget;
    auto *sliderRoot = new QVBoxLayout(sliderPanel_);
    sliderRoot->setContentsMargins(6, 6, 6, 6);
    sliderRoot->setSpacing(4);
    auto *sliderWidget = new QWidget;
    sliderVBox_ = new QVBoxLayout(sliderWidget);
    sliderVBox_->setAlignment(Qt::AlignTop);

    QPushButton *resetBtn = new QPushButton();
    resetBtn->setText("Reset");
    
    btnMode_ = new QPushButton("Mode: Follow");
    btnMode_->setToolTip("Change the joint control mode\n"
                         "    Follow mode: joints act with the sliders\n"
                         "    Setup mode: The joints act after press \"Execute\" button");

    btnExecute_ = new QPushButton("Execute");
    btnExecute_->setEnabled(false);

    auto *btnWidget = new QWidget;
    QHBoxLayout *h = new QHBoxLayout(btnWidget);
    h->addWidget(btnMode_);
    h->addWidget(btnExecute_);
    h->addWidget(resetBtn);
    h->addStretch(1);
    h->setContentsMargins(0, 0, 0, 0);

    sliderRoot->addWidget(btnWidget);

    sliderScroll_ = new QScrollArea;
    sliderScroll_->setWidgetResizable(true);
    rebuildSliderHost();
    sliderRoot->addWidget(sliderScroll_);

    connect(btnMode_, &QPushButton::clicked, this, &SidePanel::onToggleMode);
    connect(btnExecute_, &QPushButton::clicked, this, &SidePanel::onExecute);
    connect(resetBtn, &QPushButton::clicked, this, &SidePanel::onReset);
}

void SidePanel::rebuildSliderHost()
{
    // Every joint row lives under a single content widget, so rebuilding the
    // host is enough to drop the whole tree: Qt unwinds parent/child
    // ownership for us instead of a hand-rolled recursive delete.
    auto *sliderWidget = new QWidget;
    sliderVBox_ = new QVBoxLayout(sliderWidget);
    sliderVBox_->setAlignment(Qt::AlignTop);
    sliderScroll_->setWidget(sliderWidget);
}

void SidePanel::loadSliderModule(QString name, float min, float max)
{
    // auto *row = new QWidget;
    auto *hl = new QHBoxLayout();
    hl->setContentsMargins(6, 2, 6, 2);
    auto *nameLbl = new QLabel(name);
    nameLabels_.append(nameLbl);
    auto *led = new LedIndicator();
    led->setFixedSize(20, 20);
    leds_.append(led);
    auto *valEnter = new QLineEdit("0.00");
    valEnter->setFixedWidth(75);
    valEnter->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    valueLabels_.append(valEnter);
    auto *currValueLbl = new QLabel("  (0.00)");
    currValueLabels_.append(currValueLbl);
    hl->addWidget(nameLbl);
    hl->addStretch(1);
    hl->addWidget(led);
    hl->addWidget(valEnter);
    hl->addWidget(currValueLbl);
    sliderVBox_->addLayout(hl);

    // Slider row: [0 .. 140] degrees
    auto *sl = new QHBoxLayout();
    sl->setContentsMargins(12, 0, 12, 8);
    auto *sMin = new QLabel(QString::number(min));
    auto *slider = new QSlider(Qt::Horizontal);
    slider->setRange((int)(min * 100.0), (int)(max * 100.0));
    slider->setValue(0);
    slider->setEnabled(false);
    auto *sMax = new QLabel(QString::number(max));
    sl->addWidget(sMin);
    sl->addWidget(slider);
    sl->addWidget(sMax);
    sliders_.append(slider);
    sliderVBox_->addLayout(sl);
    valueLbls.append(std::pair(sMin, sMax));
    control_modes.append("zero_torque");

    connect(slider, &QSlider::valueChanged, this, &SidePanel::onAnySliderChanged);
    connect(valEnter, &QLineEdit::editingFinished, this, &SidePanel::onValueEditChanged);
}

void SidePanel::loadHandControllor(grip::HandSettings *hand_cfg)
{
    auto *h1 = new QHBoxLayout();
    auto *v1 = new QVBoxLayout();
    auto *titleLbl = new QLabel("Control Mode: ");
    auto *advanceBtn = new QPushButton("Advance");
    v1->addWidget(titleLbl);
    v1->addWidget(advanceBtn);
    h1->addLayout(v1);

    auto *v2 = new QVBoxLayout();
    auto *sh1 = new QHBoxLayout();
    auto fingerLbl = new QLabel("Finger: ");
    auto *modeBox = new QComboBox();
    for (auto mode : hand_cfg->control_policies)
        modeBox->addItem(mode.c_str());
    sh1->addWidget(fingerLbl);
    sh1->addWidget(modeBox);
    v2->addLayout(sh1);

    connect(modeBox, &QComboBox::currentTextChanged, this, [=]()
        { emit controlChanged(QString("finger"), modeBox->currentText()); });

    auto module_name = hand_cfg->structs.find("WRIST");
    if (module_name != hand_cfg->structs.end())
    {
        auto *sh2 = new QHBoxLayout();
        auto wristLbl = new QLabel("Wrist: ");
        auto *wristModeBox = new QComboBox();
        for (auto mode : hand_cfg->wrist_control)
            wristModeBox->addItem(mode.c_str());
        sh2->addWidget(wristLbl);
        sh2->addWidget(wristModeBox);
        v2->addLayout(sh2);
        connect(wristModeBox, &QComboBox::currentTextChanged, this, [=]()
            { emit controlChanged(QString("wrist"), wristModeBox->currentText()); });
    }
    h1->addLayout(v2);
    h1->setContentsMargins(2, 2, 2, 15);
    sliderVBox_->addLayout(h1);

    connect(advanceBtn, &QPushButton::clicked, this, [=]()
                { emit openAdvancePanel(); });
    
    for (auto s : hand_cfg->structs)
    {
        auto *mode_lable = new QLabel(s.first.c_str());
        sliderVBox_->addWidget(mode_lable);

        for (auto j : s.second)
        {
            if (j.id <= 0) continue; 
            loadSliderModule(QString(j.name.c_str()), j.route.first, j.route.second);
            // loadSliderModule appends exactly one slider, so the new slider
            // sits at the last index. Feedback updates are keyed by joint name.
            sliderIndex_.insert(QString::fromStdString(j.name), sliders_.size() - 1);
        }
    }

    loadFeedbackTable(hand_cfg);
}

void SidePanel::loadActuatorControllor(grip::ActuatorSettings act_cfg)
{
    act_cfg_ = act_cfg;
    loadSliderModule(QString("Max Velocity"), 0, act_cfg_.max_vel);
    loadSliderModule(QString("Max Current"), 0, act_cfg_.max_current);

    auto *ctrlHl = new QHBoxLayout();
    auto *mode_lable = new QLabel("Control Mode: ");
    mode_box = new QComboBox();
    mode_box->addItem("Position");
    mode_box->addItem("Velocity");
    mode_box->addItem("Torque");
    mode_box->addItem("MIT");
    mode_box->setCurrentIndex(0);
    ctrlHl->addWidget(mode_lable);
    ctrlHl->addWidget(mode_box);
    sliderVBox_->addLayout(ctrlHl);

    loadSliderModule(QString("Position"), act_cfg_.route.first, act_cfg_.route.second);
    loadSliderModule(QString("-"), -1.0 * act_cfg_.max_vel, act_cfg_.max_vel);

    sliders_[3]->setEnabled(false);
    valueLabels_[3]->setEnabled(false);
    sliderVBox_->addStretch(1);

    connect(mode_box, &QComboBox::currentTextChanged, this, &SidePanel::onControlChanged);
    connect(sliders_[0], &QSlider::valueChanged, this, &SidePanel::onMaxVelocityChanged);
    connect(sliders_[1], &QSlider::valueChanged, this, &SidePanel::onMaxCurrentChanged);
}

void SidePanel::applyModeChange(grip::HandSettings *hand_cfg, QString joint_name, QString mode)
{
    int i = 0;
    while (i < nameLabels_.size())
    {
        if (nameLabels_[i]->text() == joint_name)
            break;
        i++;
    }
    if (i >= nameLabels_.size() || i >= sliders_.size())
    {
        return;
    }

    for (auto cfg : hand_cfg->structs)
    {
        for (auto joint_cfg : cfg.second)
        {
            if (joint_cfg.name == joint_name.toUtf8().constData())
            {
                if (mode == "hall_position")
                {
                    sliders_[i]->setRange(joint_cfg.hall_route.first, joint_cfg.hall_route.second);
                    sliders_[i]->setEnabled(true);
                    valueLbls[i].first->setText(QString::number(joint_cfg.hall_route.first));
                    valueLbls[i].second->setText(QString::number(joint_cfg.hall_route.second));
                    control_modes[i] = "hall_position";
                    return;
                }
                else if (mode == "joint_position" || mode == "MIT_torque")
                {
                    sliders_[i]->setRange(joint_cfg.route.first * 100, joint_cfg.route.second * 100);
                    sliders_[i]->setEnabled(true);
                    valueLbls[i].first->setText(QString::number(joint_cfg.route.first));
                    valueLbls[i].second->setText(QString::number(joint_cfg.route.second));
                    control_modes[i] = "joint_position";
                    return;
                }
                else if (mode == "zero_torque")
                {
                    sliders_[i]->setEnabled(false);
                    control_modes[i] = "zero_torque";
                    return;
                }
            }
        }
    }
    
    return;
}

// ============ slots ============

QList<int> SidePanel::jointDegrees() const
{
    QList<int> out;
    out.reserve(sliders_.size());
    for (auto *s : sliders_)
        out << s->value();
    return out;
}

QString SidePanel::modeText() const { return btnMode_->text(); }

void SidePanel::onAnySliderChanged()
{
    // Update numeric labels and echo in Follow mode
    for (int i = 0; i < sliders_.size() && i < valueLabels_.size() && i < control_modes.size(); ++i)
    {
        if (control_modes[i] == "joint_position")
            valueLabels_[i]->setText(QString("%1").arg((float)sliders_[i]->value() / 100.0));
        else if (control_modes[i] == "hall_position")
            valueLabels_[i]->setText(QString("%1").arg((int)sliders_[i]->value()));
        else
            valueLabels_[i]->setText(QString("0"));
    }
    if (btnMode_->text() == "Mode: Follow")
        emit valuesChanged();
}

void SidePanel::onValueEditChanged()
{
    // Only push back the row the user actually edited. Re-applying every row
    // re-emitted valueChanged for each slider and flooded the bus with
    // redundant targets.
    auto *edit = qobject_cast<QLineEdit *>(sender());
    const int edited = edit ? valueLabels_.indexOf(edit) : -1;

    const int count = std::min({valueLabels_.size(), sliders_.size(), control_modes.size()});
    for (int i = 0; i < count; ++i)
    {
        if (edited >= 0 && i != edited)
            continue;

        if (control_modes[i] == "joint_position")
            sliders_[i]->setValue((int)(valueLabels_[i]->text().toDouble() * 100.0));
        else if (control_modes[i] == "hall_position")
            sliders_[i]->setValue(valueLabels_[i]->text().toInt());
        else
            sliders_[i]->setValue(0);
    }
}

void SidePanel::onMaxVelocityChanged()
{
    if (!mode_box || sliders_.size() < 4)
        return;
    auto curr_mode = mode_box->currentText();
    float max_vel = (float)sliders_[0]->value() / 100.0;
    if (curr_mode == "Velocity")
    {
        sliders_[2]->setRange((int)(-1 * max_vel * 100.0), (int)(max_vel * 100.0));
        valueLbls[2].first->setText(QString::number((-1 * max_vel)));
        valueLbls[2].second->setText(QString::number((max_vel)));
    }
    else if (curr_mode == "MIT")
    {
        sliders_[3]->setRange((int)(-1 * max_vel * 100.0), (int)(max_vel * 100.0));
        valueLbls[3].first->setText(QString::number((-1 * max_vel)));
        valueLbls[3].second->setText(QString::number((max_vel)));
    }
}

void SidePanel::onMaxCurrentChanged()
{
    if (!mode_box || sliders_.size() < 4)
        return;
    auto curr_mode = mode_box->currentText();
    float max_current = (float)sliders_[1]->value() / 100.0;
    float max_torque = 3 / 2 * max_current * act_cfg_.psi_f * (float)act_cfg_.n_pole_pairs;
    if (curr_mode == "Torque")
    {
        sliders_[2]->setRange((int)(-1 * max_torque * 100.0), (int)(max_torque * 100.0));
        valueLbls[2].first->setText(QString::number((-1 * max_torque)));
        valueLbls[2].second->setText(QString::number((max_torque)));
    }
}

void SidePanel::onToggleMode()
{
    const bool toSetup = (btnMode_->text() == "Mode: Follow");
    btnMode_->setText(toSetup ? "Mode: Setup" : "Mode: Follow");
    btnExecute_->setEnabled(toSetup);
    emit notify(btnMode_->text());
}

void SidePanel::onExecute()
{
    emit committed();
}

void SidePanel::onReset()
{
    for (auto *s : sliders_)
        s->setValue(0);
}

// -------- helpers --------
void SidePanel::applyActionValues(const QVector<double> &values)
{
    const int count = std::min({sliders_.size(), values.size(), valueLbls.size()});
    for (int i = 0; i < count; ++i)
    {
        const float min = valueLbls[i].first->text().toFloat() * 100;
        const float max = valueLbls[i].second->text().toFloat() * 100;
        const float v = std::clamp(float(values[i]), min, max);
        sliders_[i]->setValue((int)v);
    }
    emit valuesChanged();
}

void SidePanel::modeChange(bool teleopActive)
{
    // While tele-operation drives the hand the joint controls are read-only.
    // Restoring them must respect the per-joint control mode, otherwise
    // joints parked in zero_torque would become movable again.
    for (int i = 0; i < sliders_.size(); ++i)
    {
        if (teleopActive)
            sliders_[i]->setEnabled(false);
        else
            sliders_[i]->setEnabled(control_modes[i] != "zero_torque");
    }
    for (auto e : valueLabels_)
        e->setReadOnly(teleopActive);
}

void SidePanel::applyCameraData(std::unordered_map<std::string, float> &data, grip::HandSettings *hand_cfg)
{
    // Hot path: this runs once per tele-operation sample (up to ~120 Hz).
    // Use the prebuilt name -> row index map instead of a linear scan that
    // rebuilt a QByteArray (toUtf8) for every single comparison.
    const int count = std::min({nameLabels_.size(), sliders_.size(), valueLbls.size(), control_modes.size()});

    for (const auto &[name, value] : data)
    {
        const int i = sliderIndex_.value(QString::fromStdString(name), -1);
        if (i < 0 || i >= count)
            continue;

        float min = valueLbls[i].first->text().toFloat();
        float max = valueLbls[i].second->text().toFloat();
        float v = std::clamp(float(value), float(min), float(max));

        if (control_modes[i] == "hall_position")
        {
            // Cache the joint -> scale lookup; scanning every struct on every
            // sample made this quadratic.
            auto scaleIt = hallScales_.find(name);
            if (scaleIt == hallScales_.end())
            {
                float scale = 0.0f;
                for (auto [key, vec] : hand_cfg->structs)
                {
                    for (auto joint : vec)
                    {
                        if (joint.name == name)
                            scale = (joint.hall_route.second - joint.hall_route.first) /
                                    (joint.route.second - joint.route.first) / 100.0f;
                    }
                }
                scaleIt = hallScales_.emplace(name, scale).first;
            }
            v = v * scaleIt->second;
        }
        else if (control_modes[i] == "zero_torque")
            continue;

        sliders_[i]->setValue((int)(v * 100));
    }
}

void SidePanel::onControlChanged()
{
    if (!mode_box)
        return;
    auto curr_mode = mode_box->currentText();
    if (curr_mode != "MIT")
    {
        nameLabels_[2]->setText(curr_mode);
        sliders_[3]->setEnabled(false);
        valueLabels_[3]->setEnabled(false);
        nameLabels_[3]->setText("-");
        valueLabels_[2]->setText(QString("0"));
        valueLabels_[3]->setText(QString("0"));
        sliders_[2]->setValue(0);
        sliders_[3]->setValue(0);

        if (curr_mode == "Position")
        {
            sliders_[2]->setRange((int)(act_cfg_.route.first * 100.0), (int)(act_cfg_.route.second * 100.0));
            valueLbls[2].first->setText(QString::number((act_cfg_.route.first)));
            valueLbls[2].second->setText(QString::number((act_cfg_.route.second)));
        }
        else if (curr_mode == "Velocity")
        {
            float max_vel = (float)sliders_[0]->value() / 100.0;
            sliders_[2]->setRange((int)(-1 * max_vel * 100.0), (int)(max_vel * 100.0));
            valueLbls[2].first->setText(QString::number((-1 * max_vel)));
            valueLbls[2].second->setText(QString::number((max_vel)));
        }
        else if (curr_mode == "Torque")
        {
            float max_current = (float)sliders_[1]->value() / 100.0;
            float max_torque = 3 / 2 * max_current * act_cfg_.psi_f * (float)act_cfg_.n_pole_pairs;
            sliders_[2]->setRange((int)(-1 * max_torque * 100.0), (int)(max_torque * 100.0));
            valueLbls[2].first->setText(QString::number((-1 * max_torque)));
            valueLbls[2].second->setText(QString::number((max_torque)));
        }
    }
    else
    {
        nameLabels_[2]->setText("Position");
        sliders_[3]->setEnabled(true);
        valueLabels_[3]->setEnabled(true);
        nameLabels_[3]->setText("Velocity");
        valueLabels_[2]->setText(QString("0"));
        valueLabels_[3]->setText(QString("0"));
        sliders_[2]->setValue(0);
        sliders_[3]->setValue(0);
        float max_vel = (float)sliders_[0]->value() / 100.0;
        sliders_[2]->setRange((int)(act_cfg_.route.first * 100.0), (int)(act_cfg_.route.second * 100.0));
        sliders_[3]->setRange((int)(-1 * max_vel * 100.0), (int)(max_vel * 100.0));
        valueLbls[2].first->setText(QString::number((act_cfg_.route.first)));
        valueLbls[2].second->setText(QString::number((act_cfg_.route.second)));
        valueLbls[3].first->setText(QString::number((-1 * max_vel)));
        valueLbls[3].second->setText(QString::number((max_vel)));
    }
}

std::string SidePanel::get_control_mode()
{
    if (!mode_box)
        return {};
    return mode_box->currentText().toUtf8().constData();
}

void SidePanel::removeAll()
{
    sliders_.clear();
    valueLabels_.clear();
    currValueLabels_.clear();
    nameLabels_.clear();
    valueLbls.clear();
    leds_.clear();
    hallScales_.clear();
    control_modes.clear();
    sliderIndex_.clear();

    if (sliderScroll_)
    {
        if (QWidget *previous = sliderScroll_->takeWidget())
            delete previous;
        rebuildSliderHost();
    }

    clearFeedbackTable();
}



QStringList SidePanel::getControlModes()
{
    return control_modes;
}
