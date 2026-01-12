#include "MainWindow.h"
#include "SidePanel.h"
#include "grip/control_interface.hpp"
#include <QTextEdit>
#include <QSplitter>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStatusBar>
#include <QTimer>
#include <QStringList>
#include <QMenuBar>
#include <QTabWidget>
#include <iostream>
#include "DraggableTabWidget.hpp"
#include "Connector.hpp"
#include "CameraWidget.hpp"
#include "ActionPanel.hpp"

static const std::vector<std::string> full_joints_list
{
    "thumb_cmc_pull",
    "thumb_cmc_rot",
    "thumb_mcp",
    "thumb_ip",
    "index_mcp",
    "index_pip",
    "index_dip",
    "index_sway",
    "middle_mcp",
    "middle_pip",
    "middle_dip",
    "middle_sway",
    "ring_mcp",
    "ring_pip",
    "ring_dip",
    "ring_sway",
    "pinky_mcp",
    "pinky_pip",
    "pinky_dip",
    "pinky_sway",
    "pinky_roll",
    "roll",
    "pitch",
    "yaw"
};

MainWindow::MainWindow(const grip::AppSettings &cfg,
                       QWidget *parent)
    : QMainWindow(parent), cfg_(cfg)
{
    buildUI();
    resize(800, 600);

    // Worker thread
    workerThread_ = new QThread(this);
    worker_ = new PyCamWorker();
    worker_->moveToThread(workerThread_);

    // Start worker when thread starts
    connect(this, &QObject::destroyed, workerThread_, &QThread::quit);

    // Display frames
    connect(worker_, &PyCamWorker::newFrame, this, [this](const QImage &img)
            { camera_->setFrame(img); });

    connect(worker_, &PyCamWorker::newJoint, this, [this](const QVector<double> &joints)
            { 
                if (joints.size() != full_joints_list.size()) 
                    return;
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
                side_->applyCameraData(dof_pos);
            });
    connect(worker_, &PyCamWorker::status, this, [this](const QString &s)
            { statusBar()->showMessage(s, 2000); });
    connect(worker_, &PyCamWorker::error, this, [this](const QString &e)
            {
        statusBar()->showMessage(QString("Error: %1").arg(e), 5000);
        log_->append(QString("Error: %1").arg(e)); });

    connect(worker_, &PyCamWorker::runningChanged, this, [this](bool r)
            { camera_->onChange(r); });

    workerThread_->start();
}

