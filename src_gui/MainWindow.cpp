#include "MainWindow.h"
#include "SidePanel.h"
#include "grip/control_interface.hpp"
#include <QTextEdit>
#include <QSplitter>
#include <QVBoxLayout>

MainWindow::MainWindow(const grip::AppSettings &cfg,
                       const std::vector<grip::JointDevice> &devices,
                       grip::ControlInterface *ctrl,
                       QWidget *parent)
    : QMainWindow(parent), devices_(devices), ctrl_(ctrl)
{
    QStringList names;
    for (auto &s : cfg.joints)
        names << QString::fromStdString(s);
    buildUI(names);
}

void MainWindow::buildUI(const QStringList &jointNames)
{
    auto *central = new QWidget;
    auto *lay = new QHBoxLayout(central);
    auto *splitter = new QSplitter(Qt::Horizontal, this);

    auto *store = new ActionStore("saveData.json");
    store->load();
    side_ = new SidePanel(this, jointNames, store);
    log_ = new QTextEdit;
    log_->setReadOnly(true);

    splitter->addWidget(side_);
    splitter->addWidget(log_);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);

    lay->addWidget(splitter);
    setCentralWidget(central);

    connect(side_, &SidePanel::valuesChanged, this, &MainWindow::onSidePanelValuesChanged);
    connect(side_, &SidePanel::committed, this, &MainWindow::onSidePanelCommitted);
    connect(side_, &SidePanel::notify, this, &MainWindow::onSidePanelNotify);

    log_->append("Current control mode: " + side_->modeText());
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
