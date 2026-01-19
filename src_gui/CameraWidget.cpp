#include "CameraWidget.hpp"
#include <QWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QImage>
#include <QPixmap>
#include <QLabel>
#include <QResizeEvent>

CameraWidget::CameraWidget(QWidget *parent)
    : QFrame(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    switchBtn = new QPushButton("Start Camera");
    img_label_ = new QLabel();

    img_label_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    img_label_->setMinimumSize(0, 0);
    img_label_->setAlignment(Qt::AlignCenter);

    root->addWidget(switchBtn);
    root->addWidget(img_label_, 1);
    connect(switchBtn, &QPushButton::clicked, this, &CameraWidget::onSwitch);
}

void CameraWidget::onSwitch()
{
    switchBtn->setEnabled(false);
    emit controlSwitch(switchBtn->text());
}

void CameraWidget::onChange(bool on)
{
    switchBtn->blockSignals(true);
    switchBtn->setChecked(on);
    switchBtn->setText(on ? "Stop Camera" : "Start Camera");
    switchBtn->blockSignals(false);
    switchBtn->setEnabled(true);
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
