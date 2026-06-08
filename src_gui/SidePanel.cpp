#include "SidePanel.h"
#include "ActionStore.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QScrollArea>
#include <QSlider>
#include <QLabel>
#include <QWidget>
#include <QPushButton>
#include <QToolButton>
#include <QLineEdit>
#include <QApplication>
#include <QMessageBox>
#include <QTimer>
#include <QImage>
#include <QPixmap>
#include <QComboBox>
#include <QTabWidget>
#include <cmath>

SidePanel::SidePanel(QWidget *parent)
    : QFrame(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    buildActionsPanel();
    buildSliderPanel();

    // Splitter: actions (top) + sliders (bottom)
    auto *splitter = new QSplitter(Qt::Vertical, this);
    // splitter->addWidget(img_label_);
    splitter->addWidget(actionsPanel_);
    splitter->addWidget(sliderPanel_);
    // splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    root->addWidget(splitter);

    // Track focus changes for inline name edit UX (as in your app)
    connect(qApp, &QApplication::focusChanged, this, &SidePanel::onFocusChanged);
}

void SidePanel::buildActionsPanel()
{
    actionsPanel_ = new QWidget(this);
    auto *actionRoot = new QVBoxLayout(actionsPanel_);
    actionRoot->setContentsMargins(6, 6, 6, 6);
    actionRoot->setSpacing(4);
    auto *actionList = new QWidget;
    auto *v = new QVBoxLayout(actionList);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(4);

    btnSave_ = new QPushButton("Save");
    btnDelete_ = new QPushButton("Delete");
    btnEditName_ = new QPushButton("Rename");

    auto *btnWidget = new QWidget;
    QHBoxLayout *h = new QHBoxLayout(btnWidget);
    h->addWidget(btnSave_);
    h->addWidget(btnDelete_);
    h->addWidget(btnEditName_);
    h->addStretch(1);
    h->setContentsMargins(0, 0, 0, 0);
    v->addLayout(h);

    // Header: "Actions" [collapse]
    auto *header = new QLabel("Actions:");
    v->addWidget(header);

    // Actions list container
    actionListWidget_ = new QWidget;
    actionListWidget_->setStyleSheet("background: #fff;");

    v->addWidget(actionListWidget_);
    auto *spacer = new QSpacerItem(40, 20, QSizePolicy::Fixed, QSizePolicy::Expanding);
    v->addItem(spacer);

    actionScroll_ = new QScrollArea;
    actionScroll_->setWidget(actionList);
    actionScroll_->setWidgetResizable(true);

    actionRoot->addWidget(btnWidget);
    actionRoot->addWidget(actionScroll_);

    connect(btnSave_, &QPushButton::clicked, this, &SidePanel::onSaveAction);
}

void SidePanel::updateStore(ActionStore *store)
{
    store_ = store;
}

void SidePanel::loadActions()
{
    if (!store_)
        return;
    auto *listLayout = new QVBoxLayout(actionListWidget_);
    listLayout->setContentsMargins(24, 0, 0, 0);
    listLayout->setSpacing(2);

    // Populate from store
    store_->load();
    for (int i = 0; i < store_->actionCount(); ++i)
    {
        auto *row = new QWidget;
        auto *rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        auto *addBtn = new QPushButton("+");
        addBtn->setFixedSize(25, 25);
        auto *nameBtn = new QPushButton(store_->actionName(i));
        nameBtn->setStyleSheet(
            "QPushButton { border: 1px solid #ffffff; background: #ffffff; text-align: left; padding-left: 10px; }"
            "QPushButton:focus  { border: 2px solid #000000; background: #f0f0f0; text-align: left; padding-left: 10px; }");
        nameBtn->setFixedHeight(24);
        auto *nameEdit = new QLineEdit(store_->actionName(i));
        nameEdit->hide();

        rl->addWidget(addBtn);
        rl->addWidget(nameBtn);
        rl->addWidget(nameEdit);
        listLayout->addWidget(row);

        connect(nameBtn, &QPushButton::clicked,
                this, [=]()
                { onActionNameClicked(); });

        connect(qApp, &QApplication::focusChanged, this,
                [=](QWidget *old, QWidget *now)
                {
                    if (old == nameEdit && now != nameEdit)
                    {
                        if (now != btnSave_)
                            validate(nameEdit, nameBtn);
                    }

                    if (old == nameBtn)
                    {

                        if (now == btnDelete_)
                            onDeleteAction(nameBtn);
                        else if (now == btnEditName_)
                        {
                            nameEdit->show();
                            nameEdit->setFocus();
                            nameBtn->hide();
                            nameEdit->setProperty("rename", true);
                        }
                    }
                });

        connect(nameEdit, &QLineEdit::editingFinished,
                this, [=]()
                { validate(nameEdit, nameBtn); });

        connect(addBtn, &QPushButton::clicked, this, [=]()
                { emit addAction(nameBtn->text()); });
    }
}

