#include "MainWindow.h"
#include "SidePanel.h"
#include <algorithm>
#include <memory>
#include <QTextEdit>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStatusBar>
#include <QTimer>
#include <QString>
#include <QStringList>
#include <QLabel>
#include <QPushButton>
#include <QShortcut>
#include <QKeySequence>
#include <iostream>
#include "MainWidget.hpp"
#include "SideDock.hpp"
#include "ConnectionBar.hpp"
#include "SettingsWindow.h"
#include "CameraWidget.hpp"
#include "ActionPanel.hpp"
#include "TactilePanel.hpp"
#include "LogPanel.hpp"

MainWindow::MainWindow(const grip::AppSettings &cfg,
                       QWidget *parent)
    : QMainWindow(parent), cfg_(cfg)
{
    can_cfg_ = cfg_.can_device;
    buildUI();
    resize(1400, 900);
    setMinimumSize(1100, 700);
}

MainWindow::~MainWindow()
{
    if (plot_timer_)
    {
        plot_timer_->stop();
        delete plot_timer_;
        plot_timer_ = nullptr;
    }

    if (camera_)
        camera_->stopAll();

    if (read_timer_)
    {
        read_timer_->stop();
        delete read_timer_;
        read_timer_ = nullptr;
    }
    if (ctrl_)
    {
        ctrl_->stop_all();
        delete ctrl_;
        ctrl_ = nullptr;
    }
}

