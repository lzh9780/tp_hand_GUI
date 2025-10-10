#include "MainWindow.h"
#include "SidePanel.h"
#include "TopPanel.hpp"
#include "grip/control_interface.hpp"
#include <QTextEdit>
#include <QSplitter>
#include <QHBoxLayout>
#include <QVBoxLayout>

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
