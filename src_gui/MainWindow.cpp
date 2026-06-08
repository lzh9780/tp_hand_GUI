#include "MainWindow.h"
#include "SidePanel.h"
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
    resize(800, 600);
}

MainWindow::~MainWindow()
{
    if (camera_)
        camera_->stopAll();
    if (ctrl_)
        ctrl_->stop_all();
}

void MainWindow::buildUI()
{
    central = new QWidget(this);
    auto *rootLayout = new QHBoxLayout(central);
    rootLayout->setSpacing(5);

    connect_ = new Connector(this, cfg_.models);

    auto *splitter = new QSplitter(Qt::Horizontal, this);

    side_ = new SidePanel(this);

    log_ = new QTextEdit;
    log_->setReadOnly(true);
    // log_->setMaximumHeight(50);

    ogre_ = new OgreWidget();
    motor_ = new MotorPanel();
    camera_ = new CameraWidget();
    actionFlow_ = new ActionPanel();

    upperTabs = new DraggableTabWidget();
    lowerTabs = new DraggableTabWidget();

    upperTabs->setTabsClosable(true);
    lowerTabs->setTabsClosable(true);
    upperTabs->addTab(connect_, "Connector");
    lowerTabs->addTab(log_, "Log");

    auto *mainLyt = new QSplitter(Qt::Vertical, this);
    // mainLyt->addWidget(ogre_);
    mainLyt->addWidget(upperTabs);
    mainLyt->addWidget(lowerTabs);
    mainLyt->setStretchFactor(0, 1);
    mainLyt->setStretchFactor(1, 0);

    splitter->addWidget(side_);
    splitter->addWidget(mainLyt);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);

    rootLayout->addWidget(splitter);

    rootLayout->setContentsMargins(0, 0, 0, 0);
    central->setLayout(rootLayout);

    setCentralWidget(central);

    connect(side_, &SidePanel::valuesChanged, this, &MainWindow::onSidePanelValuesChanged);
    connect(side_, &SidePanel::committed, this, &MainWindow::onSidePanelCommitted);
    connect(side_, &SidePanel::notify, this, &MainWindow::onSidePanelNotify);
    connect(side_, &SidePanel::controlChanged, this, &MainWindow::onSidePanelControlChanged);
    connect(side_, &SidePanel::openAdvancePanel, this, &MainWindow::openMotorControlTab);
    connect(motor_, &MotorPanel::mode_change, this, &MainWindow::onJointPanelControlModeChange);
    connect(connect_, &Connector::deviceConnect, this, &MainWindow::onConnectorDeviceConnect);
    // connect(camera_, &CameraWidget::controlSwitch, this, &MainWindow::onCameraWidgetControlSwitch);
    connect(connect_, &Connector::modelChange, this, &MainWindow::onConnectorModelChange);
    connect(connect_, &Connector::stateCheck, this, &MainWindow::onConnectorStateCheck);
    connect(upperTabs, &QTabWidget::tabCloseRequested, this, &MainWindow::onUpperTabCloseRequested);
    connect(lowerTabs, &QTabWidget::tabCloseRequested, this, &MainWindow::onLowerTabCloseRequested);
    connect(actionFlow_, &ActionPanel::actionUpdate, this, &MainWindow::onActionPanelActionUpdate);
    connect(side_, &SidePanel::addAction, this, &MainWindow::onSidePanelAddAction);
    connect(side_, &SidePanel::actionStoreUpdate, this, [=]()
            { actionFlow_->reload(); });
    connect(camera_, &CameraWidget::dataUpdate, this, [this](const QVector<double> &joints)
            {
                auto full_joints_list = camera_->getFullList();
                std::unordered_map<std::string, float> dof_pos;
                for (int i = 0; i < full_joints_list.size(); ++i)
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
            { side_->modeChange(msg); });
            connect(camera_, &CameraWidget::status, this, [this](const QString &s)
            { statusBar()->showMessage(s, 2000); });
    connect(camera_, &CameraWidget::error, this, [this](const QString &e)
            {
                statusBar()->showMessage(QString("Error: %1").arg(e), 5000);
                log_->append(QString("Error: %1").arg(e)); });

    // auto* mainMenu = menuBar()->addMenu("&Menu");
    auto *tabsMenu = menuBar()->addMenu("Tabs");
    // auto* settingMenu = menuBar()->addMenu("&Settings");
    auto *actUrdfLoader = tabsMenu->addAction("3D Model");
    auto *actActionFlow = tabsMenu->addAction("Action Sets");
    auto *actConnect = tabsMenu->addAction("Connector");
    // auto* actAlgoPanel = tabsMenu->addAction("Control Algorithm");
    auto *actDiagram = tabsMenu->addAction("Diagrams");
    auto *actHandDetect = tabsMenu->addAction("Hand Detect");
    auto *actJoint = tabsMenu->addAction("Joint Modes");
    auto *actLog = tabsMenu->addAction("Log");
    // auto* actInfo = tabsMenu->addAction("Robot Info");
    auto *settingsMenu = menuBar()->addMenu("&Settings");
    auto *TextMenu = settingsMenu->addMenu("Window Zoom");
    auto *actShrink = TextMenu->addAction("Zoom Out -");
    auto *actEnlarge = TextMenu->addAction("Zoom In +");

    connect(actHandDetect, &QAction::triggered, this, &MainWindow::openCameraTab);
    connect(actConnect, &QAction::triggered, this, &MainWindow::openConnectorTab);
    connect(actLog, &QAction::triggered, this, &MainWindow::openLogTab);
    connect(actUrdfLoader, &QAction::triggered, this, &MainWindow::openUrdfLoaderTab);
    connect(actJoint, &QAction::triggered, this, &MainWindow::openMotorControlTab);
    connect(actActionFlow, &QAction::triggered, this, &MainWindow::openActionPanelTab);
    connect(actDiagram, &QAction::triggered, this, &MainWindow::openDiagram);

    // log_->append("Current control mode: " + side_->modeText());
}

