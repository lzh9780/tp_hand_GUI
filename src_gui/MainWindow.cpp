#include "MainWindow.h"
#include "SidePanel.h"
#include "TopPanel.hpp"
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

#include "Connector.hpp"
#include "CameraWidget.hpp"

MainWindow::MainWindow(const grip::AppSettings &cfg,
                       QWidget *parent)
    : QMainWindow(parent), cfg_(cfg)
{
    // QStringList names;
    // for (auto &s : cfg.joints)
    //     names << QString::fromStdString(s);
    buildUI();
    resize(800, 600);

    // Worker thread
    workerThread_ = new QThread(this);
    worker_ = new PyCamWorker();
    worker_->moveToThread(workerThread_);

    // Start worker when thread starts
    // connect(workerThread_, &QThread::started, worker_, &PyCamWorker::start);
    connect(this, &QObject::destroyed, workerThread_, &QThread::quit);
    // connect(workerThread_, &QThread::finished, worker_, &QObject::deleteLater);

    // Display frames
    connect(worker_, &PyCamWorker::newFrame, this, [this](const QImage &img)
            { camera_->setFrame(img); });

    connect(worker_, &PyCamWorker::newJoint, this, [this](const QVector<double> &joints)
            { side_->applyCameraData(joints); });
    connect(worker_, &PyCamWorker::status, this, [this](const QString &s)
            { statusBar()->showMessage(s, 2000); });
    connect(worker_, &PyCamWorker::error, this, [this](const QString &e)
            {
        statusBar()->showMessage(QString("Error: %1").arg(e), 5000);
        log_->append(QString("Error: %1").arg(e)); });

    connect(worker_, &PyCamWorker::runningChanged, this, [this](bool r)
            { camera_->onChange(r); });

    // connect(this, &MainWindow::requestStop, worker_, &PyCamWorker::stop, Qt::QueuedConnection);

    // QTimer* timer_ = new QTimer(this);
    // connect(timer_, &QTimer::timeout, worker_, &PyCamWorker::onTick);
    // timer_->start(1000/60);
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
    // top_ = new TopPanel(this, cfg_.models);
    // rootLayout->addWidget(top_);

    mainContent = new QWidget;
    // auto *lay = new QHBoxLayout();
    auto *splitter = new QSplitter(Qt::Horizontal, this);

    side_ = new SidePanel(this);

    log_ = new QTextEdit;
    log_->setReadOnly(true);
    // log_->setMaximumHeight(50);

    // ogre_ = new OgreWidget(this);
    motor_ = new MotorPanel(this);
    camera_ = new CameraWidget(this);

    upperTabs = new QTabWidget();
    lowerTabs = new QTabWidget();

    upperTabs->setTabsClosable(true);
    lowerTabs->setTabsClosable(true);
    upperTabs->addTab(connect_, "Connector");
    lowerTabs->addTab(log_, "Log");

    // auto *mainPanel = new QWidget();
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
    // lay->setContentsMargins(5, 0, 5, 5);

    // mainContent->setLayout(lay);
    // mainContent->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // rootLayout->addWidget(mainContent);
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
    connect(upperTabs, &QTabWidget::tabCloseRequested, this, &MainWindow::onUpperTabCloseRequested);
    connect(lowerTabs, &QTabWidget::tabCloseRequested, this, &MainWindow::onLowerTabCloseRequested);

    auto* tabsMenu = menuBar()->addMenu("&Tabs");
    auto* actDiagram = tabsMenu->addAction("Open &Diagrams");
    auto* actOpenDetect = tabsMenu->addAction("Open &Hand Detect");

    connect(actOpenDetect, &QAction::triggered, this, &MainWindow::openCameraTab);

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
    for (int i = 0; i < deg.size() && i < (int)joint_names.size(); ++i)
    {
        float rad = float(deg[i]) * float(M_PI) / 180.0f / 100.0;
        ctrl_->submit_target(joint_names[i].toUtf8().constData(), rad);
        std::cout << "cmd_type: " << joint_names[i].toUtf8().constData() << ", val: " << rad << "\n";
        log_->append("Follow: " + QString(joint_names[i].toUtf8().constData()) + ", " + QString::number(rad));
    }
    // log_->append("Follow: " + QString::number(deg.size()) + " targets sent");
    if (dev_cfg_.device_type == 0)
        ctrl_->set_control_mode("Position");
    else {
        float rad = float(deg[2]);
        ctrl_->submit_target(side_->get_control_mode(), rad);
        ctrl_->set_control_mode(side_->get_control_mode());
        
    }
}

