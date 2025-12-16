#include "MotorPanel.hpp"
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QString>
#include <QComboBox>
#include <QDebug>
#include <QSpacerItem>

MotorPanel::MotorPanel(QWidget *parent)
    : QFrame(parent)
{
}

void MotorPanel::load(grip::HandSettings hand)
{
    auto *root = new QVBoxLayout(this);

    for (auto s : hand.structs) {
        auto *nameLbl = new QLabel(s.first.c_str());
        root->addWidget(nameLbl);

        for (auto m : s.second.motors) {
            auto *hb = new QHBoxLayout();

            auto *motorLbl = new QLabel(QString("motor_%1   ").arg(m));
            auto *motorBox = new QComboBox();
            motorBox->addItem("Normal");
            motorBox->addItem("Zero Torque");
            motorBox->addItem("Keep Force");
            motorBox->setCurrentIndex(0);

            hb->addWidget(motorLbl);
            hb->addWidget(motorBox);
            hb->setContentsMargins(8, 0, 0, 0);

            root->addLayout(hb);
        }
    }

    auto *spacer = new QSpacerItem(40, 20, QSizePolicy::Fixed, QSizePolicy::Expanding);
    root->addItem(spacer);
}
