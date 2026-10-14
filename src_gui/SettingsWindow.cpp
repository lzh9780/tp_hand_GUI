#include "SettingsWindow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextCursor>
#include <QFormLayout>
#include <QGroupBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QTabWidget>
#include <QTextBrowser>
#include <QComboBox>
#include <QSysInfo>
#include <QDateTime>
#include <QtGlobal>

namespace
{
QSpinBox *makeSpin(int value, int min, int max, QWidget *parent)
{
    auto *s = new QSpinBox(parent);
    s->setRange(min, max);
    s->setValue(value);
    s->setFixedWidth(90);
    return s;
}

QDoubleSpinBox *makeDoubleSpin(double value, double min, double max,
                               int decimals, double step, QWidget *parent)
{
    auto *s = new QDoubleSpinBox(parent);
    s->setRange(min, max);
    s->setDecimals(decimals);
    s->setSingleStep(step);
    s->setValue(value);
    s->setFixedWidth(90);
    return s;
}

/* tseg1 / tseg2 / sjw / brp in one compact row */
QWidget *makeTimingRow(QSpinBox *tseg1, QSpinBox *tseg2, QSpinBox *sjw, QSpinBox *brp)
{
    auto *row = new QWidget;
    auto *lyt = new QHBoxLayout(row);
    lyt->setContentsMargins(0, 0, 0, 0);
    lyt->setSpacing(4);

    auto add = [&](const char *name, QSpinBox *box)
    {
        auto *cell = new QVBoxLayout();
        auto *lbl = new QLabel(name);
        lbl->setAlignment(Qt::AlignHCenter);
        lbl->setStyleSheet("color: gray; font-size: 9px;");
        cell->addWidget(lbl);
        cell->addWidget(box);
        lyt->addLayout(cell);
    };
    add("tseg1", tseg1);
    add("tseg2", tseg2);
    add("sjw", sjw);
    add("brp", brp);
    lyt->addStretch(1);
    return row;
}
} // namespace

SettingsWindow::SettingsWindow(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Settings"));
    setModal(false);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    tabs_ = new QTabWidget(this);

    auto *canTab = new QWidget(tabs_);
    auto *canRoot = new QVBoxLayout(canTab);
    canRoot->setContentsMargins(6, 6, 6, 6);
    buildCanSection(canRoot);
    tabs_->addTab(canTab, tr("CAN"));

    auto *filterTab = new QWidget(tabs_);
    auto *filterRoot = new QVBoxLayout(filterTab);
    filterRoot->setContentsMargins(6, 6, 6, 6);
    buildFilterSection(filterRoot);
    tabs_->addTab(filterTab, tr("Filter"));

    auto *modelTab = new QWidget(tabs_);
    auto *modelRoot = new QVBoxLayout(modelTab);
    modelRoot->setContentsMargins(6, 6, 6, 6);
    buildModelSection(modelRoot);
    tabs_->addTab(modelTab, tr("3D Model"));

    auto *infoTab = new QWidget(tabs_);
    auto *infoRoot = new QVBoxLayout(infoTab);
    infoRoot->setContentsMargins(6, 6, 6, 6);
    buildSoftwareSection(infoRoot);
    tabs_->addTab(infoTab, tr("Software information"));

    root->addWidget(tabs_, 1);

    auto *btnRow = new QHBoxLayout;
    auto *applyBtn = new QPushButton(tr("Apply"), this);
    auto *resetFilterBtn = new QPushButton(tr("Reset filter state"), this);
    auto *closeBtn = new QPushButton(tr("Close"), this);
    btnRow->addWidget(applyBtn);
    btnRow->addWidget(resetFilterBtn);
    btnRow->addStretch(1);
    btnRow->addWidget(closeBtn);
    root->addLayout(btnRow);

    connect(applyBtn, &QPushButton::clicked, this, &SettingsWindow::onApply);
    connect(resetFilterBtn, &QPushButton::clicked, this, &SettingsWindow::onResetFilterState);
    connect(closeBtn, &QPushButton::clicked, this, &SettingsWindow::accept);
}

