#include "CameraWidget.hpp"
#include "TeleOperation.hpp"
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
#include <QComboBox>
#include <QCoreApplication>
#include <QDebug>
#include "CameraHandDetect.hpp"

#ifdef USE_UDE_TELEOP
#include "UDETeleop.hpp"
#endif

#ifdef USE_MANUS_TELEOP
#include "ManusTeleop.hpp"
#endif

static const std::vector<std::string> full_joints_list{
    "thumb_cmc_pull",
    "thumb_rot",
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

    selectBox = new QComboBox();
    selectBox->addItem("Camera");
#ifdef USE_UDE_TELEOP
    selectBox->addItem("UDE Glove");
#endif
#ifdef USE_MANUS_TELEOP
    selectBox->addItem("Manus Glove");
#endif
    selectBox->setCurrentIndex(0);
    switchBtn = new QPushButton("Start");
    auto calibBtn = new QPushButton("Calibrate");
    img_label_ = new QLabel();

    img_label_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    img_label_->setMinimumSize(0, 0);
    img_label_->setAlignment(Qt::AlignCenter);

    auto hb = new QHBoxLayout();
    hb->addWidget(selectBox);
    hb->addWidget(switchBtn);
    hb->addWidget(calibBtn);
    root->addLayout(hb);
    root->addWidget(img_label_, 1);
    connect(switchBtn, &QPushButton::clicked, this, &CameraWidget::onSwitch);
    connect(calibBtn, &QPushButton::clicked, this, &CameraWidget::record);
}

void CameraWidget::stopThread()
{
    if (!workerThread_)
        return;

    workerThread_->quit();
    if (!workerThread_->wait(3000))
    {
        // The worker did not react to quit(); never spin forever in the UI.
        workerThread_->terminate();
        workerThread_->wait(3000);
    }
}

void CameraWidget::start()
{
    if (!control)
    {
        emit error("No tele-operation source available for the selected device.");
        onChange(false);
        return;
    }

    workerThread_ = new QThread(this);
    // control = new TeleopBase();
    control->moveToThread(workerThread_);

    // Display frames
    connect(control, &TeleopBase::newJoint, this, &CameraWidget::dataProcess);
    connect(control, &TeleopBase::status, this, [this](const QString &s)
            { emit status(s); });
    connect(control, &TeleopBase::error, this, [this](const QString &e)
            { emit error(e); });

    connect(control, &TeleopBase::runningChanged, this, [this](bool r)
            { onChange(r); });

    connect(control, &TeleopBase::newFrame, this, [this](const QImage &img)
            {
                // Acknowledge first so an early return cannot stall the producer.
                if (control)
                    QMetaObject::invokeMethod(control, "acknowledgeFrame", Qt::QueuedConnection);
                setFrame(img);
            });

    workerThread_->start();
    QMetaObject::invokeMethod(control, "start", Qt::QueuedConnection);
}

void CameraWidget::onSwitch()
{
    switchBtn->setEnabled(false);
    onControlSwitch(switchBtn->text());
}

void CameraWidget::record()
{
    if (last_data.size() != (int)full_joints_list.size())
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
    switchBtn->setText(on ? "Stop" : "Start");
    switchBtn->blockSignals(false);
    switchBtn->setEnabled(true);
}

void CameraWidget::onControlSwitch(const QString &msg)
{
    emit controlSwitch(msg);

    if (msg != "Start")
    {
        stopAll();
        return;
    }

    // Never run two sources at the same time.
    if (control)
        stopAll();

    const QString source = selectBox->currentText();
    if (source == "Camera")
        control = new PyCamWorker();
#ifdef USE_UDE_TELEOP
    else if (source == "UDE Glove")
        control = new UDETeleop();
#endif
#ifdef USE_MANUS_TELEOP
    else if (source == "Manus Glove")
        control = new ManusTeleop();
#endif

    if (!control)
    {
        emit error(QString("Tele-operation source '%1' is not available in this build.").arg(source));
        onChange(false);
        return;
    }

    start();
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
    // Acknowledge first so an early return cannot stall the producer: the
    // worker only posts a new sample once this one has been consumed.
    if (control)
        QMetaObject::invokeMethod(control, "acknowledgeJoints", Qt::QueuedConnection);

    if (joints.size() != (int)full_joints_list.size())
        return;
    last_data = joints;
    if (calibrate_data.size() != (int)full_joints_list.size())
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
    if (control)
    {
        // Ask the worker to stop, then let its thread finish so that no
        // further signal can be emitted while the object is torn down.
        QMetaObject::invokeMethod(control, "stop", Qt::QueuedConnection);
    }

    if (workerThread_ && workerThread_->isRunning())
        stopThread();

    if (control)
    {
        // deleteLater() would be posted to the (now stopped) worker thread and
        // never processed, so drop the connections and delete directly.
        control->disconnect();
        QCoreApplication::removePostedEvents(this, QEvent::MetaCall);
        delete control;
        control = nullptr;
    }

    delete workerThread_;
    workerThread_ = nullptr;

    // Make sure the button returns to "Start" even if the worker never
    // reported a running-state change.
    onChange(false);
}
