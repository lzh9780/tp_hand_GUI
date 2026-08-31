#include "MainWindow.h"
#include "SidePanel.h"
#include <algorithm>
#include <memory>
#include <QTextEdit>
#include <QSplitter>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStatusBar>
#include <QTimer>
#include <QString>
#include <QStringList>
#include <QMenuBar>
#include <QTabWidget>
#include <QToolBar>
#include <QLabel>
#include <QPushButton>
#include <iostream>
#include "DraggableTabWidget.hpp"
#include "Connector.hpp"
#include "CameraWidget.hpp"
#include "ActionPanel.hpp"

MainWindow::MainWindow(const grip::AppSettings &cfg,
                       QWidget *parent)
    : QMainWindow(parent), cfg_(cfg)
{
    buildUI();
    resize(1400, 900);
    setMinimumSize(1100, 700);
}

MainWindow::~MainWindow()
{
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
    central = new QWidget(this);
    auto *rootLayout = new QHBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // ---- Modules (created once, hosted by the tab area on demand) ----
    connect_ = new Connector(this, cfg_.models);
    ogre_ = new OgreWidget();
    motor_ = new MotorPanel();
    camera_ = new CameraWidget();
    actionFlow_ = new ActionPanel();
    tactile_ = new TactileVisual();
    tactile_->setPalmImage(QPixmap("./images/white.jpeg"));

    // ---- Left: joint / action control panel ----
    side_ = new SidePanel(this);
    side_->setMinimumWidth(320);

    // ---- Right: work area (tabs) on top, log console at the bottom ----
    // DraggableTabWidget installs its own tab bar that already handles
    // reordering, so leave QTabBar::movable at its default.
    upperTabs = new DraggableTabWidget(this);
    upperTabs->setTabsClosable(true);
    upperTabs->setDocumentMode(true);

    // Open the panels that follow the actual workflow:
    // connect the hand → watch the 3D model → run action sets.
    upperTabs->addTab(connect_, "Connector");
    upperTabs->addTab(ogre_, "3D Model");
    upperTabs->addTab(actionFlow_, "Action Sets");
    upperTabs->setCurrentIndex(0);

    buildLogPanel();

    auto *workArea = new QSplitter(Qt::Vertical, this);
    workArea->addWidget(upperTabs);
    workArea->addWidget(logPanel_);
    workArea->setStretchFactor(0, 1);
    workArea->setStretchFactor(1, 0);
    workArea->setSizes({700, 200});

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(side_);
    splitter->addWidget(workArea);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({360, 1020});

    rootLayout->addWidget(splitter);

    central->setLayout(rootLayout);
    setCentralWidget(central);

    buildToolBar();
    setConnectedStatus(false);

    connect(side_, &SidePanel::valuesChanged, this, &MainWindow::onSidePanelValuesChanged);
    connect(side_, &SidePanel::committed, this, &MainWindow::onSidePanelCommitted);
    connect(side_, &SidePanel::notify, this, &MainWindow::onSidePanelNotify);
    connect(side_, &SidePanel::controlChanged, this, &MainWindow::onSidePanelControlChanged);
    connect(side_, &SidePanel::openAdvancePanel, this, &MainWindow::openMotorControlTab);
    connect(motor_, &MotorPanel::mode_change, this, &MainWindow::onJointPanelControlModeChange);
    connect(motor_, &MotorPanel::vel_ref_change, this, &MainWindow::onJointPanelVelRefModeChange);
    connect(motor_, &MotorPanel::torque_ref_change, this, &MainWindow::onJointPanelTorqueRefModeChange);
    connect(connect_, &Connector::deviceConnect, this, &MainWindow::onConnectorDeviceConnect);
    // connect(camera_, &CameraWidget::controlSwitch, this, &MainWindow::onCameraWidgetControlSwitch);
    connect(connect_, &Connector::modelChange, this, &MainWindow::onConnectorModelChange);
    connect(connect_, &Connector::stateCheck, this, &MainWindow::onConnectorStateCheck);
    connect(upperTabs, &QTabWidget::tabCloseRequested, this, &MainWindow::onUpperTabCloseRequested);
    connect(ogre_, &OgreWidget::initFailed, this, [this](const QString &msg)
            { appendLog(QString("3D view unavailable: %1").arg(msg)); });
    // The action panel owns the store and therefore resolves the joint values
    // itself; the side panel only knows how to push targets to the sliders.
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
            { side_->modeChange(msg == "Start"); });
    connect(camera_, &CameraWidget::status, this, [this](const QString &s)
            { statusBar()->showMessage(s, 2000); });
    connect(camera_, &CameraWidget::error, this, [this](const QString &e)
            {
                statusBar()->showMessage(QString("Error: %1").arg(e), 5000);
                appendLog(QString("Error: %1").arg(e)); });
}

