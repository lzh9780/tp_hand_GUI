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
    root->setContentsMargins(0, 0, 0, 0);
    auto *connectBtn = new QPushButton("Connect");
    root->addWidget(connectBtn);
    connect(connectBtn, &QPushButton::clicked, this, &TopPanel::onConnect);
}

void TopPanel::onConnect()
{
    emit deviceConnect();
}