void MainWindow::resizeEvent(QResizeEvent *e)
{
    QMainWindow::resizeEvent(e);
}

void MainWindow::onSidePanelValuesChanged()
{
    // Only act in Follow mode
    if (side_->modeText() != "Mode: Follow")
        return;
    auto deg = side_->jointDegrees();
    auto control_mode = side_->getControlModes();
    // log_->append("Follow: " + QString::number(deg.size()) + " targets sent");
    if (ctrl_)
    {
        // ctrl_->set_control_mode("Position");
        std::unordered_map<std::string, float> joint_target;
        for (int i = 0; i < deg.size() && i < (int)dof_names.size(); ++i)
        {
            float rad = float(deg[i]) * float(M_PI) / 180.0f / 100.0;
            ctrl_->submit_target(dof_names[i].toUtf8().constData(), float(deg[i]));
            // std::cout << "cmd_type: " << dof_names[i].toUtf8().constData() << ", val: " << rad << "\n";
            log_->append("Follow: " + QString(dof_names[i].toUtf8().constData()) + ", " + QString::number(rad));
            // ogre_->setJointPosition(dof_names[i].toUtf8().constData(), rad);
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
        log_->append("");
    }
}

void MainWindow::onSidePanelCommitted()
{
    auto deg = side_->jointDegrees();
    auto control_mode = side_->getControlModes();

    if (ctrl_)
    {
        // ctrl_->set_control_mode("Position");
        std::unordered_map<std::string, float> joint_target;
        for (int i = 0; i < deg.size() && i < (int)dof_names.size(); ++i)
        {
            float rad = float(deg[i]) * float(M_PI) / 180.0f / 100.0;
            ctrl_->submit_target(dof_names[i].toUtf8().constData(), float(deg[i]));
            // std::cout << "cmd_type: " << dof_names[i].toUtf8().constData() << ", val: " << rad << "\n";
            log_->append("Execute: " + QString(dof_names[i].toUtf8().constData()) + ", " + QString::number(rad));
            // ogre_->setJointPosition(dof_names[i].toUtf8().constData(), rad);
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
    log_->append("Execute: batch targets sent.\n");
}

void MainWindow::onSidePanelNotify(const QString &msg)
{
    log_->append(msg);
}

void MainWindow::onSidePanelControlChanged(const QString &name, const QString &mode)
{
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
    try
    {
        dev_cfg_ = grip::load_device_settings(config_path_);
        hand_cfg_ = grip::load_hand_settings(config_path_);

        ctrl_ = new grip::ControlInterface(hand_cfg_, std::chrono::milliseconds(10));
        if (withCan)
        {
            ctrl_->start_bus();
            log_->append("Device Connected. ");
            ctrl_->start_control_loop();
            // log_->append("Control Policy Applied. \nCurrent Policy: Protected Hall Position Control");
            log_->append("Current control mode: " + side_->modeText());

            read_timer_ = new QTimer(this);
            connect(read_timer_, &QTimer::timeout, this, &MainWindow::readJointAngles);
            read_timer_->start(10);
        }

        // motor_->load(hand_cfg_);
        for (const auto &[key, value] : hand_cfg_.structs)
        {
            for (auto &j : value)
            {
                if (j.id >= 0)
                    dof_names << QString::fromStdString(j.name);
                joints_inverted_index.emplace(j.name, key);
                dof_map.emplace(j.id, j.name);
            }
            // for (auto &j : value.joints)
            //     joints_inverted_index.emplace(j, key);
        }
        // side_->loadSliders(dof_names);
        // qDebug() << "Hand Controllor loaded";
        side_->loadHandControllor(&hand_cfg_);
        motor_->load(hand_cfg_);
        ogre_->loadURDFDual(hand_cfg_.urdf_file);
        auto *store = new ActionStore(action_path_.c_str());
        store->load();
        side_->updateStore(store);
        side_->loadActions();

        actionFlow_->updateStore(store);
        actionFlow_->loadFlowList();

        connect_->onChange(true);
    }
    catch (const std::exception &e)
    {
        const QString qerr = QString(e.what());
        log_->append(qerr);
        connect_->onChange(false);
    }
}

void MainWindow::onDeviceDisconnect()
{
    side_->removeAll();
    motor_->removeAll();
    actionFlow_->clearAll();
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
    ogre_->clearScene();
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
    side_->applyActionNameToSliders(msg);
}

void MainWindow::onSidePanelAddAction(const QString &msg)
{
    actionFlow_->addAction(msg);
}

void MainWindow::onJointPanelControlModeChange(const QString &module_name, const QString &control_mode)
{
    if (motor_->checkJointStandalone(module_name))
    {
        qDebug() << "standalone change";
        side_->applyModeChange(&hand_cfg_, module_name, control_mode);
        ctrl_->submit_control_mode(module_name.toUtf8().constData(), control_mode.toUtf8().constData());
    }
}

void MainWindow::readJointAngles()
{
    HandStateMap state = ctrl_->snapshot();
    std::unordered_map<std::string, float> joints;
    std::unordered_map<std::string, float> joint_target;
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
    }
    for (auto &[key, value] : joint_target)
        ogre_->setJointPosition("cmd", key, value);
    side_->updateJoints(joints);
}

void MainWindow::onConnectorModelChange(const QString &model_name)
{
    config_path_ = std::string("configs/config_") + model_name.toUtf8().constData() + std::string(".yaml");
    action_path_ = std::string("data/action_") + model_name.toUtf8().constData() + std::string(".json");
}

void MainWindow::onUpperTabCloseRequested(int idx)
{
    upperTabs->removeTab(idx);
}

void MainWindow::onLowerTabCloseRequested(int idx)
{
    lowerTabs->removeTab(idx);
}

void MainWindow::openCameraTab()
{
    upperTabs->addTab(camera_, "Hand Detect");
}

void MainWindow::openConnectorTab()
{
    upperTabs->addTab(connect_, "Connector");
}

void MainWindow::openLogTab()
{
    upperTabs->addTab(log_, "Log");
}

void MainWindow::openUrdfLoaderTab()
{
    upperTabs->addTab(ogre_, "3D Model");
}

void MainWindow::openMotorControlTab()
{
    upperTabs->addTab(motor_, "Motor Control");
}

void MainWindow::openActionPanelTab()
{
    upperTabs->addTab(actionFlow_, "Action Sets");
}

void MainWindow::openDiagram()
{
    auto *plot = new PlotPanel(this);
    upperTabs->addTab(plot, "Diagram");

    auto timer_ = new QTimer(plot); // lifetime tied to this panel

    connect(timer_, &QTimer::timeout, plot, [this, plot]()
            {
        HandStateMap snapshot;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (ctrl_)
                snapshot = ctrl_->snapshot();
        }
        plot->feedData(snapshot); });

    plot->setUpdateTimer(timer_);
    timer_->start(20);
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
    int counter = 0;
    while (counter < map.size())
    {
        result.emplace(ms[counter].name, joint_rad * map[counter]);
        counter += 1;
    }
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
    int idx = 0;
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
        result.emplace(j.name, (j.joint_feedback_map[idx] == 0 ? 0 : j.joint_feedback_map[idx]) * value);
    }
    return result;
}