void SidePanel::buildSliderPanel()
{
    sliderPanel_ = new QWidget;
    auto *sliderRoot = new QVBoxLayout(sliderPanel_);
    sliderRoot->setContentsMargins(6, 6, 6, 6);
    sliderRoot->setSpacing(4);
    auto *sliderWidget = new QWidget;
    sliderVBox_ = new QVBoxLayout(sliderWidget);
    sliderVBox_->setAlignment(Qt::AlignTop);

    QPushButton *resetBtn = new QPushButton();
    resetBtn->setText("Reset");
    
    btnMode_ = new QPushButton("Mode: Follow");
    btnMode_->setToolTip("Change the joint control mode\n"
                         "    Follow mode: joints act with the sliders\n"
                         "    Setup mode: The joints act after press \"Execute\" button");

    btnExecute_ = new QPushButton("Execute");
    btnExecute_->setEnabled(false);

    auto *btnWidget = new QWidget;
    QHBoxLayout *h = new QHBoxLayout(btnWidget);
    h->addWidget(btnMode_);
    h->addWidget(btnExecute_);
    h->addWidget(resetBtn);
    h->addStretch(1);
    h->setContentsMargins(0, 0, 0, 0);

    sliderRoot->addWidget(btnWidget);

    sliderScroll_ = new QScrollArea;
    sliderScroll_->setWidget(sliderWidget);
    sliderScroll_->setWidgetResizable(true);
    sliderRoot->addWidget(sliderScroll_);

    connect(btnMode_, &QPushButton::clicked, this, &SidePanel::onToggleMode);
    connect(btnExecute_, &QPushButton::clicked, this, &SidePanel::onExecute);
    connect(resetBtn, &QPushButton::clicked, this, &SidePanel::onReset);
}

void SidePanel::loadSliderModule(QString name, float min, float max)
{
    // auto *row = new QWidget;
    auto *hl = new QHBoxLayout();
    hl->setContentsMargins(6, 2, 6, 2);
    auto *nameLbl = new QLabel(name);
    nameLabels_.append(nameLbl);
    auto *valEnter = new QLineEdit("0.00");
    valueLabels_.append(valEnter);
    auto *currValueLbl = new QLabel("  (0.00)");
    currValueLabels_.append(currValueLbl);
    hl->addWidget(nameLbl);
    hl->addStretch(1);
    hl->addWidget(valEnter);
    hl->addWidget(currValueLbl);
    sliderVBox_->addLayout(hl);

    // Slider row: [0 .. 140] degrees
    auto *sl = new QHBoxLayout();
    sl->setContentsMargins(12, 0, 12, 8);
    auto *sMin = new QLabel(QString::number(min));
    auto *slider = new QSlider(Qt::Horizontal);
    slider->setRange((int)(min * 100.0), (int)(max * 100.0));
    slider->setValue(0);
    slider->setEnabled(false);
    auto *sMax = new QLabel(QString::number(max));
    sl->addWidget(sMin);
    sl->addWidget(slider);
    sl->addWidget(sMax);
    sliders_.append(slider);
    sliderVBox_->addLayout(sl);
    valueLbls.append(std::pair(sMin, sMax));
    control_modes.append("zero_torque");

    connect(slider, &QSlider::valueChanged, this, &SidePanel::onAnySliderChanged);
    connect(valEnter, &QLineEdit::editingFinished, this, &SidePanel::onValueEditChanged);
}

