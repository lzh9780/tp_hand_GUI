#include "MotorPanel.hpp"
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
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

        for (auto j : s.second)
        {
            if (j.id == -1)
                continue;
            auto *ctrlHl = new QHBoxLayout();
            auto *mode_lable = new QLabel(QString(j.name.c_str()));
            auto *mode_box = new QComboBox();

            if (s.first != "WRIST")
            {
                for (auto p : hand.control_policies)
                    mode_box->addItem(p.c_str());
            }
            else
            {
                for (auto p : hand.wrist_control)
                    mode_box->addItem(p.c_str());
            }

            auto *single = new QCheckBox(QString("Standalone"));
            ctrlHl->addWidget(mode_lable);
            ctrlHl->addWidget(mode_box);
            ctrlHl->addWidget(single);
            contentLyt->addLayout(ctrlHl);

            connect(mode_box, &QComboBox::currentTextChanged,
                this, [=]()
                { emit mode_change(mode_lable->text(), mode_box->currentText());});

            names.append(QString(j.name.c_str()));
            single_checkbox.append(single);
            mode_selecters.append(mode_box);
        }
    }

    auto *spacer = new QSpacerItem(40, 20, QSizePolicy::Fixed, QSizePolicy::Expanding);
    contentLyt->addItem(spacer);
    scroll_->setWidget(content);
    root->addWidget(scroll_);
    scroll_->setWidgetResizable(true);
}

bool MotorPanel::checkJointStandalone(QString joint_name)
{
    int index = names.indexOf(joint_name); 
    if (index == -1)
        return false;
    else 
        return single_checkbox[index]->checkState() == Qt::Checked;
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

void MotorPanel::applyModeChange(QString name, QString mode)
{
    int i = names.indexOf(name); 
    if (i == -1)
        return;
    else
    {
        int index = mode_selecters[i]->findText(mode);
        if (index != -1)
            mode_selecters[i]->setCurrentIndex(index);
    }
}