void SettingsWindow::buildCanSection(QVBoxLayout *root)
{
    canGroup_ = new QGroupBox(tr("CAN Bus"), this);
    auto *form = new QFormLayout(canGroup_);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setContentsMargins(12, 14, 12, 12);
    form->setSpacing(8);

    deviceType_ = makeSpin(33, 0, 10000, canGroup_);
    coreClock_ = makeSpin(60000000, 1000000, 200000000, canGroup_);
    coreClock_->setSingleStep(1000000);
    terminationBox = new QCheckBox(tr("Enable 120Ω termination"), canGroup_);
    terminationBox->setChecked(true);

    arbTseg1_ = makeSpin(14, 0, 31, canGroup_);
    arbTseg2_ = makeSpin(3, 0, 15, canGroup_);
    arbSjw_ = makeSpin(3, 0, 7, canGroup_);
    arbBrp_ = makeSpin(2, 0, 1024, canGroup_);

    dataTseg1_ = makeSpin(1, 0, 31, canGroup_);
    dataTseg2_ = makeSpin(0, 0, 15, canGroup_);
    dataSjw_ = makeSpin(0, 0, 7, canGroup_);
    dataBrp_ = makeSpin(2, 0, 1024, canGroup_);

    // Device index / channel are edited on the top connection bar, not here.
    form->addRow(tr("Device type:"), deviceType_);
    form->addRow(tr("Core clock (Hz):"), coreClock_);
    form->addRow("", terminationBox);
    form->addRow(tr("Arbitration:"),
                 makeTimingRow(arbTseg1_, arbTseg2_, arbSjw_, arbBrp_));
    form->addRow(tr("Data phase:"),
                 makeTimingRow(dataTseg1_, dataTseg2_, dataSjw_, dataBrp_));

    // Simulation mode used to live in the Connector panel.
    simulationBox_ = new QCheckBox(tr("Simulation mode (no hardware connected)"), canGroup_);
    simulationBox_->setToolTip(tr("Run without a CAN bus: the GUI stays fully "
                                  "interactive but no commands reach the hand."));
    form->addRow("", simulationBox_);
    connect(simulationBox_, &QCheckBox::toggled, this, &SettingsWindow::simulationModeChanged);

    root->addWidget(canGroup_);
}

void SettingsWindow::buildModelSection(QVBoxLayout *root)
{
    auto *group = new QGroupBox(tr("3D model"), this);
    auto *form = new QFormLayout(group);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setContentsMargins(12, 14, 12, 12);
    form->setSpacing(8);

    model3DMode_ = new QComboBox(group);
    model3DMode_->addItem(tr("Single model"), false);
    model3DMode_->addItem(tr("Dual model (command + feedback)"), true);
    model3DMode_->setCurrentIndex(1); // keep the previous behaviour: two models
    model3DMode_->setToolTip(tr("Dual loads the URDF twice so the commanded and the "
                                "measured pose can be compared side by side."));

    model3DSource_ = new QComboBox(group);
    model3DSource_->addItem(tr("Feedback data"), true);
    model3DSource_->addItem(tr("Command data"), false);
    model3DSource_->setCurrentIndex(0);
    model3DSource_->setToolTip(tr("Which values drive the model when only one is loaded."));

    viewStyle_ = new QComboBox(group);
    viewStyle_->addItem(tr("Orbit"), QStringLiteral("Orbit"));
    viewStyle_->addItem(tr("FPS (first person)"), QStringLiteral("FPS"));
    
    viewStyle_->setToolTip(tr("Same two controllers as RViz: Orbit rotates around the look-at "
                              "point, FPS looks around and moves with WASD."));

    showFocalMarker_ = new QCheckBox(tr("Show look-at point"), group);
    showFocalMarker_->setChecked(true);
    showFocalMarker_->setToolTip(tr("Draw a marker on the point the camera orbits around "
                                    "(RViz' focal point). Zoom never moves it."));

    form->addRow(tr("Model count:"), model3DMode_);
    form->addRow(tr("Single model driven by:"), model3DSource_);
    form->addRow(tr("Navigation:"), viewStyle_);
    form->addRow(QString(), showFocalMarker_);

    auto *hint = new QLabel(tr("Dual: left model follows the commands, right model the feedback. "
                               "Single: one model only — pick which data drives it.\n"
                               "View: Z is up (ROS/RViz convention). Left drag = orbit / look, "
                               "middle (or shift+left) drag = move the focal point, wheel or "
                               "right drag = zoom, WASD/QE = move, R = reset view."),
                            group);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: gray; font-size: 9px;");
    form->addRow("", hint);

    root->addWidget(group);
    root->addStretch(1);

    connect(model3DMode_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &SettingsWindow::onModelModeChanged);
    onModelModeChanged(model3DMode_->currentIndex());
}

void SettingsWindow::onModelModeChanged(int index)
{
    // The data source only matters when a single model is used.
    const bool dual = (index == 1);
    model3DSource_->setEnabled(!dual);
}

