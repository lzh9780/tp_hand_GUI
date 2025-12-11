#include "CameraWidget.hpp"
#include <QWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QString>
#include <QComboBox>
#include <QDebug>
#include <QImage>
#include <QPixmap>
#include <QLabel>

CameraWidget::CameraWidget(QWidget *parent)
    : QFrame(parent)
{
    auto *root = new QVBoxLayout(this);

    root->setContentsMargins(0, 0, 0, 0);
    switchBtn = new QPushButton("Start Camera");
    img_label_ = new QLabel();
    root->addWidget(switchBtn);
    root->addWidget(img_label_);
    connect(switchBtn, &QPushButton::clicked, this, &CameraWidget::onSwitch);
}

void CameraWidget::onSwitch() {
    switchBtn->setEnabled(false);
    if (switchBtn->text() == "Start Camera") {
        emit controlSwitch(switchBtn->text());
        // switchBtn->setText("Stop Camera");
    } else if (switchBtn->text() == "Stop Camera") {
        emit controlSwitch(switchBtn->text());
        // switchBtn->setText("Start Camera");
    }
}

void CameraWidget::onChange(bool on) {
    switchBtn->blockSignals(true);
    switchBtn->setChecked(on);
    switchBtn->setText(on ? "Stop Camera" : "Start Camera");
    switchBtn->blockSignals(false);
    switchBtn->setEnabled(true);
}

void CameraWidget::setFrame(const QImage img)
{
    // qDebug() << "Drawing frame";
    auto pm = QPixmap::fromImage(img).scaled(img_label_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation); //(img);
    img_label_->setPixmap(pm);
}
