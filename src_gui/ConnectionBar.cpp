#include "ConnectionBar.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QSpinBox>

ConnectionBar::ConnectionBar(QWidget *parent, std::vector<std::string> models)
    : QFrame(parent)
{
    setFrameShape(QFrame::StyledPanel);

    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(8, 6, 8, 6);
    row->setSpacing(8);

    /* ---- model ---- */
    row->addWidget(new QLabel("Model:"));
    modelBox_ = new QComboBox(this);
    modelBox_->addItem("- Select a model -");
    modelBox_->setItemData(0, 0, Qt::UserRole - 1);
    modelBox_->setCurrentIndex(0);
    for (const auto &name : models)
        modelBox_->addItem(QString::fromStdString(name));
    modelBox_->setMinimumWidth(160);
    row->addWidget(modelBox_, 1);

    /* ---- CAN device index / channel ---- */
    row->addWidget(new QLabel("Device:"));
    deviceIndex_ = new QSpinBox(this);
    deviceIndex_->setRange(0, 15);
    deviceIndex_->setValue(0);
    deviceIndex_->setFixedWidth(60);
    deviceIndex_->setToolTip("CAN device index (USBCANFD board number).");
    row->addWidget(deviceIndex_);

    row->addWidget(new QLabel("Channel:"));
    channelIndex_ = new QSpinBox(this);
    channelIndex_->setRange(0, 3);
    channelIndex_->setValue(0);
    channelIndex_->setFixedWidth(60);
    channelIndex_->setToolTip("CAN channel on the selected device.");
    row->addWidget(channelIndex_);

    /* ---- connect + status ---- */
    connectBtn_ = new QPushButton("Connect", this);
    connectBtn_->setFixedWidth(110);
    connectBtn_->setEnabled(false); // enabled once a real model is picked
    row->addWidget(connectBtn_);

    statusLabel_ = new QLabel("Not connected", this);
    statusLabel_->setMinimumWidth(160);
    row->addWidget(statusLabel_);
    row->addStretch(1);

    connect(connectBtn_, &QPushButton::clicked, this, &ConnectionBar::onConnect);
    connect(modelBox_, &QComboBox::currentTextChanged, this, &ConnectionBar::onModelChange);
    connect(deviceIndex_, qOverload<int>(&QSpinBox::valueChanged),
            this, &ConnectionBar::emitDeviceLocation);
    connect(channelIndex_, qOverload<int>(&QSpinBox::valueChanged),
            this, &ConnectionBar::emitDeviceLocation);
}

void ConnectionBar::onConnect()
{
    if (modelBox_->currentIndex() <= 0)
    {
        statusLabel_->setText("Please select a model first.");
        return;
    }

    modelBox_->setEnabled(false);
    connectBtn_->setEnabled(false);

    emit deviceConnect(connectBtn_->text());
}

void ConnectionBar::onModelChange()
{
    const bool valid = modelBox_->currentIndex() > 0;
    connectBtn_->setEnabled(valid);
    if (!valid)
        statusLabel_->setText("Not connected");
    emit modelChange(modelBox_->currentText());
}

void ConnectionBar::emitDeviceLocation()
{
    emit canConfigChanged(deviceIndex_->value(), channelIndex_->value());
}

void ConnectionBar::onChange(bool on)
{
    connectBtn_->blockSignals(true);
    connectBtn_->setText(on ? "Disconnect" : "Connect");
    connectBtn_->setEnabled(true);
    connectBtn_->blockSignals(false);

    // The device / channel are read when the bus is opened: freeze them while
    // connected so the UI cannot advertise a location the device is not on.
    modelBox_->setEnabled(!on);
    deviceIndex_->setEnabled(!on);
    channelIndex_->setEnabled(!on);

    statusLabel_->setText(on
        ? QString("Connected: %1").arg(modelBox_->currentText())
        : QString("Not connected"));
}

QString ConnectionBar::currentModel() const
{
    return modelBox_->currentText();
}

void ConnectionBar::setStatus(const QString &text)
{
    statusLabel_->setText(text);
}

void ConnectionBar::setDeviceLocation(int device_index, int channel_index)
{
    const bool blockedIdx = deviceIndex_->blockSignals(true);
    const bool blockedCh = channelIndex_->blockSignals(true);
    deviceIndex_->setValue(device_index);
    channelIndex_->setValue(channel_index);
    deviceIndex_->blockSignals(blockedIdx);
    channelIndex_->blockSignals(blockedCh);
}
