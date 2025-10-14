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
    switchBtn = new QPushButton("Joints");
    background->addWidget(switchBtn);
    auto *connectBtn = new QPushButton("Connect");
    background->addWidget(connectBtn);
    connect(connectBtn, &QPushButton::clicked, this, &TopPanel::onConnect);
    connect(switchBtn, &QPushButton::clicked, this, &TopPanel::onSwitch);
}

void TopPanel::onConnect()
{
    emit deviceConnect();
}

void TopPanel::onSwitch() {
    if (switchBtn->text() == "Joints") {
        switchBtn->setText("Camera");
        emit controlSwitch(switchBtn->text());
    } else if (switchBtn->text() == "Camera") {
        switchBtn->setText("Joints");
        emit controlSwitch(switchBtn->text());
    }
}