void MainWindow::buildToolBar()
{
    toolBar_ = new QToolBar("Panels", this);
    toolBar_->setMovable(false);
    toolBar_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    addToolBar(Qt::TopToolBarArea, toolBar_);

    actConnector_  = new QAction("Connector", this);
    actModel3D_    = new QAction("3D Model", this);
    actActionSets_ = new QAction("Action Sets", this);
    actJointModes_ = new QAction("Joint Modes", this);
    actHandDetect_ = new QAction("Hand Detect", this);
    actTactile_    = new QAction("Tactile", this);
    actDiagram_    = new QAction("New Diagram", this);
    actLog_        = new QAction("Log", this);
    actLog_->setCheckable(true);
    actLog_->setChecked(true);

    actConnector_->setToolTip("Show the device connector");
    actModel3D_->setToolTip("Show the 3D visualization of the hand");
    actActionSets_->setToolTip("Show the action set sequencer");
    actJointModes_->setToolTip("Show per-joint control modes and limits");
    actHandDetect_->setToolTip("Show tele-operation / hand tracking");
    actTactile_->setToolTip("Show tactile pressure visualization");
    actDiagram_->setToolTip("Open a new live diagram panel");
    actLog_->setToolTip("Show or hide the log console");

    connect(actConnector_,  &QAction::triggered, this, &MainWindow::openConnectorTab);
    connect(actModel3D_,    &QAction::triggered, this, &MainWindow::openUrdfLoaderTab);
    connect(actActionSets_, &QAction::triggered, this, &MainWindow::openActionPanelTab);
    connect(actJointModes_, &QAction::triggered, this, &MainWindow::openMotorControlTab);
    connect(actHandDetect_, &QAction::triggered, this, &MainWindow::openCameraTab);
    connect(actTactile_,    &QAction::triggered, this, &MainWindow::openTactileImage);
    connect(actDiagram_,    &QAction::triggered, this, &MainWindow::openDiagram);
    connect(actLog_,        &QAction::triggered, this, &MainWindow::toggleLogPanel);

    toolBar_->addAction(actConnector_);
    toolBar_->addSeparator();
    toolBar_->addAction(actModel3D_);
    toolBar_->addAction(actActionSets_);
    toolBar_->addAction(actJointModes_);
    toolBar_->addSeparator();
    toolBar_->addAction(actHandDetect_);
    toolBar_->addAction(actTactile_);
    toolBar_->addAction(actDiagram_);
    toolBar_->addSeparator();
    toolBar_->addAction(actLog_);

    // The same shortcuts stay reachable from the menu bar.
    auto *viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction(actConnector_);
    viewMenu->addAction(actModel3D_);
    viewMenu->addAction(actActionSets_);
    viewMenu->addAction(actJointModes_);
    viewMenu->addSeparator();
    viewMenu->addAction(actHandDetect_);
    viewMenu->addAction(actTactile_);
    viewMenu->addAction(actDiagram_);
    viewMenu->addSeparator();
    viewMenu->addAction(actLog_);
}

