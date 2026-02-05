#include "MotorPanel.hpp"
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QString>
#include <QComboBox>
#include <QDebug>
#include <QSpacerItem>
#include <QScrollArea>

MotorPanel::MotorPanel(QWidget *parent)
    : QFrame(parent)
{
    root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
}

void MotorPanel::load(grip::HandSettings hand)
{
    auto *scroll_ = new QScrollArea();
    auto *content = new QWidget();
    auto *contentLyt = new QVBoxLayout(content);

    for (auto s : hand.structs) {
        auto *nameLbl = new QLabel(s.first.c_str());
        contentLyt->addWidget(nameLbl);

        // for (auto m : s.second.motors) {
        //     auto *hb = new QHBoxLayout();

        //     auto *motorLbl = new QLabel(QString("motor_%1   ").arg(m));
        //     auto *motorBox = new QComboBox();
        //     motorBox->addItem("Normal");
        //     motorBox->addItem("Zero Torque");
        //     motorBox->addItem("Keep Force");
        //     motorBox->setCurrentIndex(0);

        //     hb->addWidget(motorLbl);
        //     hb->addWidget(motorBox);
        //     hb->setContentsMargins(8, 0, 0, 0);

        //     contentLyt->addLayout(hb);
        // }
    }

    auto *spacer = new QSpacerItem(40, 20, QSizePolicy::Fixed, QSizePolicy::Expanding);
    contentLyt->addItem(spacer);
    scroll_->setWidget(content);
    root->addWidget(scroll_);
    scroll_->setWidgetResizable(true);
}

void MotorPanel::removeAll()
{
    QLayoutItem *item;
    while ((item = root->takeAt(0)) != nullptr)
    {
        if (QLayout *rowLyt = item->layout())
        {
            
            QLayoutItem *child;
            while ((child = rowLyt->takeAt(0)))
            {
                if (child->widget())
                    delete child->widget();
                delete child;
            }
            delete rowLyt;
        }
        else if (item->widget())
        {
            delete item->widget();
        }
    }
}
