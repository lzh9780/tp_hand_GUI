#include "Connector.hpp"
#include <QWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QString>
#include <QComboBox>
#include <QCheckBox>
#include <QDebug>

Connector::Connector(QWidget *parent, std::vector<std::string> models)
    : QFrame(parent)
{
    auto *root = new QVBoxLayout(this);
    auto *content = new QWidget();
    content->setFixedHeight(30);
    // content->setStyleSheet("background: #fff;");
    root->addWidget(content);

    model_box = new QComboBox();
    model_box->addItem("- Select a model -");
    model_box->setItemData(0, 0, Qt::UserRole - 1);
    model_box->setCurrentIndex(0);
    for (auto name : models) {
        model_box->addItem(QString::fromUtf8(std::string(name).c_str()));
    }

    auto *background = new QHBoxLayout(content);
    background->setContentsMargins(0, 0, 0, 0);
    connectBtn = new QPushButton("Connect");
    background->addWidget(model_box);
    background->addWidget(connectBtn);
    connect(connectBtn, &QPushButton::clicked, this, &Connector::onConnect);
    connect(model_box, &QComboBox::currentTextChanged, this, &Connector::onModelChange);

    deviceState = new QCheckBox(QString("no device connect"));
    connect(deviceState, &QCheckBox::clicked, this, [=]{emit stateCheck(deviceState->checkState());});
    root->addWidget(deviceState);
}

void Connector::onConnect()
{
    model_box->setEnabled(false);
    deviceState->setEnabled(false);
    connectBtn->setEnabled(false);
    emit deviceConnect(connectBtn->text());
}

void Connector::onModelChange() {
    emit modelChange(QString(model_box->currentText()));
}

void Connector::onChange(bool on) {
    connectBtn->blockSignals(true);
    connectBtn->setChecked(on);
    connectBtn->setText(on ? "Disconnect" : "Connect");
    model_box->setEnabled(!on);
    deviceState->setEnabled(!on);
    connectBtn->blockSignals(false);
    connectBtn->setEnabled(true);
}