void SidePanel::loadHandControllor(grip::HandSettings *hand_cfg)
{
    auto *h1 = new QHBoxLayout();
    auto *v1 = new QVBoxLayout();
    auto *titleLbl = new QLabel("Control Mode: ");
    auto *advanceBtn = new QPushButton("Advance");
    v1->addWidget(titleLbl);
    v1->addWidget(advanceBtn);
    h1->addLayout(v1);

    auto *v2 = new QVBoxLayout();
    auto *sh1 = new QHBoxLayout();
    auto fingerLbl = new QLabel("Finger: ");
    auto *modeBox = new QComboBox();
    for (auto mode : hand_cfg->control_policies)
        modeBox->addItem(mode.c_str());
    sh1->addWidget(fingerLbl);
    sh1->addWidget(modeBox);
    v2->addLayout(sh1);

    connect(modeBox, &QComboBox::currentTextChanged, this, [=]()
        { emit controlChanged(QString("finger"), modeBox->currentText()); });

    auto module_name = hand_cfg->structs.find("WRIST");
    if (module_name != hand_cfg->structs.end())
    {
        auto *sh2 = new QHBoxLayout();
        auto wristLbl = new QLabel("Wrist: ");
        auto *wristModeBox = new QComboBox();
        for (auto mode : hand_cfg->wrist_control)
            wristModeBox->addItem(mode.c_str());
        sh2->addWidget(wristLbl);
        sh2->addWidget(wristModeBox);
        v2->addLayout(sh2);
        connect(wristModeBox, &QComboBox::currentTextChanged, this, [=]()
            { emit controlChanged(QString("wrist"), wristModeBox->currentText()); });
    }
    h1->addLayout(v2);
    h1->setContentsMargins(2, 2, 2, 15);
    sliderVBox_->addLayout(h1);

    connect(advanceBtn, &QPushButton::clicked, this, [=]()
                { emit openAdvancePanel(); });
    
    for (auto s : hand_cfg->structs)
    {
        auto *mode_lable = new QLabel(s.first.c_str());
        sliderVBox_->addWidget(mode_lable);

        for (auto j : s.second)
        {
            if (j.id <= 0) continue; 
            loadSliderModule(QString(j.name.c_str()), j.route.first, j.route.second);
        }
    }
}

void SidePanel::loadActuatorControllor(grip::ActuatorSettings act_cfg)
{
    act_cfg_ = act_cfg;
    loadSliderModule(QString("Max Velocity"), 0, act_cfg_.max_vel);
    loadSliderModule(QString("Max Current"), 0, act_cfg_.max_current);

    auto *ctrlHl = new QHBoxLayout();
    auto *mode_lable = new QLabel("Control Mode: ");
    mode_box = new QComboBox();
    mode_box->addItem("Position");
    mode_box->addItem("Velocity");
    mode_box->addItem("Torque");
    mode_box->addItem("MIT");
    mode_box->setCurrentIndex(0);
    ctrlHl->addWidget(mode_lable);
    ctrlHl->addWidget(mode_box);
    sliderVBox_->addLayout(ctrlHl);

    loadSliderModule(QString("Position"), act_cfg_.route.first, act_cfg_.route.second);
    loadSliderModule(QString("-"), -1.0 * act_cfg_.max_vel, act_cfg_.max_vel);

    sliders_[3]->setEnabled(false);
    valueLabels_[3]->setEnabled(false);
    sliderVBox_->addStretch(1);

    connect(mode_box, &QComboBox::currentTextChanged, this, &SidePanel::onControlChanged);
    connect(sliders_[0], &QSlider::valueChanged, this, &SidePanel::onMaxVelocityChanged);
    connect(sliders_[1], &QSlider::valueChanged, this, &SidePanel::onMaxCurrentChanged);
}

