#include "TopPanel.hpp"
#include <QWidget>
#include <QHBoxLayout>
#include <QPushButton>
#include <QString>
#include <QComboBox>
#include <QDebug>

TopPanel::TopPanel(QWidget *parent, std::vector<std::string> models)
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
    switchBtn = new QPushButton("Start Camera");
    background->addWidget(switchBtn);
    connectBtn = new QPushButton("Connect");
    background->addWidget(model_box);
    background->addWidget(connectBtn);
    connect(connectBtn, &QPushButton::clicked, this, &TopPanel::onConnect);
    connect(switchBtn, &QPushButton::clicked, this, &TopPanel::onSwitch);
    connect(model_box, &QComboBox::currentTextChanged, this, &TopPanel::onModelChange);
}

void TopPanel::onConnect()
{
    emit deviceConnect();
}

void TopPanel::onSwitch() {
    switchBtn->setEnabled(false);
    if (switchBtn->text() == "Start Camera") {
        emit controlSwitch(switchBtn->text());
        // switchBtn->setText("Stop Camera");
    } else if (switchBtn->text() == "Stop Camera") {
        emit controlSwitch(switchBtn->text());
        // switchBtn->setText("Start Camera");
    }
}

void TopPanel::onChange(bool on) {
    switchBtn->blockSignals(true);
    switchBtn->setChecked(on);
    switchBtn->setText(on ? "Stop Camera" : "Start Camera");
    switchBtn->blockSignals(false);
    switchBtn->setEnabled(true);
}

void TopPanel::onModelChange() {
    emit modelChange(QString(model_box->currentText()));
}
