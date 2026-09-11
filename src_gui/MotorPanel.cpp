#include "MotorPanel.hpp"
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QSlider>
#include <QLineEdit>
#include <QSpacerItem>
#include <QDebug>

MotorPanel::MotorPanel(QWidget *parent)
    : QFrame(parent)
{
    root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    auto *hint = new QLabel("Velocity / torque references only apply while a joint runs in MIT mode.\n"
                            "Tick \"Standalone\" to exempt a joint from the global control mode.");
    hint->setWordWrap(true);
    hint->setStyleSheet("color: #666;");
    root->addWidget(hint);
}

void MotorPanel::load(grip::HandSettings hand)
{
    auto *scroll = new QScrollArea();
    auto *content = new QWidget();
    auto *contentLyt = new QVBoxLayout(content);
    contentLyt->setContentsMargins(6, 6, 6, 6);
    contentLyt->setSpacing(8);

    for (auto s : hand.structs)
    {
        // Skip modules that only contain passive (non-actuated) joints.
        bool moduleHasJoint = false;
        for (auto j : s.second)
        {
            if (j.id != -1)
            {
                moduleHasJoint = true;
                break;
            }
        }
        if (!moduleHasJoint)
            continue;

        // One group box per anatomical module keeps the list scannable.
        auto *moduleBox = new QGroupBox(QString::fromStdString(s.first));
        auto *moduleLyt = new QVBoxLayout(moduleBox);
        moduleLyt->setSpacing(6);

        for (auto j : s.second)
        {
            if (j.id == -1)
                continue;

            auto *jointBox = new QGroupBox(QString::fromStdString(j.name));
            auto *form = new QFormLayout(jointBox);
            form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
            form->setContentsMargins(10, 8, 10, 8);
            form->setSpacing(6);

            auto *mode_label = new QLabel(QString::fromStdString(j.name));
            auto *mode_box = new QComboBox();

            if (s.first != "WRIST")
            {
                for (auto p : hand.control_policies)
                    mode_box->addItem(QString::fromStdString(p));
            }
            else
            {
                for (auto p : hand.wrist_control)
                    mode_box->addItem(QString::fromStdString(p));
            }

            auto *standaloneRow = new QHBoxLayout();
            auto *single = new QCheckBox("Standalone");
            single->setToolTip("Keep this joint on its own control mode instead of\n"
                               "following the global finger / wrist mode.");
            standaloneRow->addWidget(mode_box, 1);
            standaloneRow->addWidget(single);
            form->addRow("Mode:", standaloneRow);

            // --- velocity reference ---
            auto *velSlider = new QSlider(Qt::Horizontal);
            velSlider->setRange(0, 2000);
            auto *velEdit = new QLineEdit("0.00");
            velEdit->setFixedWidth(70);
            velEdit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

            auto *velRow = new QHBoxLayout();
            velRow->setSpacing(6);
            velRow->addWidget(new QLabel("0"));
            velRow->addWidget(velSlider, 1);
            velRow->addWidget(new QLabel("20 rad/s"));
            velRow->addWidget(velEdit);
            form->addRow("Velocity:", velRow);

            // --- torque reference ---
            auto *torqueSlider = new QSlider(Qt::Horizontal);
            torqueSlider->setRange(0, 1500);
            auto *torqueEdit = new QLineEdit("0");
            torqueEdit->setFixedWidth(70);
            torqueEdit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

            auto *torqueRow = new QHBoxLayout();
            torqueRow->setSpacing(6);
            torqueRow->addWidget(new QLabel("0"));
            torqueRow->addWidget(torqueSlider, 1);
            torqueRow->addWidget(new QLabel("1500 mA"));
            torqueRow->addWidget(torqueEdit);
            form->addRow("Torque:", torqueRow);

            velSlider->setEnabled(false);
            velEdit->setEnabled(false);
            torqueSlider->setEnabled(true);
            torqueEdit->setEnabled(true);
            velSlider->setValue(0);
            torqueSlider->setValue(0);

            connect(mode_box, &QComboBox::currentTextChanged,
                    this, [=]()
                    { emit mode_change(mode_label->text(), mode_box->currentText());
                      const bool mit = (mode_box->currentText() == "MIT_torque");
                      velSlider->setEnabled(mit);
                      velEdit->setEnabled(mit);
                    //   torqueSlider->setEnabled(mit);
                    //   torqueEdit->setEnabled(mit);
                    //   if (!mit)
                    //   {
                    //       velSlider->setValue(0);
                    //       torqueSlider->setValue(0);
                    //   } });
                    } );

            connect(velSlider, &QSlider::valueChanged,
                    this, [=]()
                    { velEdit->setText(QString("%1").arg((float)velSlider->value() / 100.0));
                      emit vel_ref_change(mode_label->text(), velSlider->value());
                    });
            connect(velEdit, &QLineEdit::editingFinished,
                    this, [=]()
                    { velSlider->setValue((int)(velEdit->text().toDouble() * 100.0)); });

            connect(torqueSlider, &QSlider::valueChanged,
                    this, [=]()
                    { torqueEdit->setText(QString("%1").arg(torqueSlider->value()));
                      emit torque_ref_change(mode_label->text(), torqueSlider->value());
                    });
            connect(torqueEdit, &QLineEdit::editingFinished,
                    this, [=]()
                    { torqueSlider->setValue(torqueEdit->text().toInt()); });

            moduleLyt->addWidget(jointBox);

            names.append(QString::fromStdString(j.name));
            single_checkbox.append(single);
            mode_selecters.append(mode_box);
        }

        contentLyt->addWidget(moduleBox);
    }

    contentLyt->addStretch(1);
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    root->addWidget(scroll, 1);
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
    // The hint label sits at index 0 and belongs to the persistent layout.
    while (root->count() > 1)
    {
        QLayoutItem *item = root->takeAt(root->count() - 1);
        if (!item)
            break;
        if (QLayout *rowLyt = item->layout())
        {
            QLayoutItem *child = nullptr;
            while ((child = rowLyt->takeAt(0)) != nullptr)
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
        delete item;
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