void MainWindow::onSidePanelCommitted()
{
    auto deg = side_->jointDegrees();
    for (int i = 0; i < deg.size() && i < (int)joint_names.size(); ++i)
    {
        float rad = float(deg[i]) * float(M_PI) / 180.0f / 100.0;
        ctrl_->submit_target(joint_names[i].toUtf8().constData(), rad);
    }
    log_->append("Execute: batch targets sent.");
}

void MainWindow::onSidePanelNotify(const QString &msg)
{
    log_->append(msg);
}

void MainWindow::onConnectorDeviceConnect()
{
    try
    {
        dev_cfg_ = grip::load_device_settings(config_path_);
        if (dev_cfg_.device_type == 0){
            hand_cfg_ = grip::load_hand_settings(config_path_);
            motor_->load(hand_cfg_);}
        else 
            act_cfg_ = grip::load_actuator_settings(config_path_);

        // qDebug() << dev_cfg_.device_type;
        if (dev_cfg_.device_type == 0)
        {
            // for (const auto &[key, value] : hand_cfg_.structs)
            // {
            //     for (auto &s : value.joints)
            //         joint_names << QString::fromStdString(s);
            // }
            // side_->loadSliders(joint_names);
            // qDebug() << "Hand Controllor loaded";
            side_->loadHandControllor(hand_cfg_);
        } else {
            side_->loadActuatorControllor(act_cfg_);
            // qDebug() << "Actuator Controllor loaded";
        }

        if (dev_cfg_.device_type == 0) {
            ctrl_ = new grip::ControlInterface(hand_cfg_, std::chrono::milliseconds(10));
            // ctrl_->start_bus(cfg_.can_device);
            log_->append("Device Connected. ");
            log_->append("Start Calibration... ");
            // ctrl_->run_calibration();
            // while (ctrl_->get_board_state() != 1)
            //     log_->append(".");
            log_->append("Calibration Complete. ");
            ctrl_->start_control_loop();
            log_->append("Control Policy Applied. \nCurrent Policy: Protected Hall Position Control");
            log_->append("Current control mode: " + side_->modeText());
            auto *store = new ActionStore(action_path_.c_str());
            store->load();
            side_->updateStore(store);
            side_->loadActions();

            // auto timer_ = new QTimer(this);
            // connect(timer_, &QTimer::timeout, this, &MainWindow::readJointAngles);
            // timer_->start(10);
        } else {
            ctrl_ = new grip::ControlInterface(act_cfg_, std::chrono::milliseconds(10));
            ctrl_->start_bus(cfg_.can_device);
            log_->append("Device Connected. ");
            ctrl_->start_control_loop();
        }
    }
    catch (const std::exception &e)
    {
        const QString qerr = QString(e.what());
        log_->append(qerr);
    }
}

void MainWindow::onCameraWidgetControlSwitch(const QString &msg)
{
    side_->modeChange(msg);
    if (msg == "Start Camera")
    {
        // qDebug() << "Mode camera";
        QMetaObject::invokeMethod(worker_, "start", Qt::QueuedConnection);
        // qDebug() << "Thread started";
    }
    else
    {
        // emit requestStop();
        QMetaObject::invokeMethod(worker_, "stop", Qt::BlockingQueuedConnection);
    }
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

    // std::vector<float> values = [thumb_angle, index_angle, middle_angle, ring_angle, pinky_angle];

    side_->updateJoints(thumb_angle, index_angle, middle_angle, ring_angle, pinky_angle);
}

void MainWindow::onConnectorModelChange(const QString &model_name)
{
    config_path_ = std::string("configs/config_") + model_name.toUtf8().constData() + std::string(".yaml");
    // qDebug() << config_path_.c_str();
    action_path_ = std::string("data/action_") + model_name.toUtf8().constData() + std::string(".json");
    // qDebug() << action_path_.c_str();
    // if (ogre_->loadURDF("robots/demohand/urdf/Demo_urdf_v1.urdf"))
    //     log_->append("urdf loaded");
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