void SidePanel::applyModeChange(grip::HandSettings *hand_cfg, QString joint_name, QString mode)
{
    size_t i = 0;
    while (i < nameLabels_.size())
    {
        if (nameLabels_[i]->text() == joint_name)
            break;
        i++;
    }
    if (i >= nameLabels_.size())
    {
        return;
    }

    for (auto cfg : hand_cfg->structs)
    {
        for (auto joint_cfg : cfg.second)
        {
            if (joint_cfg.name == joint_name.toUtf8().constData())
            {
                if (mode == "hall_position")
                {
                    sliders_[i]->setRange(joint_cfg.hall_route.first, joint_cfg.hall_route.second);
                    sliders_[i]->setEnabled(true);
                    valueLbls[i].first->setText(QString::number(joint_cfg.hall_route.first));
                    valueLbls[i].second->setText(QString::number(joint_cfg.hall_route.second));
                    control_modes[i] = "hall_position";
                    return;
                }
                else if (mode == "joint_position")
                {
                    sliders_[i]->setRange(joint_cfg.route.first * 100, joint_cfg.route.second * 100);
                    sliders_[i]->setEnabled(true);
                    valueLbls[i].first->setText(QString::number(joint_cfg.route.first));
                    valueLbls[i].second->setText(QString::number(joint_cfg.route.second));
                    control_modes[i] = "joint_position";
                    return;
                }
                else if (mode == "zero_torque")
                {
                    sliders_[i]->setEnabled(false);
                    control_modes[i] = "zero_torque";
                    return;
                }
            }
        }
    }
    
    return;
}

// ============ slots ============

QList<int> SidePanel::jointDegrees() const
{
    QList<int> out;
    out.reserve(sliders_.size());
    for (auto *s : sliders_)
        out << s->value();
    return out;
}

QString SidePanel::modeText() const { return btnMode_->text(); }

void SidePanel::onAnySliderChanged()
{
    // Update numeric labels and echo in Follow mode
    for (int i = 0; i < sliders_.size(); ++i)
    {
        if (control_modes[i] == "joint_position")
            valueLabels_[i]->setText(QString("%1").arg((float)sliders_[i]->value() / 100.0));
        else if (control_modes[i] == "hall_position")
            valueLabels_[i]->setText(QString("%1").arg((int)sliders_[i]->value()));
        else if (control_modes[i] == "zero_torque")
            valueLabels_[i]->setText(QString("0"));
    }
    if (btnMode_->text() == "Mode: Follow")
        emit valuesChanged();
}

void SidePanel::onValueEditChanged()
{
    for (int i = 0; i < valueLabels_.size(); ++i)
    {
        if (control_modes[i] == "joint_position")
            sliders_[i]->setValue((int)(atof(valueLabels_[i]->text().toUtf8().constData()) * 100.0));
        else if (control_modes[i] == "hall_position")
            sliders_[i]->setValue((int)(atoi(valueLabels_[i]->text().toUtf8().constData())));
        else if (control_modes[i] == "zero_torque")
            sliders_[i]->setValue(0);
    }
}

void SidePanel::onMaxVelocityChanged()
{
    auto curr_mode = mode_box->currentText();
    float max_vel = (float)sliders_[0]->value() / 100.0;
    if (curr_mode == "Velocity")
    {
        sliders_[2]->setRange((int)(-1 * max_vel * 100.0), (int)(max_vel * 100.0));
        valueLbls[2].first->setText(QString::number((-1 * max_vel)));
        valueLbls[2].second->setText(QString::number((max_vel)));
    }
    else if (curr_mode == "MIT")
    {
        sliders_[3]->setRange((int)(-1 * max_vel * 100.0), (int)(max_vel * 100.0));
        valueLbls[3].first->setText(QString::number((-1 * max_vel)));
        valueLbls[3].second->setText(QString::number((max_vel)));
    }
}

