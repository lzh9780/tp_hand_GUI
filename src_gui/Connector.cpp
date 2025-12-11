#include "Connector.hpp"
#include <QWidget>
#include <QHBoxLayout>
#include <QPushButton>
#include <QString>
#include <QComboBox>
#include <QDebug>

Connector::Connector(QWidget *parent, std::vector<std::string> models)
    : QFrame(parent)
{
    auto *root = new QHBoxLayout(this);
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
}

void Connector::onConnect()
{
    emit deviceConnect();
}

void Connector::onModelChange() {
    emit modelChange(QString(model_box->currentText()));
}
