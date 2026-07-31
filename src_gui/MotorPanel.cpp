#include "MotorPanel.hpp"
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QDebug>
#include <QSpacerItem>
#include <QScrollArea>
#include <QSlider>
#include <QLineEdit>

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

    for (auto s : hand.structs)
    {
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

            auto *velHl = new QHBoxLayout();
            velHl->addWidget(new QLabel("Velocity Ref: "));
            velHl->addWidget(new QLabel("0"));
            auto *velSlider = new QSlider(Qt::Horizontal);
            velSlider->setRange(0, 2000);
            velHl->addWidget(velSlider);
            velHl->addWidget(new QLabel("20"));
            auto *velEdit = new QLineEdit("0.00");
            velEdit->setFixedWidth(75);
            velEdit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            velHl->addWidget(velEdit);
            velHl->addWidget(new QLabel("rad/s"));
            contentLyt->addLayout(velHl);

            auto *torqueHl = new QHBoxLayout();
            torqueHl->addWidget(new QLabel("Torque Ref:   "));
            torqueHl->addWidget(new QLabel("0"));
            auto *torqueSlider = new QSlider(Qt::Horizontal);
            torqueSlider->setRange(0, 500);
            torqueHl->addWidget(torqueSlider);
            torqueHl->addWidget(new QLabel("500"));
            auto *torqueEdit = new QLineEdit("0");
            torqueEdit->setFixedWidth(75);
            torqueEdit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            torqueHl->addWidget(torqueEdit);
            torqueHl->addWidget(new QLabel("mA"));
            contentLyt->addLayout(torqueHl);

            velSlider->setEnabled(false);
            velEdit->setEnabled(false);
            torqueSlider->setEnabled(false);
            torqueEdit->setEnabled(false);
            velSlider->setValue(0);
            torqueSlider->setValue(0);

            connect(mode_box, &QComboBox::currentTextChanged,
                    this, [=]()
                    { emit mode_change(mode_lable->text(), mode_box->currentText()); 
                      if (mode_box->currentText() == "MIT_torque")
                      {
                          velSlider->setEnabled(true);
                          velEdit->setEnabled(true);
                          torqueSlider->setEnabled(true);
                          torqueEdit->setEnabled(true);
                      } 
                      else 
                      {
                          velSlider->setEnabled(false);
                          velEdit->setEnabled(false);
                          torqueSlider->setEnabled(false);
                          torqueEdit->setEnabled(false);
                          velSlider->setValue(0);
                          torqueSlider->setValue(0);
                      } });

            connect(velSlider, &QSlider::valueChanged,
                    this, [=]()
                    { velEdit->setText(QString("%1").arg((float)velSlider->value() / 100.0)); 
                      emit vel_ref_change(mode_lable->text(), velSlider->value());
                    });
            connect(velEdit, &QLineEdit::editingFinished,
                    this, [=]()
                    { velSlider->setValue((int)(atof(velEdit->text().toUtf8().constData()) * 100.0)); });

            connect(torqueSlider, &QSlider::valueChanged,
                    this, [=]()
                    { torqueEdit->setText(QString("%1").arg(torqueSlider->value())); 
                      emit torque_ref_change(mode_lable->text(), torqueSlider->value());
                    });
            connect(torqueEdit, &QLineEdit::editingFinished,
                    this, [=]()
                    { torqueSlider->setValue((int)(atof(torqueEdit->text().toUtf8().constData()))); });

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
    names.clear();
    single_checkbox.clear();
    mode_selecters.clear();
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