void SidePanel::onMaxCurrentChanged()
{
    auto curr_mode = mode_box->currentText();
    float max_current = (float)sliders_[1]->value() / 100.0;
    float max_torque = 3 / 2 * max_current * act_cfg_.psi_f * (float)act_cfg_.n_pole_pairs;
    if (curr_mode == "Torque")
    {
        sliders_[2]->setRange((int)(-1 * max_torque * 100.0), (int)(max_torque * 100.0));
        valueLbls[2].first->setText(QString::number((-1 * max_torque)));
        valueLbls[2].second->setText(QString::number((max_torque)));
    }
}

void SidePanel::onToggleMode()
{
    const bool toSetup = (btnMode_->text() == "Mode: Follow");
    btnMode_->setText(toSetup ? "Mode: Setup" : "Mode: Follow");
    btnExecute_->setEnabled(toSetup);
    emit notify(btnMode_->text());
}

void SidePanel::onExecute()
{
    emit committed();
}

void SidePanel::onReset()
{
    for (auto *s : sliders_)
        s->setValue(0);
}

void SidePanel::onSaveAction()
{
    if (!store_)
        return;
    // Toggle add-row at end
    auto *layout = qobject_cast<QVBoxLayout *>(actionListWidget_->layout());
    if (addingRowIndex_ == -1)
    {
        auto *row = new QWidget;
        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 0, 0, 0);
        auto *addBtn = new QPushButton("+");
        addBtn->setFixedSize(25, 25);
        auto *nameBtn = new QPushButton;
        nameBtn->setStyleSheet(
            "QPushButton { border: 1px solid #ffffff; background: #ffffff; text-align: left; padding-left: 10px; }"
            "QPushButton:focus  { border: 2px solid #000000; background: #f0f0f0; text-align: left; padding-left: 10px; }");
        nameBtn->hide();
        auto *nameEdit = new QLineEdit;
        hl->addWidget(addBtn);
        hl->addWidget(nameBtn);
        hl->addWidget(nameEdit);
        layout->addWidget(row);
        addingRowIndex_ = layout->count() - 1;
        nameEdit->setFocus();
        connect(nameBtn, &QPushButton::clicked,
                this, [=]()
                { onActionNameClicked(); });

        connect(qApp, &QApplication::focusChanged, this,
                [=](QWidget *old, QWidget *now)
                {
                    if (old == nameEdit && now != nameEdit)
                    {
                        if (now != btnSave_)
                            validate(nameEdit, nameBtn);
                    }

                    if (old == nameBtn)
                    {
                        if (now == btnDelete_)
                            onDeleteAction(nameBtn);
                        else if (now == btnEditName_)
                        {
                            nameEdit->show();
                            nameEdit->setFocus();
                            nameBtn->hide();
                            nameEdit->setProperty("rename", true);
                        }
                    }
                });

        connect(nameEdit, &QLineEdit::editingFinished,
                this, [=]()
                { validate(nameEdit, nameBtn); });
        connect(addBtn, &QPushButton::clicked, this, [=]()
                { emit addAction(nameBtn->text()); });
        btnSave_->setText("Cancel");
    }
    else
    {
        // cancel
        QLayoutItem *item = layout->takeAt(addingRowIndex_);
        delete item->widget();
        delete item;
        addingRowIndex_ = -1;
        btnSave_->setText("Save");
    }
}

void SidePanel::onActionNameClicked()
{
    if (!store_)
        return;
    // Find which QPushButton sent this, then apply action to sliders
    auto *btn = qobject_cast<QPushButton *>(sender());
    if (!btn)
        return;
    int idx = store_->findActionByName(btn->text());
    if (idx >= 0)
        applyActionToSliders(idx);
}