void MainWindow::buildUI()
{
    // ---- modules (created once; the docks re-parent them) ----
    ogre_ = new OgreWidget();
    camera_ = new CameraWidget();
    actionFlow_ = new ActionPanel();
    tactile_ = new TactilePanel();

    side_ = new SidePanel(this);
    plotPanel_ = new PlotPanel(this);

    // ---- layered canvas: connection bar + [left dock | 3D | right dock] ----
    canvas_ = new MainWidget(this);
    setCentralWidget(canvas_);
    canvas_->setBackgroundWidget(ogre_);

    connBar_ = new ConnectionBar(canvas_, cfg_.models);
    canvas_->setTopBar(connBar_);

    buildDocks();

    plot_timer_ = new QTimer(this);
    connect(plot_timer_, &QTimer::timeout, this, [this]()
            {
                if (!plotPanel_)
                    return;
                HandStateMap snapshot;
                {
                    std::lock_guard<std::mutex> lk(mtx_);
                    if (ctrl_)
                        snapshot = ctrl_->snapshot();
                }
                plotPanel_->feedData(snapshot);
            });

    /* ---- signal wiring ---- */
    connect(side_, &SidePanel::valuesChanged, this, &MainWindow::onSidePanelValuesChanged);
    connect(side_, &SidePanel::notify, this, &MainWindow::onSidePanelNotify);

    connect(connBar_, &ConnectionBar::deviceConnect, this, &MainWindow::onConnectorDeviceConnect);
    connect(connBar_, &ConnectionBar::modelChange, this, &MainWindow::onConnectorModelChange);
    connect(connBar_, &ConnectionBar::canConfigChanged, this, &MainWindow::onConnectionBarCanConfigChanged);
    // appendLog is overloaded, so select the one-argument form explicitly.
    connect(connBar_, &ConnectionBar::statusMessage, this,
            qOverload<const QString &>(&MainWindow::appendLog));
    connect(connBar_, &ConnectionBar::settingsRequested, this, &MainWindow::openSettings);
    connect(connBar_, &ConnectionBar::enableChanged, this, &MainWindow::onEnableChanged);

    /* ---- settings pop-up: CAN bus + feedback filter ---- */
    settingsWindow_ = new SettingsWindow(this);
    connect(settingsWindow_, &SettingsWindow::canConfigChanged, this, &MainWindow::onCanConfigChanged);
    connect(settingsWindow_, &SettingsWindow::filterChanged, this, &MainWindow::onFilterChanged);
    connect(settingsWindow_, &SettingsWindow::filterReset, this, &MainWindow::onFilterReset);
    connect(settingsWindow_, &SettingsWindow::notify, this,
            qOverload<const QString &>(&MainWindow::appendLog));
    connect(settingsWindow_, &SettingsWindow::simulationModeChanged,
            this, &MainWindow::onSimulationModeChanged);
    connect(settingsWindow_, &SettingsWindow::model3DConfigChanged,
            this, &MainWindow::onModel3DConfigChanged);
    connect(settingsWindow_, &SettingsWindow::viewStyleChanged, this,
            [this](const QString &style)
            {
                ogre_->setViewPreset(style);
                appendLog(QString("3D view navigation: %1.").arg(style));
            });
    connect(settingsWindow_, &SettingsWindow::focalMarkerVisibilityChanged, this,
            [this](bool visible)
            {
                ogre_->setFocalMarkerVisible(visible);
                appendLog(visible ? "Look-at point marker shown."
                                  : "Look-at point marker hidden.");
            });
    settingsWindow_->applyCanConfig(); // seed can_cfg_ with the defaults

    QStringList modelNames;
    for (const auto &m : cfg_.models)
        modelNames << QString::fromStdString(m);
    settingsWindow_->setSoftwareInfo(QString::fromStdString(config_path_), modelNames);

    connect(ogre_, &OgreWidget::initFailed, this, [this](const QString &msg)
            { appendLog(LogLevel::Error, QString("3D view unavailable: %1").arg(msg)); });

    connect(actionFlow_, &ActionPanel::actionUpdate, this, &MainWindow::onActionPanelActionUpdate);
    actionFlow_->setPoseProvider([this]() -> QVector<double>
    {
        const QList<int> deg = side_->jointDegrees();
        QVector<double> pose;
        pose.reserve(deg.size());
        for (int d : deg)
            pose.push_back(double(d));
        return pose;
    });
    connect(camera_, &CameraWidget::dataUpdate, this, [this](const QVector<double> &joints)
            {
                auto full_joints_list = camera_->getFullList();
                std::unordered_map<std::string, float> dof_pos;
                const int count = std::min(joints.size(), (int)full_joints_list.size());
                for (int i = 0; i < count; ++i)
                {
                    const auto joint_name = full_joints_list[i];
                    std::unordered_map<std::string, float> dof_pos_buff = jointsToDofs(joint_name, joints[i]);
                    for (const auto &buff : dof_pos_buff)
                    {
                        const auto &name = buff.first;
                        auto item = dof_pos.find(name);
                        if (item != dof_pos.end())
                            item->second += buff.second;
                        else
                            dof_pos.emplace(name, buff.second);
                    }
                }
                side_->applyCameraData(dof_pos, &hand_cfg_);
            });
    connect(camera_, &CameraWidget::controlSwitch, this, [this](const QString &msg)
            { appendLog(QString("Hand tracking %1.").arg(msg.toLower())); });
    connect(camera_, &CameraWidget::status, this, [this](const QString &s)
            { statusBar()->showMessage(s, 2000); });
    connect(camera_, &CameraWidget::error, this, [this](const QString &e)
            {
                statusBar()->showMessage(QString("Error: %1").arg(e), 5000);
                appendLog(LogLevel::Error, QString("Hand tracking: %1").arg(e)); });

    /* ---- keyboard shortcuts for the panel columns ---- */
    // Ctrl+1..3 focus a page of the left column, Ctrl+4..6 the right one;
    // Ctrl+B toggles both columns down to their tab strips.
    {
        auto addShortcut = [this](const QKeySequence &key, void (MainWindow::*slot)())
        {
            auto *sc = new QShortcut(key, this);
            connect(sc, &QShortcut::activated, this, slot);
        };
        addShortcut(QKeySequence("Ctrl+1"), &MainWindow::showCameraPanel);
        addShortcut(QKeySequence("Ctrl+2"), &MainWindow::showPlotPanel);
        addShortcut(QKeySequence("Ctrl+3"), &MainWindow::showTactilePanel);
        addShortcut(QKeySequence("Ctrl+4"), &MainWindow::showLogPanel);
        addShortcut(QKeySequence("Ctrl+B"), &MainWindow::toggleLeftDock);
        addShortcut(QKeySequence("Ctrl+Shift+B"), &MainWindow::toggleRightDock);
    }

    setConnectedStatus(false);
}

/**
 * Docks (UI design note #3): vertical tabs on the outer edges of the canvas.
 *
 *  - left:  joint control, hand tracking, automation;
 *  - right: joint plots, tactile pairs, log / bus telemetry.
 *
 * The 3D model is deliberately NOT a dock page: it is the central widget and
 * always fills the space between the columns.
 */
