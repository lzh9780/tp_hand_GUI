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

MainWindow::MainWindow(const grip::AppSettings &cfg,
                       const std::vector<grip::JointDevice> &devices,
                       grip::ControlInterface *ctrl,
                       QWidget *parent)
    : QMainWindow(parent), devices_(devices), ctrl_(ctrl), cfg_(cfg)
{
    QStringList names;
    for (auto &s : cfg.joints)
        names << QString::fromStdString(s);
    buildUI(names);
    resize(800, 600);

    // Worker thread
    workerThread_ = new QThread(this);
    worker_ = new PyCamWorker();
    worker_->moveToThread(workerThread_);

    // Start worker when thread starts
    connect(workerThread_, &QThread::started, worker_, &PyCamWorker::start);
    connect(this, &QObject::destroyed, worker_, &PyCamWorker::stop);
    connect(workerThread_, &QThread::finished, worker_, &QObject::deleteLater);

    // Display frames
    connect(worker_, &PyCamWorker::newFrame, this, [this](const QImage& img){
        // label_->setPixmap(QPixmap::fromImage(img));
        side_->setFrame(img);
        // statusBar()->showMessage(QString("Frame %1x%2").arg(img.width()).arg(img.height()), 100);
    });

    connect(worker_, &PyCamWorker::newJoint, this, [this](const QVector<double>& joints){
        // label_->setPixmap(QPixmap::fromImage(img));
        side_->applyCameraData(joints);
        // statusBar()->showMessage(QString("Frame %1x%2").arg(img.width()).arg(img.height()), 100);
    });
    connect(worker_, &PyCamWorker::status, this, [this](const QString& s){
        statusBar()->showMessage(s, 2000);
    });
    connect(worker_, &PyCamWorker::error, this, [this](const QString& e){
        statusBar()->showMessage(QString("Error: %1").arg(e), 5000);
    });

    // QTimer* timer_ = new QTimer(this);
    // connect(timer_, &QTimer::timeout, worker_, &PyCamWorker::onTick);
    // timer_->start(1000/60);
}

void MainWindow::buildUI(const QStringList &jointNames)
{
    central = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(central);
    rootLayout->setSpacing(5);

    top_ = new TopPanel(this);
    rootLayout->addWidget(top_);

    mainContent = new QWidget;
    auto *lay = new QHBoxLayout();
    auto *splitter = new QSplitter(Qt::Horizontal, this);

    auto *store = new ActionStore("saveData.json");
    store->load();
    side_ = new SidePanel(this, jointNames, store);

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
    for (int i = 0; i < deg.size() && i < (int)devices_.size(); ++i)
    {
        float rad = float(deg[i]) * float(M_PI) / 180.0f;
        ctrl_->submit_target(devices_[i], rad);
    }
    log_->append("Follow: " + QString::number(deg.size()) + " targets sent");
}

void MainWindow::onSidePanelCommitted()
{
    auto deg = side_->jointDegrees();
    for (int i = 0; i < deg.size() && i < (int)devices_.size(); ++i)
    {
        float rad = float(deg[i]) * float(M_PI) / 180.0f;
        ctrl_->submit_target(devices_[i], rad);
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
        ctrl_->start_bus(cfg_.can_device);
        log_->append("Device Connected. ");
        log_->append("Start Calibration... ");
        ctrl_->run_calibration(cfg_.calibration);
        log_->append("Calibration Complete. ");
        ctrl_->start_control_loop();
        log_->append("Control Policy Applied. \nCurrent Policy: Protected Hall Position Control");
        log_->append("Current control mode: " + side_->modeText());
    }
    catch (const std::exception &e)
    {
        const QString qerr = QString(e.what());
        log_->append(qerr);
    }
}

void MainWindow::onTopPanelControlSwitch(const QString &msg) {
    side_->modeChange(msg);
    if (msg == "Camera") {
        qDebug() << "Mode camera";
        workerThread_->start();
        qDebug() << "Thread started";
    } else {
        workerThread_->quit();
        workerThread_->wait();
    }
}