void MainWindow::buildLogPanel()
{
    logPanel_ = new QWidget(this);
    auto *logLyt = new QVBoxLayout(logPanel_);
    logLyt->setContentsMargins(0, 0, 0, 0);
    logLyt->setSpacing(0);

    auto *header = new QWidget(logPanel_);
    auto *headerLyt = new QHBoxLayout(header);
    headerLyt->setContentsMargins(6, 2, 6, 2);
    headerLyt->setSpacing(6);
    auto *title = new QLabel("Log");
    auto *clearBtn = new QPushButton("Clear");
    clearBtn->setFixedHeight(22);
    headerLyt->addWidget(title);
    headerLyt->addStretch(1);
    headerLyt->addWidget(clearBtn);

    log_ = new QTextEdit;
    log_->setReadOnly(true);
    log_->setLineWrapMode(QTextEdit::NoWrap);

    logLyt->addWidget(header);
    logLyt->addWidget(log_, 1);

    connect(clearBtn, &QPushButton::clicked, log_, &QTextEdit::clear);
}

void MainWindow::appendLog(const QString &msg)
{
    if (log_)
        log_->append(msg);
}

void MainWindow::setConnectedStatus(bool connected)
{
    statusBar()->showMessage(connected
        ? QString("Connected: %1").arg(selectedModel_)
        : QString("Not connected"));
}

void MainWindow::addOrShowTab(QWidget *widget, const QString &title)
{
    if (!widget)
        return;

    // Reuse the existing tab instead of adding a duplicate one.
    const int existing = upperTabs->indexOf(widget);
    if (existing >= 0)
    {
        upperTabs->setCurrentIndex(existing);
        return;
    }

    upperTabs->addTab(widget, title);
    upperTabs->setCurrentIndex(upperTabs->count() - 1);
    widget->show();
}