void SettingsWindow::buildSoftwareSection(QVBoxLayout *root)
{
    infoView_ = new QTextBrowser;
    infoView_->setReadOnly(true);
    infoView_->setOpenExternalLinks(false);
    infoView_->setFrameShape(QFrame::NoFrame);
    infoView_->setMinimumHeight(220);

    auto addLine = [this](const QString &key, const QString &value)
    {
        infoView_->append(QString("<b>%1:</b> %2").arg(key.toHtmlEscaped(), value.toHtmlEscaped()));
    };

    addLine(tr("Application"), QStringLiteral("grip_gui (tp hand interface)"));
    addLine(tr("Build date"), QStringLiteral("%1 %2").arg(__DATE__, __TIME__));
    addLine(tr("Qt (compiled against)"), QStringLiteral(QT_VERSION_STR));
    addLine(tr("Qt (running on)"), QString(qVersion()));
    addLine(tr("Compiler"), QStringLiteral("%1").arg(
#ifdef __VERSION__
        __VERSION__
#else
        "unknown"
#endif
    ));
    addLine(tr("System"), QStringLiteral("%1 %2 (%3)")
                               .arg(QSysInfo::productType(), QSysInfo::productVersion(),
                                    QSysInfo::currentCpuArchitecture()));
    addLine(tr("Kernel"), QSysInfo::kernelType() + QStringLiteral(" ") + QSysInfo::kernelVersion());
    addLine(tr("CAN driver"), QStringLiteral("libusbcanfd.so (ZLG USBCANFD)"));
    infoView_->append(QString()); // placeholder, filled by setSoftwareInfo()

    root->addWidget(infoView_, 1);
}

void SettingsWindow::buildFilterSection(QVBoxLayout *root)
{
    filterGroup_ = new QGroupBox(tr("Feedback filter"), this);
    auto *form = new QFormLayout(filterGroup_);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setContentsMargins(12, 14, 12, 12);
    form->setSpacing(8);

    filterEnable_ = new QCheckBox(tr("Enable One Euro filter"), filterGroup_);
    filterEnable_->setChecked(true);
    filterEnable_->setToolTip(tr("Smooths the feedback shown in the GUI. Off = raw values."));

    filterFreq_ = makeDoubleSpin(100.0, 1.0, 1000.0, 1, 10.0, filterGroup_);
    filterMinCutoff_ = makeDoubleSpin(1.0, 0.01, 50.0, 2, 0.1, filterGroup_);
    filterBeta_ = makeDoubleSpin(0.05, 0.0, 2.0, 3, 0.005, filterGroup_);
    filterDCutoff_ = makeDoubleSpin(1.0, 0.01, 50.0, 2, 0.1, filterGroup_);

    form->addRow("", filterEnable_);
    form->addRow(tr("Sample rate (Hz):"), filterFreq_);
    form->addRow(tr("Min cutoff (Hz):"), filterMinCutoff_);
    form->addRow(tr("Beta:"), filterBeta_);
    form->addRow(tr("d cutoff (Hz):"), filterDCutoff_);

    auto *hint = new QLabel(tr("Lower min cutoff = smoother but more lag; "
                               "higher beta = less lag while moving fast."),
                            filterGroup_);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: gray; font-size: 9px;");
    form->addRow("", hint);

    root->addWidget(filterGroup_);
}

void SettingsWindow::setCanConfig(int device_type, int device_index, int channel_index,
                                  int core_clock_hz, bool termination_enabled,
                                  int arb_tseg1, int arb_tseg2, int arb_sjw, int arb_brp,
                                  int data_tseg1, int data_tseg2, int data_sjw, int data_brp)
{
    const bool blocked = signalsBlocked();
    blockSignals(true);
    deviceType_->setValue(device_type);
    deviceLocation_ = qMakePair(device_index, channel_index);
    coreClock_->setValue(core_clock_hz);
    terminationBox->setChecked(termination_enabled);
    arbTseg1_->setValue(arb_tseg1);
    arbTseg2_->setValue(arb_tseg2);
    arbSjw_->setValue(arb_sjw);
    arbBrp_->setValue(arb_brp);
    dataTseg1_->setValue(data_tseg1);
    dataTseg2_->setValue(data_tseg2);
    dataSjw_->setValue(data_sjw);
    dataBrp_->setValue(data_brp);
    blockSignals(blocked);
}

void SettingsWindow::emitCanConfig()
{
    // Device index / channel come from the top connection bar, so echo back the
    // last known values instead of owning spin boxes for them.
    emit canConfigChanged(deviceType_->value(), deviceLocation_.first, deviceLocation_.second,
                          coreClock_->value(), terminationBox->isChecked(),
                          arbTseg1_->value(), arbTseg2_->value(), arbSjw_->value(), arbBrp_->value(),
                          dataTseg1_->value(), dataTseg2_->value(), dataSjw_->value(), dataBrp_->value());
}