MainWindow::~MainWindow()
{
    if (worker_)
    {
        QMetaObject::invokeMethod(worker_, "stop", Qt::BlockingQueuedConnection);
        worker_->deleteLater();
    }
    if (workerThread_)
    {
        workerThread_->quit();
        workerThread_->wait();
    }
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
    // connect(side_, &SidePanel::mode_change, this, &MainWindow::onSidePanelControlModeChaneg);
    connect(connect_, &Connector::deviceConnect, this, &MainWindow::onConnectorDeviceConnect);
    connect(camera_, &CameraWidget::controlSwitch, this, &MainWindow::onCameraWidgetControlSwitch);
    connect(connect_, &Connector::modelChange, this, &MainWindow::onConnectorModelChange);
    connect(connect_, &Connector::stateCheck, this, &MainWindow::onConnectorStateCheck);
    connect(upperTabs, &QTabWidget::tabCloseRequested, this, &MainWindow::onUpperTabCloseRequested);
    connect(lowerTabs, &QTabWidget::tabCloseRequested, this, &MainWindow::onLowerTabCloseRequested);
    connect(actionFlow_, &ActionPanel::actionUpdate, this, &MainWindow::onActionPanelActionUpdate);
    connect(side_, &SidePanel::addAction, this, &MainWindow::onSidePanelAddAction);

    // auto* mainMenu = menuBar()->addMenu("&Menu");
    auto *tabsMenu = menuBar()->addMenu("Tabs");
    // auto* settingMenu = menuBar()->addMenu("&Settings");
    auto *actUrdfLoader = tabsMenu->addAction("3D Model");
    auto *actActionFlow = tabsMenu->addAction("Action Sets");
    auto *actConnect = tabsMenu->addAction("Connector");
    // auto* actAlgoPanel = tabsMenu->addAction("Control Algorithm");
    auto *actDiagram = tabsMenu->addAction("Diagrams");
    auto *actHandDetect = tabsMenu->addAction("Hand Detect");
    auto *actLog = tabsMenu->addAction("Log");
    auto *actMotor = tabsMenu->addAction("Motor Control");
    // auto* actInfo = tabsMenu->addAction("Robot Info");

    connect(actHandDetect, &QAction::triggered, this, &MainWindow::openCameraTab);
    connect(actConnect, &QAction::triggered, this, &MainWindow::openConnectorTab);
    connect(actLog, &QAction::triggered, this, &MainWindow::openLogTab);
    connect(actUrdfLoader, &QAction::triggered, this, &MainWindow::openUrdfLoaderTab);
    connect(actMotor, &QAction::triggered, this, &MainWindow::openMotorControlTab);
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
    // log_->append("Follow: " + QString::number(deg.size()) + " targets sent");
    if (dev_cfg_.device_type == 0 && ctrl_)
    {
        // ctrl_->set_control_mode("Position");
        std::unordered_map<std::string, float> joint_target;
        for (int i = 0; i < deg.size() && i < (int)dof_names.size(); ++i)
        {
            float rad = float(deg[i]) * float(M_PI) / 180.0f / 100.0;
            ctrl_->submit_target(dof_names[i].toUtf8().constData(), rad);
            // std::cout << "cmd_type: " << dof_names[i].toUtf8().constData() << ", val: " << rad << "\n";
            log_->append("Follow: " + QString(dof_names[i].toUtf8().constData()) + ", " + QString::number(rad));
            // ogre_->setJointPosition(dof_names[i].toUtf8().constData(), rad);
            std::unordered_map<std::string, float> joint_pos = dofsToJoints(dof_names[i].toUtf8().constData(), rad);
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
            ogre_->setJointPosition(key, value);
    }
    else if (dev_cfg_.device_type != 0 && ctrl_)
    {
        float rad = float(deg[2]);
        ctrl_->submit_target(side_->get_control_mode(), rad);
        // ctrl_->set_control_mode(side_->get_control_mode());
    }
}

void MainWindow::onSidePanelCommitted()
{
    auto deg = side_->jointDegrees();
    for (int i = 0; i < deg.size() && i < (int)dof_names.size(); ++i)
    {
        float rad = float(deg[i]) * float(M_PI) / 180.0f / 100.0;
        ctrl_->submit_target(dof_names[i].toUtf8().constData(), rad);
    }
    log_->append("Execute: batch targets sent.");
}

void MainWindow::onSidePanelNotify(const QString &msg)
{
    log_->append(msg);
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
        if (dev_cfg_.device_type == 0)
        {
            hand_cfg_ = grip::load_hand_settings(config_path_);

            ctrl_ = new grip::ControlInterface(hand_cfg_, std::chrono::milliseconds(10));
            if (withCan)
            {
                ctrl_->start_bus(cfg_.can_device);
                log_->append("Device Connected. ");
                log_->append("Start Calibration... ");
                // ctrl_->run_calibration();
                // while (ctrl_->get_board_state() != 1)
                //     log_->append(".");
                log_->append("Calibration Complete. ");
                ctrl_->start_control_loop();
                // log_->append("Control Policy Applied. \nCurrent Policy: Protected Hall Position Control");
                log_->append("Current control mode: " + side_->modeText());

                auto timer_ = new QTimer(this);
                connect(timer_, &QTimer::timeout, this, &MainWindow::readJointAngles);
                timer_->start(10);
            }

            motor_->load(hand_cfg_);
            for (const auto &[key, value] : hand_cfg_.structs)
            {
                for (auto &d : value.dofs)
                {
                    dof_names << QString::fromStdString(d.name);
                    dofs_inverted_index.emplace(d.name, key);
                }
                for (auto &j : value.joints)
                    joints_inverted_index.emplace(j, key);
            }
            // side_->loadSliders(dof_names);
            // qDebug() << "Hand Controllor loaded";
            side_->loadHandControllor(hand_cfg_);
            ogre_->loadURDF(hand_cfg_.urdf_file);
        }
        else
        {
            act_cfg_ = grip::load_actuator_settings(config_path_);

            ctrl_ = new grip::ControlInterface(act_cfg_, std::chrono::milliseconds(10));
            if (withCan)
            {
                ctrl_->start_bus(cfg_.can_device);
                log_->append("Device Connected. ");
                ctrl_->start_control_loop();
            }
            side_->loadActuatorControllor(act_cfg_);
        }
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
        ctrl_->stop_all();
        free(ctrl_);
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

void MainWindow::onCameraWidgetControlSwitch(const QString &msg)
{
    side_->modeChange(msg);
    if (msg == "Start Camera")
        QMetaObject::invokeMethod(worker_, "start", Qt::QueuedConnection);
    else
        QMetaObject::invokeMethod(worker_, "stop", Qt::BlockingQueuedConnection);
}

void MainWindow::onActionPanelActionUpdate(const QString &msg)
{
    side_->applyActionNameToSliders(msg);
}

void MainWindow::onSidePanelAddAction(const QString &msg)
{
    actionFlow_->addAction(msg);
}

// void MainWindow::onSidePanelControlModeChaneg(const QString &module_name, const QString &control_mode)
// {
//     ctrl_->set_module_control_mode(module_name.toUtf8().constData(), control_mode.toUtf8().constData());
// }

void MainWindow::readJointAngles()
{
    grip::JointAngles j = ctrl_->get_joint_angles();
    float thumb_angle = j.thumb_ip + j.thumb_mcp;
    float index_angle = j.index_mcp + j.index_pip + j.index_dip;
    float middle_angle = j.middle_mcp + j.middle_pip + j.middle_dip;
    float ring_angle = j.ring_mcp + j.ring_pip + j.ring_dip;
    float pinky_angle = j.pinky_mcp + j.pinky_pip + j.pinky_dip;

    side_->updateJoints(thumb_angle, index_angle, middle_angle, ring_angle, pinky_angle);
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
    auto *plot = new PlotPanel();
    upperTabs->addTab(plot, "Diagram");

    auto timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, [=]()
            { if (ctrl_) plot->updateFromHand(ctrl_->get_hand_info()); });
    timer_->start(10);
}

std::unordered_map<std::string, float> MainWindow::dofsToJoints(const std::string dof_name, float value)
{
    auto module_name = dofs_inverted_index.find(dof_name);
    if (module_name == dofs_inverted_index.end())
        return {};
    auto module_struct = hand_cfg_.structs.find(module_name->second);
    if (module_struct == hand_cfg_.structs.end())
        return {};
    auto ms = module_struct->second;
    std::vector<float> map;
    for (auto &d : ms.dofs)
    {
        if (d.name == dof_name)
        {
            map = d.dof_joint_map;
            break;
        }
    }
    if (map.size() == 0)
        return {};
    float sum = 0;
    for (float v : map)
        sum += v;

    std::unordered_map<std::string, float> result;
    int counter = 0;
    while (counter < map.size())
    {
        result.emplace(ms.joints[counter], value * map[counter] / sum);
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
    for (auto j : ms.joints)
    {
        if (j == joint_name)
            break;
        idx += 1;
    }
    if (idx >= ms.joints.size())
        return {};
    std::unordered_map<std::string, float> result;
    for (auto dof : ms.dofs)
    {
        if (idx >= dof.dof_joint_map.size())
            continue;
        result.emplace(dof.name, dof.dof_joint_map[idx] * value);
    }
    return result;
}
