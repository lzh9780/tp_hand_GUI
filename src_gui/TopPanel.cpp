#include "TopPanel.hpp"
#include <QWidget>
#include <QHBoxLayout>
#include <QPushButton>

TopPanel::TopPanel(QWidget *parent)
    : QFrame(parent)
{
    auto *root = new QHBoxLayout(this);
    auto *content = new QWidget();
    content->setFixedHeight(30);
    content->setStyleSheet("background: #fff;");
    root->addWidget(content);
    auto *background = new QHBoxLayout(content);
    background->setContentsMargins(0, 0, 0, 0);
    switchBtn = new QPushButton("Start Camera");
    background->addWidget(switchBtn);
    connectBtn = new QPushButton("Connect");
    background->addWidget(connectBtn);
    connect(connectBtn, &QPushButton::clicked, this, &TopPanel::onConnect);
    connect(switchBtn, &QPushButton::clicked, this, &TopPanel::onSwitch);
}

void TopPanel::onConnect()
{
    emit deviceConnect();
}

void TopPanel::onSwitch() {
    switchBtn->setEnabled(false);
    if (switchBtn->text() == "Start Camera") {
        emit controlSwitch(switchBtn->text());
        switchBtn->setText("Stop Camera");
    } else if (switchBtn->text() == "Stop Camera") {
        emit controlSwitch(switchBtn->text());
        switchBtn->setText("Start Camera");
    }
}

void TopPanel::onChange(bool on) {
    switchBtn->blockSignals(true);
    switchBtn->setChecked(on);
    switchBtn->setText(on ? "Stop Camera" : "Start Camera");
    switchBtn->blockSignals(false);
    switchBtn->setEnabled(true);
}
