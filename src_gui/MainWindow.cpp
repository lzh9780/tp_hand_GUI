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
            { side_->setFrame(img); });

    connect(worker_, &PyCamWorker::newJoint, this, [this](const QVector<double> &joints)
            { side_->applyCameraData(joints); });
    connect(worker_, &PyCamWorker::status, this, [this](const QString &s)
            { statusBar()->showMessage(s, 2000); });
    connect(worker_, &PyCamWorker::error, this, [this](const QString &e)
            {
        statusBar()->showMessage(QString("Error: %1").arg(e), 5000);
        log_->append(QString("Error: %1").arg(e)); });

    connect(worker_, &PyCamWorker::runningChanged, this, [this](bool r)
            { top_->onChange(r); });

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
    ctrl_->stop_all();
}

void MainWindow::buildUI()
{
    central = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(central);
    rootLayout->setSpacing(5);

    top_ = new TopPanel(this, cfg_.models);
    rootLayout->addWidget(top_);

    mainContent = new QWidget;
    auto *lay = new QHBoxLayout();
    auto *splitter = new QSplitter(Qt::Horizontal, this);

    side_ = new SidePanel(this);

    log_ = new QTextEdit;
    log_->setReadOnly(true);

    splitter->addWidget(side_);
    splitter->addWidget(log_);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    lay->addWidget(splitter);
    lay->setContentsMargins(5, 0, 5, 5);

    mainContent->setLayout(lay);
    mainContent->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    rootLayout->addWidget(mainContent);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    central->setLayout(rootLayout);

    setCentralWidget(central);

    connect(side_, &SidePanel::valuesChanged, this, &MainWindow::onSidePanelValuesChanged);
    connect(side_, &SidePanel::committed, this, &MainWindow::onSidePanelCommitted);
    connect(side_, &SidePanel::notify, this, &MainWindow::onSidePanelNotify);
    connect(top_, &TopPanel::deviceConnect, this, &MainWindow::onTopPanelDeviceConnect);
    connect(top_, &TopPanel::controlSwitch, this, &MainWindow::onTopPanelControlSwitch);
    connect(top_, &TopPanel::modelChange, this, &MainWindow::onTopPanelModelChange);

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
        float rad = float(deg[i]) * float(M_PI) / 180.0f;
        ctrl_->submit_target(joint_names[i].toUtf8().constData(), rad);
        // log_->append("Follow: " + QString(joint_names[i].toUtf8().constData()) + ", " + QString::number(rad));
    }
    // log_->append("Follow: " + QString::number(deg.size()) + " targets sent");
}

void MainWindow::onSidePanelCommitted()
{
    auto deg = side_->jointDegrees();
    for (int i = 0; i < deg.size() && i < (int)joint_names.size(); ++i)
    {
        float rad = float(deg[i]) * float(M_PI) / 180.0f;
        ctrl_->submit_target(joint_names[i].toUtf8().constData(), rad);
    }
    log_->append("Execute: batch targets sent.");
}

void MainWindow::onSidePanelNotify(const QString &msg)
{
    log_->append(msg);
}

void MainWindow::onTopPanelDeviceConnect()
{
    try
    {
        hand_cfg_ = grip::load_hand_settings(config_path_);
        ctrl_ = new grip::ControlInterface(hand_cfg_, std::chrono::milliseconds(10));
        ctrl_->start_bus(cfg_.can_device);
        log_->append("Device Connected. ");
        log_->append("Start Calibration... ");
        // ctrl_->run_calibration(cfg_.calibration);
        while (ctrl_->get_board_state() != 1)
            log_->append(".");
        log_->append("Calibration Complete. ");
        ctrl_->start_control_loop();
        log_->append("Control Policy Applied. \nCurrent Policy: Protected Hall Position Control");
        log_->append("Current control mode: " + side_->modeText());
        auto *store = new ActionStore(action_path_.c_str());
        store->load();
        side_->updateStore(store);
        side_->loadActions();
        
        for (const auto &[key, value] : hand_cfg_.structs)
        {
            for (auto &s : value.joints)
                joint_names << QString::fromStdString(s);
        }
        side_->loadSliders(joint_names);

        auto timer_ = new QTimer(this);
        connect(timer_, &QTimer::timeout, this, &MainWindow::readJointAngles);
        timer_->start(10);
    }
    catch (const std::exception &e)
    {
        const QString qerr = QString(e.what());
        log_->append(qerr);
    }
}

void MainWindow::onTopPanelControlSwitch(const QString &msg)
{
    side_->modeChange(msg);
    if (msg == "Start Camera")
    {
        qDebug() << "Mode camera";
        QMetaObject::invokeMethod(worker_, "start", Qt::QueuedConnection);
        qDebug() << "Thread started";
    }
    else
    {
        // emit requestStop();
        QMetaObject::invokeMethod(worker_, "stop", Qt::BlockingQueuedConnection);
    }
}

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

void MainWindow::onTopPanelModelChange(const QString &model_name)
{
    config_path_ = std::string("configs/config_") + model_name.toUtf8().constData() + std::string(".yaml");
    qDebug() << config_path_.c_str();
    action_path_ = std::string("data/action_") + model_name.toUtf8().constData() + std::string(".json");
    qDebug() << action_path_.c_str();
}