void MainWindow::buildDocks()
{
    leftDock_ = new SideDock(Qt::LeftEdge, canvas_);
    leftDock_->addPanel(side_, "Joints", "Joint target sliders");
    leftDock_->addPanel(camera_, "Tracking", "Camera / hand tracking tele-operation");
    leftDock_->addPanel(actionFlow_, "Actions", "Action set sequencer");
    leftDock_->setDockWidth(340);
    canvas_->setLeftDock(leftDock_);
    // The docks float above the 3D view, so a collapse only re-places them; the
    // 3D widget keeps its geometry and the model is never resized.
    connect(leftDock_, &SideDock::collapsedChanged, this,
            [this](bool) { canvas_->relayout(); });

    rightDock_ = new SideDock(Qt::RightEdge, canvas_);
    rightDock_->addPanel(plotPanel_, "Plots", "Joint position over time (one diagram per joint)");
    rightDock_->addPanel(tactile_, "Tactile", "Tactile sensors: point cloud + force");
    rightDock_->addPanel(buildLogPanel(), "Log", "Log and bus telemetry");
    rightDock_->setDockWidth(430);
    canvas_->setRightDock(rightDock_);
    connect(rightDock_, &SideDock::collapsedChanged, this,
            [this](bool) { canvas_->relayout(); });

    // Start with both columns hidden: only their tab strips are shown, so the 3D
    // view gets the full window on launch and the panels are opened on demand by
    // clicking a tab. This runs after every panel has been added, because the
    // strip width is derived from the tab list.
    leftDock_->setCollapsed(true);
    rightDock_->setCollapsed(true);
    canvas_->relayout();
}

/* Log panel: a dock page (not an overlay), so it sits inside the right column
   together with the other tabs. It owns the text log (timestamp + level) and the
   bus telemetry table underneath it. */
QWidget *MainWindow::buildLogPanel()
{
    logPanel_ = new LogPanel(canvas_);
    return logPanel_;
}

void MainWindow::appendLog(const QString &msg)
{
    appendLog(LogLevel::Info, msg);
}

void MainWindow::appendLog(LogLevel level, const QString &msg)
{
    if (logPanel_)
        logPanel_->append(level, msg);
}

void MainWindow::setConnectedStatus(bool connected)
{
    statusBar()->showMessage(connected
        ? QString("Connected: %1").arg(selectedModel_)
        : QString("Not connected"));
}

/* ------------------------------------------------------------------ *
 *  Joint targets
 * ------------------------------------------------------------------ */

void MainWindow::onSidePanelValuesChanged()
{
    if (!ctrl_)
        return;

    const QList<int> deg = side_->jointDegrees();
    const QStringList control_mode = side_->getControlModes();

    std::unordered_map<std::string, float> joint_target;
    const int count = std::min({deg.size(), dof_names.size(), control_mode.size()});
    for (int i = 0; i < count; ++i)
    {
        if (control_mode[i] == "zero_torque")
            continue; // disarmed joints are not commanded

        const float rad = float(deg[i]) * float(M_PI) / 180.0f / 100.0;
        ctrl_->submit_target(dof_names[i].toUtf8().constData(), float(deg[i]));

        const std::unordered_map<std::string, float> joint_pos =
            (control_mode[i] == "hall_position")
                ? dofsToJoints(dof_names[i].toUtf8().constData(), float(deg[i]), true)
                : dofsToJoints(dof_names[i].toUtf8().constData(), rad, false);

        for (const auto &j : joint_pos)
        {
            const auto &name = j.first;
            auto item = joint_target.find(name);
            if (item != joint_target.end())
                item->second += j.second;
            else
                joint_target.emplace(name, j.second);
        }
    }

    if (model3DShowsSource("cmd"))
    {
        for (auto &[key, value] : joint_target)
            ogre_->setJointPosition(key, value);
    }
}



void MainWindow::onSidePanelNotify(const QString &msg)
{
    appendLog(msg);
}

/**
 * Enable/Disable from the connection bar (UI design note #6).
 *
 * Enable  -> every actuated joint is switched to `joint_position`, the sliders
 *            become live and the current positions are pushed right away.
 * Disable -> every joint goes back to `zero_torque` and the sliders are parked.
 */
void MainWindow::onEnableChanged(bool enable)
{
    const char *mode = enable ? "joint_position" : "zero_torque";

    side_->setAllJointsEnabled(enable);

    if (!ctrl_)
    {
        appendLog(enable ? "Hand enabled (no connection: 3D preview only)."
                         : "Hand disabled.");
        return;
    }

    for (const auto &mod : hand_cfg_.structs)
    {
        for (const auto &j : mod.second)
        {
            if (j.id <= 0)
                continue; // passive joint: nothing to command
            ctrl_->submit_control_mode(j.name, mode);
        }
    }

    appendLog(QString("Hand %1: all joints -> %2.")
                  .arg(enable ? "enabled" : "disabled")
                  .arg(mode));

    if (enable)
        onSidePanelValuesChanged(); // push the current targets immediately
}

