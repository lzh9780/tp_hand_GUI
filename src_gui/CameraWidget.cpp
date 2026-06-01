#include "CameraWidget.hpp"
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QImage>
#include <QPixmap>
#include <QLabel>
#include <QResizeEvent>
#include <QThread>
#include <QStatusBar>
#include "PyCamWorker.h"

static const std::vector<std::string> full_joints_list{
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
    "yaw"};

CameraWidget::CameraWidget(QWidget *parent)
    : QFrame(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    switchBtn = new QPushButton("Start Camera");
    auto calibBtn = new QPushButton("Calibrate");
    img_label_ = new QLabel();

    img_label_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    img_label_->setMinimumSize(0, 0);
    img_label_->setAlignment(Qt::AlignCenter);

    auto hb = new QHBoxLayout();
    hb->addWidget(switchBtn);
    hb->addWidget(calibBtn);
    root->addLayout(hb);
    root->addWidget(img_label_, 1);
    connect(switchBtn, &QPushButton::clicked, this, &CameraWidget::onSwitch);
    connect(calibBtn, &QPushButton::clicked, this, &CameraWidget::record);

    // Worker thread
    workerThread_ = new QThread(this);
    worker_ = new PyCamWorker();
    worker_->moveToThread(workerThread_);

    // Start worker when thread starts
    connect(this, &QObject::destroyed, workerThread_, &QThread::quit);

    // Display frames
    connect(worker_, &PyCamWorker::newFrame, this, [this](const QImage &img)
            { setFrame(img); });

    connect(worker_, &PyCamWorker::newJoint, this, &CameraWidget::dataProcess);
    connect(worker_, &PyCamWorker::status, this, [this](const QString &s)
            { emit status(s); });
    connect(worker_, &PyCamWorker::error, this, [this](const QString &e)
            { emit error(e); });

    connect(worker_, &PyCamWorker::runningChanged, this, [this](bool r)
            { onChange(r); });

    workerThread_->start();
}

void CameraWidget::onSwitch()
{
    switchBtn->setEnabled(false);
    onControlSwitch(switchBtn->text());
}

void CameraWidget::record()
{
    if (last_data.size() != full_joints_list.size())
    {
        emit error("Invaild data. Calibration failed");
        return;
    }
    calibrate_data = last_data;
}

void CameraWidget::onChange(bool on)
{
    switchBtn->blockSignals(true);
    switchBtn->setChecked(on);
    switchBtn->setText(on ? "Stop Camera" : "Start Camera");
    switchBtn->blockSignals(false);
    switchBtn->setEnabled(true);
}

void CameraWidget::onControlSwitch(const QString &msg)
{
    emit controlSwitch(msg);
    if (msg == "Start Camera")
        QMetaObject::invokeMethod(worker_, "start", Qt::QueuedConnection);
    else
        QMetaObject::invokeMethod(worker_, "stop", Qt::BlockingQueuedConnection);
}

void CameraWidget::setFrame(const QImage &img)
{
    last_frame_ = img;
    updatePixmap();
}

void CameraWidget::resizeEvent(QResizeEvent *e)
{
    QFrame::resizeEvent(e);
    updatePixmap();
}

void CameraWidget::updatePixmap()
{
    if (!img_label_ || last_frame_.isNull())
        return;

    const QSize target = img_label_->contentsRect().size();
    if (target.width() <= 0 || target.height() <= 0)
        return;

    const QPixmap pm =
        QPixmap::fromImage(last_frame_).scaled(img_label_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);

    img_label_->setPixmap(pm);
}

void CameraWidget::dataProcess(const QVector<double> &joints)
{
    if (joints.size() != full_joints_list.size())
        return;
    last_data = joints;
    if (calibrate_data.size() != full_joints_list.size())
    {
        emit dataUpdate(joints);
        return;
    }
    QVector<double> result;
    for (size_t i = 0; i < full_joints_list.size(); i++)
    {
        result.append(last_data[i] - calibrate_data[i]);
    }
    emit dataUpdate(result);
}

std::vector<std::string> CameraWidget::getFullList()
{
    return full_joints_list;
}

void CameraWidget::stopAll()
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
}