void SettingsWindow::applyCanConfig()
{
    emitCanConfig();
}

void SettingsWindow::applyFilter()
{
    emit filterChanged(filterEnable_->isChecked(), filterFreq_->value(),
                       filterMinCutoff_->value(), filterBeta_->value(), filterDCutoff_->value());
}

void SettingsWindow::onApply()
{
    // CAN parameters are frozen while a device is connected: they are read once
    // when the bus is opened, so changing them here would have no effect.
    if (canGroup_->isEnabled())
    {
        emitCanConfig();
        emit notify(tr("CAN settings applied: type=%1, clock=%2 Hz, "
                       "termination=%3, arb=%4/%5/%6/%7, data=%8/%9/%10/%11")
                        .arg(deviceType_->value())
                        .arg(coreClock_->value())
                        .arg(terminationBox->isChecked() ? "on" : "off")
                        .arg(arbTseg1_->value()).arg(arbTseg2_->value())
                        .arg(arbSjw_->value()).arg(arbBrp_->value())
                        .arg(dataTseg1_->value()).arg(dataTseg2_->value())
                        .arg(dataSjw_->value()).arg(dataBrp_->value()));
    }
    else
    {
        emit notify(tr("CAN settings are locked while a device is connected."));
    }

    emit model3DConfigChanged(dualModel(), useFeedbackData());
    emit viewStyleChanged(viewStyle());
    emit focalMarkerVisibilityChanged(showFocalMarker());
    emit notify(tr("3D model: %1%2")
                    .arg(dualModel() ? tr("dual (command + feedback)")
                                     : tr("single"))
                    .arg(dualModel() ? QString()
                                     : tr(", driven by %1").arg(useFeedbackData() ? tr("feedback data")
                                                                                  : tr("command data"))));

    applyFilter();
    emit notify(tr("Filter settings applied: %1, freq=%2 Hz, min cutoff=%3, beta=%4, d cutoff=%5")
                    .arg(filterEnable_->isChecked() ? "enabled" : "disabled")
                    .arg(filterFreq_->value())
                    .arg(filterMinCutoff_->value())
                    .arg(filterBeta_->value())
                    .arg(filterDCutoff_->value()));
}

void SettingsWindow::onResetFilterState()
{
    emit filterReset();
    emit notify(tr("Filter state reset."));
}

void SettingsWindow::setConnected(bool connected)
{
    // CAN parameters and the simulation switch are read when the bus is opened,
    // so both are frozen while a device is connected.
    canGroup_->setEnabled(!connected);
}

void SettingsWindow::setDeviceLocation(int device_index, int channel_index)
{
    deviceLocation_ = qMakePair(device_index, channel_index);
}

void SettingsWindow::setSoftwareInfo(const QString &configPath, const QStringList &models)
{
    if (!infoView_)
        return;

    auto addLine = [this](const QString &key, const QString &value)
    {
        infoView_->append(QString("<b>%1:</b> %2").arg(key.toHtmlEscaped(), value.toHtmlEscaped()));
    };

    addLine(tr("Config file"), configPath.isEmpty() ? tr("(none)") : configPath);
    addLine(tr("Models"), models.isEmpty() ? tr("(none)") : models.join(QStringLiteral(", ")));
    addLine(tr("Generated"), QDateTime::currentDateTime().toString(Qt::ISODate));
}

bool SettingsWindow::simulationMode() const
{
    return simulationBox_->isChecked();
}

bool SettingsWindow::dualModel() const
{
    return model3DMode_ && model3DMode_->currentData().toBool();
}

bool SettingsWindow::useFeedbackData() const
{
    return model3DSource_ && model3DSource_->currentData().toBool();
}

QString SettingsWindow::viewStyle() const
{
    return viewStyle_ ? viewStyle_->currentData().toString() : QStringLiteral("Orbit");
}

bool SettingsWindow::showFocalMarker() const
{
    return showFocalMarker_ ? showFocalMarker_->isChecked() : true;
}

bool SettingsWindow::filterEnabled() const
{
    return filterEnable_->isChecked();
}

double SettingsWindow::filterFreq() const
{
    return filterFreq_->value();
}

double SettingsWindow::filterMinCutoff() const
{
    return filterMinCutoff_->value();
}

double SettingsWindow::filterBeta() const
{
    return filterBeta_->value();
}

double SettingsWindow::filterDCutoff() const
{
    return filterDCutoff_->value();
}