/* ------------------------------------------------------------------ *
 *  Connection lifecycle
 * ------------------------------------------------------------------ */

void MainWindow::onConnectorDeviceConnect(const QString &msg)
{
    if (msg == "Connect")
        onDeviceConnect();
    else
        onDeviceDisconnect();
}

void MainWindow::onDeviceConnect()
{
    if (selectedModel_.isEmpty())
    {
        appendLog(LogLevel::Warning, "Please select a hand model before connecting.");
        connBar_->onChange(false);
        return;
    }

    try
    {
        dev_cfg_ = grip::load_device_settings(config_path_);
        hand_cfg_ = grip::load_hand_settings(config_path_);

        ctrl_ = new grip::ControlInterface(hand_cfg_, std::chrono::milliseconds(10));

        ctrl_->set_filter_enabled(settingsWindow_->filterEnabled());
        ctrl_->set_filter_params(settingsWindow_->filterFreq(), settingsWindow_->filterMinCutoff(),
                                 settingsWindow_->filterBeta(), settingsWindow_->filterDCutoff());

        if (withCan)
        {
            ctrl_->start_bus(can_cfg_);
            appendLog("Device Connected. ");
            appendLog(QString("Device id: 0x%1 (slave id %2), hand side: %3")
                          .arg(QString::number(ctrl_->dev_id(), 16))
                          .arg(ctrl_->hand_id())
                          .arg(QString::fromStdString(ctrl_->hand_side())));
            ctrl_->start_control_loop();

            read_timer_ = new QTimer(this);
            connect(read_timer_, &QTimer::timeout, this, &MainWindow::readJointAngles);
            read_timer_->start(10);
        }

        // Only joints with a positive id are directly actuated. The predicate
        // must match SidePanel::loadHandControllor() so that dof_names and the
        // slider list stay index-aligned when targets are submitted.
        dof_names.clear();
        joints_inverted_index.clear();
        dof_map.clear();
        for (const auto &[key, value] : hand_cfg_.structs)
        {
            for (auto &j : value)
            {
                if (j.id > 0)
                    dof_names << QString::fromStdString(j.name);
                joints_inverted_index.emplace(j.name, key);
                dof_map.emplace(j.id, j.name);
            }
        }

        side_->loadHandControllor(&hand_cfg_);
        loadModel3D();

        std::unique_ptr<ActionStore> store(new ActionStore(action_path_.c_str()));
        store->load();
        actionFlow_->setStore(store.get());
        store.release();

        if (plot_timer_ && !plot_timer_->isActive())
            plot_timer_->start(20);

        connBar_->onChange(true);
        settingsWindow_->setConnected(true);
        setConnectedStatus(true);
    }
    catch (const std::exception &e)
    {
        const QString qerr = QString(e.what());
        appendLog(LogLevel::Error, qerr);
        connBar_->onChange(false);
        settingsWindow_->setConnected(false);
        setConnectedStatus(false);
    }
}

void MainWindow::onDeviceDisconnect()
{
    actionFlow_->setStore(nullptr);
    side_->removeAll();

    if (plot_timer_)
        plot_timer_->stop();
    if (plotPanel_)
    {
        plotPanel_->clearDiagrams();
        plotPanel_->resetHistory();
    }
    if (tactile_)
        tactile_->resetHistory();

    if (ctrl_)
    {
        if (read_timer_)
        {
            read_timer_->stop();
            delete read_timer_;
            read_timer_ = nullptr;
        }
        delete ctrl_;
        ctrl_ = nullptr;
    }
    connBar_->onChange(false);
    settingsWindow_->setConnected(false);
    setConnectedStatus(false);
    // The telemetry below belongs to the closed bus: show it as unavailable
    // rather than leaving the last reading on screen as if it were live.
    if (logPanel_)
        logPanel_->clearBusTelemetry();
    busFaultActive_ = false;
    ogre_->clearScene();
    dof_names.clear();
    joints_inverted_index.clear();
    dof_map.clear();
    dev_cfg_ = grip::DeviceSettings();
    hand_cfg_ = grip::HandSettings();
    act_cfg_ = grip::ActuatorSettings();
}

void MainWindow::onSimulationModeChanged(bool simulation)
{
    withCan = !simulation;
    appendLog(simulation ? "Simulation mode: no hardware connected."
                         : "Simulation mode off: CAN device will be used.");
}