void SidePanel::onFocusChanged(QWidget *oldW, QWidget *nowW)
{
    Q_UNUSED(oldW)
    Q_UNUSED(nowW)
    // no-op here; inline editing is handled on editingFinished
}

void SidePanel::onDeleteAction(QPushButton *btn)
{
    if (!store_)
        return;
    QMessageBox msgBox;
    msgBox.setTextFormat(Qt::MarkdownText);
    msgBox.setWindowTitle("Delete Action");
    msgBox.setText(QString("#### Are you sure to delete action %1?").arg(btn->text()));
    msgBox.setInformativeText("This action will also be removed if it has been contained by any action set.");
    msgBox.setIcon(QMessageBox::Warning);
    auto *confirmBtn = msgBox.addButton(QMessageBox::Ok);
    auto *cancelBtn = msgBox.addButton(QMessageBox::Cancel);

    auto actionSets = store_->findActionInSet(btn->text());
    if (actionSets.size() > 0)
    {
        auto msg = QString("Following action set contains action %1:\n").arg(btn->text());
        for (auto &action : actionSets)
            msg.append(QString("  - %1\n").arg(action));
        msgBox.setDetailedText(msg);
    }
    else
        msgBox.setDetailedText(QString("No action set contains action %1").arg(btn->text()));

    msgBox.exec();

    if (msgBox.clickedButton() == confirmBtn)
    {
        auto *layout = qobject_cast<QVBoxLayout *>(actionListWidget_->layout());
        int idx = store_->findActionByName(btn->text());
        QLayoutItem *item = layout->takeAt(idx);
        auto name = btn->text();

        if (item)
        {
            // qDebug() << "Deleting";
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

        if (idx < store_->actionCount())
        {
            store_->removeAction(idx);
            persistStore();
        }
        if (actionSets.size() > 0)
        {
            store_->removeActionFromSet(name);
            emit actionStoreUpdate();
        }
    }
    else
    {
        return;
    }
}

// -------- helpers --------
void SidePanel::applyActionToSliders(int actionIndex)
{
    if (!store_)
        return;
    auto vals = store_->actionJoints(actionIndex);
    for (int i = 0; i < sliders_.size() && i < vals.size(); ++i)
    {
        float min = valueLbls[i].first->text().toFloat();
        float max = valueLbls[i].second->text().toFloat();
        float v = std::clamp(float(vals[i]), min, max);
        sliders_[i]->setValue(v);
    }
    emit valuesChanged();
}

void SidePanel::persistStore()
{
    if (!store_)
        return;
    store_->save();
}

bool SidePanel::validateNewActionName(const QString &n, const QString &o) const
{
    if (!store_)
        return false;
    if (n.trimmed().isEmpty())
        return false;

    int i = -1;
    if (o.trimmed().isEmpty() || o != n)
    {
        i = store_->findActionByName(n);
    }
    return i == -1;
}

void SidePanel::validate(QLineEdit *edit, QPushButton *btn)
{
    if (!store_)
        return;
    if (edit->property("validating").toBool())
        return;
    edit->setProperty("validating", true);

    if (validateNewActionName(edit->text(), btn->text()))
    {
        QMessageBox msgBox;
        msgBox.setTextFormat(Qt::MarkdownText);
        msgBox.setWindowTitle("Rename Action");
        msgBox.setText(QString("#### Are you sure to rename action %1 to %2?").arg(btn->text(), edit->text()));
        msgBox.setInformativeText(QString("This action will also be renamed if it has been contained by any action set.\n"
            "Or you can remove all action with name %1 from action sets.").arg(btn->text()));
        msgBox.setIcon(QMessageBox::Warning);
        auto *renameBtn = msgBox.addButton(QMessageBox::Ok);
        auto *cancelBtn = msgBox.addButton(QMessageBox::Cancel);
        auto *deleteBtn = msgBox.addButton("Delete All", QMessageBox::ActionRole);

        auto actionSets = store_->findActionInSet(btn->text());
        if (actionSets.size() > 0)
        {
            auto msg = QString("Following action set contains action %1:\n").arg(btn->text());
            for (auto &action : actionSets)
                msg.append(QString("  - %1\n").arg(action));
            msgBox.setDetailedText(msg);
        }
        else
            msgBox.setDetailedText(QString("No action set contains action %1").arg(btn->text()));
        
        if (edit->property("rename").toBool() && edit->text() != btn->text())
        {
            msgBox.exec();

            if (msgBox.clickedButton() == deleteBtn)
            {
                if (actionSets.size() > 0)
                    store_->removeActionFromSet(btn->text());
            }
            else if (msgBox.clickedButton() == renameBtn)
            {
                
                if (actionSets.size() > 0)
                    store_->renameActionInSet(btn->text(), edit->text());
            }
            else
            {
                return;
            }
        }
        
        QString aName = edit->text();

        if (btn->text().trimmed().isEmpty())
        {
            QVector<double> vals;
            vals.reserve(sliders_.size());
            for (auto *s : sliders_)
                vals.push_back(double(s->value()));

            btnSave_->setText("Save");
            addingRowIndex_ = -1;
            store_->addAction(aName, vals);
        }
        else
        {
            int idx = store_->findActionByName(btn->text());
            store_->setActionName(idx, aName);
        }

        btn->setText(aName);
        btn->show();
        edit->hide();
        emit actionStoreUpdate();

        persistStore();
    }
    else
    {
        QMessageBox::warning(this, "Invalid name", "Name must be unique and non-empty.");
        edit->setFocus();
        edit->selectAll();
    }

    QTimer::singleShot(0, edit, [edit]
                       { edit->setProperty("validating", false); });
    QTimer::singleShot(0, edit, [edit]
                       { edit->setProperty("rename", false); });
}

void SidePanel::modeChange(const QString &mode)
{
    if (mode == "Start Camera")
    {
        for (auto s : sliders_)
            s->setEnabled(false);
        for (auto e : valueLabels_)
            e->setReadOnly(true);
    }
    else
    {
        for (auto s : sliders_)
            s->setEnabled(true);
        for (auto e : valueLabels_)
            e->setReadOnly(false);
    }
}

void SidePanel::applyCameraData(std::unordered_map<std::string, float> &data, grip::HandSettings *hand_cfg)
{
    for (const auto &[name, value] : data)
    {
        for (int i = 0; i < nameLabels_.size(); ++i)
        {
            if (nameLabels_[i]->text().toUtf8().constData() == name)
            {
                float min = valueLbls[i].first->text().toFloat();
                float max = valueLbls[i].second->text().toFloat();
                float v = std::clamp(float(value), float(min), float(max));

                if (control_modes[i] == "hall_position")
                {
                    for (auto [key, vec] : hand_cfg->structs)
                    {
                        for (auto joint : vec)
                        {
                            if (joint.name == name)
                                v = v / (joint.route.second - joint.route.first) * 
                                    (joint.hall_route.second - joint.hall_route.first) / 100;
                        }
                    }
                }
                else if (control_modes[i] == "zero_torque")
                    continue;
                
                sliders_[i]->setValue(v * 100);
            }
        }
    }
}

void SidePanel::updateJoints(const std::unordered_map<std::string, float> state)
{
    // for (auto l : currValueLabels_) {
    //     l->setText("  (%1)".arg(sliders_[i]->value()));
    // }
    // currValueLabels_[3]->setText(QString("  (%1)").arg((index / float(M_PI) * 180), 0, 'f', 2));
    for (auto &[name, val]: state)
    {
        int idx = 0;
        for (auto sName : nameLabels_)
        {
            if (sName->text().toUtf8().constData() == name)
            {
                currValueLabels_[idx]->setText(QString("  (%1)").arg(float(val), 0, 'f', 2));
                continue;
            }
            ++idx;
        }
    }
}

void SidePanel::onControlChanged()
{
    auto curr_mode = mode_box->currentText();
    if (curr_mode != "MIT")
    {
        nameLabels_[2]->setText(curr_mode);
        sliders_[3]->setEnabled(false);
        valueLabels_[3]->setEnabled(false);
        nameLabels_[3]->setText("-");
        valueLabels_[2]->setText(QString("0"));
        valueLabels_[3]->setText(QString("0"));
        sliders_[2]->setValue(0);
        sliders_[3]->setValue(0);

        if (curr_mode == "Position")
        {
            sliders_[2]->setRange((int)(act_cfg_.route.first * 100.0), (int)(act_cfg_.route.second * 100.0));
            valueLbls[2].first->setText(QString::number((act_cfg_.route.first)));
            valueLbls[2].second->setText(QString::number((act_cfg_.route.second)));
        }
        else if (curr_mode == "Velocity")
        {
            float max_vel = (float)sliders_[0]->value() / 100.0;
            sliders_[2]->setRange((int)(-1 * max_vel * 100.0), (int)(max_vel * 100.0));
            valueLbls[2].first->setText(QString::number((-1 * max_vel)));
            valueLbls[2].second->setText(QString::number((max_vel)));
        }
        else if (curr_mode == "Torque")
        {
            float max_current = (float)sliders_[1]->value() / 100.0;
            float max_torque = 3 / 2 * max_current * act_cfg_.psi_f * (float)act_cfg_.n_pole_pairs;
            sliders_[2]->setRange((int)(-1 * max_torque * 100.0), (int)(max_torque * 100.0));
            valueLbls[2].first->setText(QString::number((-1 * max_torque)));
            valueLbls[2].second->setText(QString::number((max_torque)));
        }
    }
    else
    {
        nameLabels_[2]->setText("Position");
        sliders_[3]->setEnabled(true);
        valueLabels_[3]->setEnabled(true);
        nameLabels_[3]->setText("Velocity");
        valueLabels_[2]->setText(QString("0"));
        valueLabels_[3]->setText(QString("0"));
        sliders_[2]->setValue(0);
        sliders_[3]->setValue(0);
        float max_vel = (float)sliders_[0]->value() / 100.0;
        sliders_[2]->setRange((int)(act_cfg_.route.first * 100.0), (int)(act_cfg_.route.second * 100.0));
        sliders_[3]->setRange((int)(-1 * max_vel * 100.0), (int)(max_vel * 100.0));
        valueLbls[2].first->setText(QString::number((act_cfg_.route.first)));
        valueLbls[2].second->setText(QString::number((act_cfg_.route.second)));
        valueLbls[3].first->setText(QString::number((-1 * max_vel)));
        valueLbls[3].second->setText(QString::number((max_vel)));
    }
}

std::string SidePanel::get_control_mode()
{
    return mode_box->currentText().toUtf8().constData();
}

void SidePanel::removeAll()
{
    sliders_.clear();
    valueLabels_.clear();
    currValueLabels_.clear();
    nameLabels_.clear();
    valueLbls.clear();

    QLayoutItem *item;
    while ((item = sliderVBox_->takeAt(0)) != nullptr)
    {
        deleteLayout(item);
    }

    auto *layout = qobject_cast<QVBoxLayout *>(actionListWidget_->layout());
    while ((item = layout->takeAt(0)) != nullptr)
    {
        deleteLayout(item);
    }
    delete layout;

    if (!store_)
        return;
    delete store_;
}

void SidePanel::deleteLayout(QLayoutItem *item)
{
    if (QLayout *lyt = item->layout())
    {
        QLayoutItem *child;
        while ((child = lyt->takeAt(0)))
        {
            deleteLayout(child);
        }
        delete lyt;
    }
    else if (item->widget())
    {
        delete item->widget();
    }
}

void SidePanel::applyActionNameToSliders(const QString &name)
{
    if (!store_)
        return;
    int idx = store_->findActionByName(name);
    if (idx >= 0)
        applyActionToSliders(idx);
}

QStringList SidePanel::getControlModes()
{
    return control_modes;
}