void MainWindow::onSidePanelValuesChanged()
{
    // Only act in Follow mode
    if (side_->modeText() != "Mode: Follow")
        return;
    if (!ctrl_)
        return;

    const QList<int> deg = side_->jointDegrees();
    const QStringList control_mode = side_->getControlModes();

    std::unordered_map<std::string, float> joint_target;
    const int count = std::min({deg.size(), dof_names.size(), control_mode.size()});
    for (int i = 0; i < count; ++i)
    {
        const float rad = float(deg[i]) * float(M_PI) / 180.0f / 100.0;
        ctrl_->submit_target(dof_names[i].toUtf8().constData(), float(deg[i]));

        std::unordered_map<std::string, float> joint_pos = {};
        if (control_mode[i] == "hall_position")
            joint_pos = dofsToJoints(dof_names[i].toUtf8().constData(), float(deg[i]), true);
        else
            joint_pos = dofsToJoints(dof_names[i].toUtf8().constData(), rad, false);
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
    for (auto &[key, value] : joint_target)
        ogre_->setJointPosition("fb", key, value);
}

void MainWindow::onSidePanelCommitted()
{
    if (!ctrl_)
        return;

    const QList<int> deg = side_->jointDegrees();
    const QStringList control_mode = side_->getControlModes();

    std::unordered_map<std::string, float> joint_target;
    const int count = std::min({deg.size(), dof_names.size(), control_mode.size()});
    for (int i = 0; i < count; ++i)
    {
        const float rad = float(deg[i]) * float(M_PI) / 180.0f / 100.0;
        ctrl_->submit_target(dof_names[i].toUtf8().constData(), float(deg[i]));
        appendLog(QString("Execute: %1, %2").arg(dof_names[i]).arg(rad));

        std::unordered_map<std::string, float> joint_pos = {};
        if (control_mode[i] == "hall_position")
            joint_pos = dofsToJoints(dof_names[i].toUtf8().constData(), float(deg[i]), true);
        else
            joint_pos = dofsToJoints(dof_names[i].toUtf8().constData(), rad, false);
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
    for (auto &[key, value] : joint_target)
        ogre_->setJointPosition("fb", key, value);

    appendLog(QString("Execute: %1 targets sent.").arg(count));
}

void MainWindow::onSidePanelNotify(const QString &msg)
{
    appendLog(msg);
}

void MainWindow::onSidePanelControlChanged(const QString &name, const QString &mode)
{
    if (!ctrl_)
        return;
    for (auto mod : hand_cfg_.structs)
    {
        if (mod.first == "WRIST" && name == "wrist")
        {
            for (auto j : mod.second)
            {
                if (!motor_->checkJointStandalone(QString(j.name.c_str())))
                {
                    side_->applyModeChange(&hand_cfg_, QString(j.name.c_str()), mode);
                    motor_->applyModeChange(QString(j.name.c_str()), mode);
                    ctrl_->submit_control_mode(j.name, mode.toUtf8().constData());
                }
            }
        }
        else if (mod.first != "WRIST" && name == "finger")
        {
            for (auto j : mod.second)
            {
                if (!motor_->checkJointStandalone(QString(j.name.c_str())))
                {
                    side_->applyModeChange(&hand_cfg_, QString(j.name.c_str()), mode);
                    motor_->applyModeChange(QString(j.name.c_str()), mode);
                    ctrl_->submit_control_mode(j.name, mode.toUtf8().constData());
                }
            }
        }
    }
}

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
        appendLog("Please select a hand model before connecting.");
        connect_->onChange(false);
        return;
    }

    try
    {
        dev_cfg_ = grip::load_device_settings(config_path_);
        hand_cfg_ = grip::load_hand_settings(config_path_);

        ctrl_ = new grip::ControlInterface(hand_cfg_, std::chrono::milliseconds(10));
        if (withCan)
        {
            ctrl_->start_bus();
            appendLog("Device Connected. ");
            ctrl_->start_control_loop();
            // appendLog("Control Policy Applied. \nCurrent Policy: Protected Hall Position Control");
            appendLog("Current control mode: " + side_->modeText());

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
        motor_->load(hand_cfg_);
        ogre_->loadURDFDual(hand_cfg_.urdf_file);

        // The action panel owns the store, so it is released to it only after
        // everything else succeeded; a throw in between still frees it.
        std::unique_ptr<ActionStore> store(new ActionStore(action_path_.c_str()));
        store->load();
        actionFlow_->setStore(store.get());
        store.release();

        connect_->onChange(true);
        setConnectedStatus(true);
    }
    catch (const std::exception &e)
    {
        const QString qerr = QString(e.what());
        appendLog(qerr);
        connect_->onChange(false);
        setConnectedStatus(false);
    }
}

void MainWindow::onDeviceDisconnect()
{
    // ActionPanel owns (and deletes) the ActionStore, so hand it a null store
    // before the joint widgets go away.
    actionFlow_->setStore(nullptr);
    side_->removeAll();
    motor_->removeAll();

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
    connect_->onChange(false);
    setConnectedStatus(false);
    ogre_->clearScene();
    dof_names.clear();
    joints_inverted_index.clear();
    dof_map.clear();
    dev_cfg_ = grip::DeviceSettings();
    hand_cfg_ = grip::HandSettings();
    act_cfg_ = grip::ActuatorSettings();
}

void MainWindow::onConnectorStateCheck(Qt::CheckState state)
{
    if (state == Qt::Unchecked)
        withCan = true;
    else if (state == Qt::Checked)
        withCan = false;
}

void MainWindow::onActionPanelActionUpdate(const QString &msg)
{
    const QVector<double> values = actionFlow_->actionValues(msg);
    if (values.isEmpty())
        return;
    side_->applyActionValues(values);
}

void MainWindow::onJointPanelControlModeChange(const QString &module_name, const QString &control_mode)
{
    if (!ctrl_)
        return;
    if (motor_->checkJointStandalone(module_name))
    {
        side_->applyModeChange(&hand_cfg_, module_name, control_mode);
        ctrl_->submit_control_mode(module_name.toUtf8().constData(), control_mode.toUtf8().constData());
    }
}

void MainWindow::onJointPanelVelRefModeChange(const QString &module_name, const int vel_ref)
{
    if (!ctrl_)
        return;
    ctrl_->submit_vel_ref(module_name.toUtf8().constData(), vel_ref);
}

void MainWindow::onJointPanelTorqueRefModeChange(const QString &module_name, const int torque_ref)
{
    if (!ctrl_)
        return;
    ctrl_->submit_torque_ref(module_name.toUtf8().constData(), torque_ref);
}

void MainWindow::readJointAngles()
{
    HandStateMap state = ctrl_->snapshot();
    std::unordered_map<std::string, float> joints;
    std::unordered_map<std::string, float> joint_target;
    std::unordered_map<std::string, int> controlStates;
    TactileVisual::HandPressureData tips;
    for (auto &[name, joint_group] : state)
    {
        for (auto &[joint_name, data] : joint_group.joints)
        {
            // auto j = dof_map.find(i);
            // if (j == dof_map.end())
            //     continue;
            joints.emplace(joint_name, data.position);
            float rad = float(data.position) * float(M_PI) / 180.0f;
            std::unordered_map<std::string, float> joint_pos = dofsToJoints(joint_name, rad, false);
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
        QVector<float> pressure;
        pressure.append((float)joint_group.tip_sensor.tip_tactile / 100.0f);
        if (name == "THUMB")
            tips.thumb.values = pressure;
        else if (name == "INDEX_FINGER")
            tips.index.values = pressure;
        else if (name == "MIDDLE_FINGER")
            tips.middle.values = pressure;
        else if (name == "RING_FINGER")
            tips.ring.values = pressure;
        else if (name == "PINKY")
            tips.pinky.values = pressure;
        for (auto &[joint_name, data] : joint_group.control_mode)
        {
            controlStates.emplace(joint_name, data);
        }
    }
    for (auto &[key, value] : joint_target)
        ogre_->setJointPosition("cmd", key, value);
    side_->updateFeedback(state);
    tactile_->setHandPressureData(tips);
}

void MainWindow::onConnectorModelChange(const QString &model_name)
{
    // The first combo entry is the "- Select a model -" placeholder.
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

void MainWindow::onUpperTabCloseRequested(int idx)
{
    // Always keep at least one tab so the work area never collapses.
    if (upperTabs->count() <= 1)
        return;
    upperTabs->removeTab(idx);
}

void MainWindow::toggleLogPanel()
{
    if (!logPanel_)
        return;
    const bool visible = actLog_ && actLog_->isChecked();
    logPanel_->setVisible(visible);
}

void MainWindow::openCameraTab()
{
    addOrShowTab(camera_, "Hand Detect");
}

void MainWindow::openConnectorTab()
{
    addOrShowTab(connect_, "Connector");
}

void MainWindow::openUrdfLoaderTab()
{
    addOrShowTab(ogre_, "3D Model");
}

void MainWindow::openMotorControlTab()
{
    addOrShowTab(motor_, "Joint Modes");
}

void MainWindow::openActionPanelTab()
{
    addOrShowTab(actionFlow_, "Action Sets");
}

void MainWindow::openDiagram()
{
    auto *plot = new PlotPanel(this);
    upperTabs->addTab(plot, "Diagram");
    upperTabs->setCurrentIndex(upperTabs->count() - 1);

    auto *timer = new QTimer(plot); // lifetime tied to this panel

    connect(timer, &QTimer::timeout, plot, [this, plot]()
            {
        HandStateMap snapshot;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (ctrl_)
                snapshot = ctrl_->snapshot();
        }
        plot->feedData(snapshot); });

    plot->setUpdateTimer(timer);
    timer->start(20);
}

void MainWindow::openTactileImage()
{
    addOrShowTab(tactile_, "Tactile");
}

std::unordered_map<std::string, float> MainWindow::dofsToJoints(const std::string dof_name, float value, bool is_hall)
{
    // auto module_name = dofs_inverted_index.find(dof_name);
    // if (module_name == dofs_inverted_index.end())
    //     return {};
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
    // float sum = 0;
    // for (float v : map)
    //     sum += v;

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