void MainWindow::onCanConfigChanged(int device_type, int device_index, int channel_index,
                                    int core_clock_hz, bool termination_enabled,
                                    int arb_tseg1, int arb_tseg2, int arb_sjw, int arb_brp,
                                    int data_tseg1, int data_tseg2, int data_sjw, int data_brp)
{
    can_cfg_.device_type = device_type;
    can_cfg_.device_index = device_index;
    can_cfg_.channel_index = channel_index;
    can_cfg_.core_clock_hz = core_clock_hz;
    can_cfg_.termination_enabled = termination_enabled;
    can_cfg_.bit_timing.arbitration = {arb_tseg1, arb_tseg2, arb_sjw, arb_brp};
    can_cfg_.bit_timing.data_phase = {data_tseg1, data_tseg2, data_sjw, data_brp};

    connBar_->setDeviceLocation(device_index, channel_index);
}

void MainWindow::onConnectionBarCanConfigChanged(int device_index, int channel_index)
{
    can_cfg_.device_index = device_index;
    can_cfg_.channel_index = channel_index;
    if (settingsWindow_)
        settingsWindow_->setDeviceLocation(device_index, channel_index);
}

void MainWindow::onFilterChanged(bool enabled, double freq, double min_cutoff,
                                 double beta, double d_cutoff)
{
    if (!ctrl_)
        return;
    ctrl_->set_filter_enabled(enabled);
    ctrl_->set_filter_params(freq, min_cutoff, beta, d_cutoff);
}

void MainWindow::onFilterReset()
{
    if (!ctrl_)
        return;
    ctrl_->reset_filters();
    appendLog("Filter state reset.");
}

/* ------------------------------------------------------------------ *
 *  3D model: a single scene driven by one data source
 * ------------------------------------------------------------------ */

void MainWindow::loadModel3D()
{
    if (!ogre_ || hand_cfg_.urdf_file.empty())
        return;

    model3DUseFeedback_ = settingsWindow_ ? settingsWindow_->useFeedbackData() : model3DUseFeedback_;

    const bool ok = ogre_->loadURDF(hand_cfg_.urdf_file);
    if (ok)
    {
        appendLog(QString("3D model loaded (source: %1).")
                      .arg(model3DUseFeedback_ ? "feedback" : "command"));
    }
    else
    {
        appendLog(LogLevel::Error,
                  QString("3D model load failed: %1")
                      .arg(QString::fromStdString(hand_cfg_.urdf_file)));
    }
}

bool MainWindow::model3DShowsSource(const char *source) const
{
    return model3DUseFeedback_ ? (std::strcmp(source, "fb") == 0)
                               : (std::strcmp(source, "cmd") == 0);
}

void MainWindow::onModel3DConfigChanged(bool use_feedback)
{
    if (hand_cfg_.urdf_file.empty())
        return; // nothing loaded yet: the choice is applied on the next connect

    // Applying unrelated settings re-emits this signal: only rebuild the scene
    // when the choice actually changed.
    if (use_feedback == model3DUseFeedback_)
        return;

    model3DUseFeedback_ = use_feedback;
    loadModel3D();
}

void MainWindow::openSettings()
{
    if (!settingsWindow_)
        return;
    settingsWindow_->show();
    settingsWindow_->raise();
    settingsWindow_->activateWindow();
}

void MainWindow::onActionPanelActionUpdate(const QString &msg)
{
    const QVector<double> values = actionFlow_->actionValues(msg);
    if (values.isEmpty())
        return;
    side_->applyActionValues(values);
}

/* ------------------------------------------------------------------ *
 *  Feedback loop
 * ------------------------------------------------------------------ */

