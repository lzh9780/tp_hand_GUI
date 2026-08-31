#include "Connector.hpp"
#include <QWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QString>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QDebug>

Connector::Connector(QWidget *parent, std::vector<std::string> models)
    : QFrame(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);
    root->setAlignment(Qt::AlignTop);

    auto *group = new QGroupBox("Device");
    auto *form = new QFormLayout(group);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setContentsMargins(12, 14, 12, 12);
    form->setSpacing(10);

    model_box = new QComboBox();
    model_box->addItem("- Select a model -");
    model_box->setItemData(0, 0, Qt::UserRole - 1);
    model_box->setCurrentIndex(0);
    for (const auto &name : models)
    {
        model_box->addItem(QString::fromStdString(name));
    }

    connectBtn = new QPushButton("Connect");
    connectBtn->setFixedWidth(110);
    connectBtn->setEnabled(false); // enabled once a real model is picked

    auto *modelRow = new QHBoxLayout();
    modelRow->setSpacing(8);
    modelRow->addWidget(model_box, 1);
    modelRow->addWidget(connectBtn);

    deviceState = new QCheckBox("Simulation mode (no hardware connected)");
    deviceState->setToolTip("Run without a CAN bus: the GUI stays fully "
                            "interactive but no commands reach the hand.");

    statusLabel_ = new QLabel("Not connected");

    form->addRow("Model:", modelRow);
    form->addRow("", deviceState);
    form->addRow("Status:", statusLabel_);

    root->addWidget(group);
    root->addStretch(1);

    connect(connectBtn, &QPushButton::clicked, this, &Connector::onConnect);
    connect(model_box, &QComboBox::currentTextChanged, this, &Connector::onModelChange);
    connect(deviceState, &QCheckBox::clicked, this, [this]
            { emit stateCheck(deviceState->checkState()); });
}

void Connector::onConnect()
{
    if (model_box->currentIndex() <= 0)
    {
        statusLabel_->setText("Please select a model first.");
        return;
    }

    model_box->setEnabled(false);
    deviceState->setEnabled(false);
    connectBtn->setEnabled(false);
    emit deviceConnect(connectBtn->text());
}

void Connector::onModelChange()
{
    const bool valid = model_box->currentIndex() > 0;
    connectBtn->setEnabled(valid);
    if (!valid)
        statusLabel_->setText("Not connected");
    emit modelChange(QString(model_box->currentText()));
}

void Connector::onChange(bool on)
{
    connectBtn->blockSignals(true);
    connectBtn->setChecked(on);
    connectBtn->setText(on ? "Disconnect" : "Connect");
    model_box->setEnabled(!on);
    deviceState->setEnabled(!on);
    connectBtn->setEnabled(true);
    connectBtn->blockSignals(false);

    statusLabel_->setText(on
        ? QString("Connected: %1").arg(model_box->currentText())
        : QString("Not connected"));
}