void MainWindow::readJointAngles()
{
    HandStateMap state = ctrl_->snapshot();
    std::unordered_map<std::string, float> joint_target;

    for (auto &[group_name, joint_group] : state)
    {
        for (auto &[joint_name, data] : joint_group.joints)
        {
            const float rad = float(data.position) * float(M_PI) / 180.0f;
            const std::unordered_map<std::string, float> joint_pos =
                dofsToJoints(joint_name, rad, false);
            for (const auto &j : joint_pos)
            {
                const auto &name = j.first;
                auto item = joint_target.find(name);
                if (item != joint_target.end())
                    item->second += j.second;
                else
                    joint_target.emplace(name, j.second);
            }
        }
    }

    if (model3DShowsSource("fb"))
    {
        for (auto &[key, value] : joint_target)
            ogre_->setJointPosition(key, value);
    }

    // Bus telemetry feeds the table under the log text box. This runs in the
    // 10 ms loop, so it must not append to the text log: a line per cycle would
    // bury everything else within seconds.
    if (ctrl_ && logPanel_)
    {
        const BusData bus = ctrl_->bus_info();
        const bool fault = bus.fault_code != 0;
        logPanel_->setBusTelemetry(double(bus.bus_voltage), int(bus.bus_current),
                                   double(bus.temp1_cC),
                                   double(bus.temp2_cC),
                                   quint16(bus.fault_code), fault);

        // Report a fault once, when it appears, instead of every cycle.
        if (fault && !busFaultActive_)
            appendLog(LogLevel::Error, QString("Bus fault 0x%1").arg(bus.fault_code, 0, 16));
        else if (!fault && busFaultActive_)
            appendLog(LogLevel::Info, "Bus fault cleared.");
        busFaultActive_ = fault;
    }
}

/* ------------------------------------------------------------------ *
 *  Panel visibility
 * ------------------------------------------------------------------ */

void MainWindow::onConnectorModelChange(const QString &model_name)
{
    if (model_name.isEmpty() || model_name.startsWith("-"))
    {
        selectedModel_.clear();
        config_path_.clear();
        action_path_.clear();
        return;
    }

    selectedModel_ = model_name;
    config_path_ = std::string("configs/config_") + model_name.toUtf8().constData() + std::string(".yaml");
    action_path_ = std::string("data/action_") + model_name.toUtf8().constData() + std::string(".json");
}

void MainWindow::showCameraPanel()
{
    if (leftDock_)
        leftDock_->showPanel(camera_);
}

void MainWindow::showPlotPanel()
{
    if (rightDock_)
        rightDock_->showPanel(plotPanel_);
}

void MainWindow::showTactilePanel()
{
    if (rightDock_)
        rightDock_->showPanel(tactile_);
}

void MainWindow::showLogPanel()
{
    if (rightDock_ && logPanel_)
        rightDock_->showPanel(logPanel_);
}

void MainWindow::toggleLeftDock()
{
    if (leftDock_)
        leftDock_->setCollapsed(!leftDock_->isCollapsed());
}

void MainWindow::toggleRightDock()
{
    if (rightDock_)
        rightDock_->setCollapsed(!rightDock_->isCollapsed());
}

/* ------------------------------------------------------------------ *
 *  Joint <-> DOF mapping helpers
 * ------------------------------------------------------------------ */

std::unordered_map<std::string, float> MainWindow::dofsToJoints(const std::string dof_name, float value, bool is_hall)
{
    auto module_name = joints_inverted_index.find(dof_name);
    if (module_name == joints_inverted_index.end())
        return {};
    auto module_struct = hand_cfg_.structs.find(module_name->second);
    if (module_struct == hand_cfg_.structs.end())
        return {};
    auto ms = module_struct->second;
    std::vector<float> map;
    float joint_rad = value;
    for (auto &j : ms)
    {
        if (j.name == dof_name)
        {
            map = j.joint_feedback_map;
            if (is_hall)
                joint_rad = value / (j.hall_route.second - j.hall_route.first) *
                            (j.route.second - j.route.first) / 180.0 * float(M_PI);
            break;
        }
    }
    if (map.size() == 0)
        return {};

    std::unordered_map<std::string, float> result;
    const int count = std::min(map.size(), ms.size());
    for (int counter = 0; counter < count; ++counter)
        result.emplace(ms[counter].name, joint_rad * map[counter]);
    return result;
}

std::unordered_map<std::string, float> MainWindow::jointsToDofs(std::string joint_name, float value)
{
    auto module_name = joints_inverted_index.find(joint_name);
    if (module_name == joints_inverted_index.end())
        return {};
    auto module_struct = hand_cfg_.structs.find(module_name->second);
    if (module_struct == hand_cfg_.structs.end())
        return {};
    auto ms = module_struct->second;
    size_t idx = 0;
    for (auto j : ms)
    {
        if (j.name == joint_name)
            break;
        idx += 1;
    }
    if (idx >= ms.size())
        return {};
    std::unordered_map<std::string, float> result;
    for (auto j : ms)
    {
        if (j.id <= 0)
            continue;
        if (idx >= j.joint_feedback_map.size())
            continue;
        if (j.name != joint_name)
            continue;
        result.emplace(j.name, j.joint_feedback_map[idx] * value);
    }
    return result;
}
