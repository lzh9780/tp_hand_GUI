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
#include <cmath>

static constexpr int kDegMin = -50; // degrees
static constexpr int kDegMax = 150;

SidePanel::SidePanel(QWidget *parent)
    : QFrame(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    buildControlRow();
    buildActionsPanel();
    buildSliderPanel();

    // Splitter: actions (top) + sliders (bottom)
    auto *splitter = new QSplitter(Qt::Vertical, this);
    // splitter->addWidget(img_label_);
    splitter->addWidget(actionScroll_);
    splitter->addWidget(sliderScroll_);
    // splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    root->addWidget(controlRow_);
    root->addWidget(splitter);

    // Track focus changes for inline name edit UX (as in your app)
    connect(qApp, &QApplication::focusChanged, this, &SidePanel::onFocusChanged);
}

void SidePanel::buildControlRow()
{
    controlRow_ = new QWidget(this);
    auto *h = new QHBoxLayout(controlRow_);
    h->setContentsMargins(8, 6, 8, 6);

    btnSave_ = new QPushButton("Save");
    btnDelete_ = new QPushButton("Delete");

    // QPushButton *recordBtn = new QPushButton();
    // recordBtn->setText("Record");
    // recordBtn->setEnabled(false);

    // QPushButton *saveRecordBtn = new QPushButton();
    // saveRecordBtn->setText("Save Set");
    // saveRecordBtn->setEnabled(false);

    QPushButton *editBtn = new QPushButton();
    editBtn->setText("Edit");
    editBtn->setEnabled(false);

    btnEditName_ = new QPushButton("Rename");

    btnMode_ = new QPushButton("Mode: Follow");
    btnMode_->setToolTip("Change the joint control mode\n"
                         "    Follow mode: joints act with the sliders\n"
                         "    Setup mode: The joints act after press \"Execute\" button");

    btnExecute_ = new QPushButton("Execute");
    btnExecute_->setEnabled(false);

    QVBoxLayout *btnCol1 = new QVBoxLayout();
    btnCol1->addWidget(btnSave_);
    // btnCol1->addWidget(btnDelete_);
    btnCol1->addWidget(editBtn);
    btnCol1->addWidget(btnMode_);
    // saveLine->setSpacing(0);

    QVBoxLayout *btnCol2 = new QVBoxLayout();
    // btnCol2->addWidget(saveRecordBtn);
    // btnCol2->addWidget(recordBtn);
    btnCol2->addWidget(btnDelete_);
    btnCol2->addWidget(btnEditName_);
    btnCol2->addWidget(btnExecute_);

    h->addLayout(btnCol1);
    h->addLayout(btnCol2);

    controlRow_->setLayout(h);

    connect(btnMode_, &QPushButton::clicked, this, &SidePanel::onToggleMode);
    connect(btnExecute_, &QPushButton::clicked, this, &SidePanel::onExecute);
    connect(btnSave_, &QPushButton::clicked, this, &SidePanel::onSaveAction);
    // connect(btnDelete_, &QPushButton::clicked, this, &SidePanel::onDeleteAction);
}

void SidePanel::buildActionsPanel()
{
    actionsPanel_ = new QWidget(this);
    auto *v = new QVBoxLayout(actionsPanel_);
    v->setContentsMargins(6, 6, 6, 6);
    v->setSpacing(4);

    // // Header: "Actions" [collapse]
    // auto *header = new QWidget;
    // auto *hl = new QHBoxLayout(header);
    // hl->setContentsMargins(0, 0, 0, 0);
    // btnCollapseActions_ = new QToolButton;
    // btnCollapseActions_->setArrowType(Qt::UpArrow);
    // btnCollapseActions_->setFixedSize(20, 20);
    // connect(btnCollapseActions_, &QToolButton::clicked, this, &SidePanel::onToggleActions);
    // hl->addWidget(btnCollapseActions_);
    // hl->addWidget(new QLabel("Actions:"));
    auto *header = new QLabel("Actions:");
    v->addWidget(header);

    // Actions list container
    actionListWidget_ = new QWidget;
    actionListWidget_->setStyleSheet("background: #fff;");

    // Header: "Action Sets" [collapse] (kept for UI parity)
    // auto *header2 = new QWidget;
    // auto *hl2 = new QHBoxLayout(header2);
    // hl2->setContentsMargins(0, 0, 0, 0);
    // btnCollapseSets_ = new QToolButton;
    // btnCollapseSets_->setArrowType(Qt::UpArrow);
    // btnCollapseSets_->setFixedSize(20, 20);
    // connect(btnCollapseSets_, &QToolButton::clicked, this, &SidePanel::onToggleActionSets);
    // hl2->addWidget(btnCollapseSets_);
    // hl2->addWidget(new QLabel("Action Sets:"));
    v->addWidget(actionListWidget_);
    // v->addWidget(header2);
    // actionSetPanel_ = new QWidget;
    // actionSetPanel_->setMinimumHeight(80);
    // actionSetPanel_->setStyleSheet("background: #fff;");
    // v->addWidget(actionSetPanel_);
    auto *spacer = new QSpacerItem(40, 20, QSizePolicy::Fixed, QSizePolicy::Expanding);
    v->addItem(spacer);

    actionScroll_ = new QScrollArea;
    actionScroll_->setWidget(actionsPanel_);
    actionScroll_->setWidgetResizable(true);
}

void SidePanel::updateStore(ActionStore *store)
{
    store_ = store;
}

void SidePanel::loadActions()
{
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
                        {
                            vialdate(nameEdit, nameBtn);
                        }
                    }

                    if (old == nameBtn)
                    {

                        if (now == btnDelete_)
                        {
                            onDeleteAction(nameBtn);
                        }
                        else if (now == btnEditName_)
                        {
                            nameEdit->show();
                            nameEdit->setFocus();
                            nameBtn->hide();
                        }
                    }
                });

        connect(nameEdit, &QLineEdit::editingFinished,
                this, [=]()
                { vialdate(nameEdit, nameBtn); });

        connect(addBtn, &QPushButton::clicked, this, [=]()
                { emit addAction(nameBtn->text()); });
    }
}

void SidePanel::buildSliderPanel()
{
    sliderPanel_ = new QWidget;
    sliderVBox_ = new QVBoxLayout(sliderPanel_);
    sliderVBox_->setAlignment(Qt::AlignTop);

    sliderScroll_ = new QScrollArea;
    sliderScroll_->setWidget(sliderPanel_);
    sliderScroll_->setWidgetResizable(true);
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
    auto *sMax = new QLabel(QString::number(max));
    sl->addWidget(sMin);
    sl->addWidget(slider);
    sl->addWidget(sMax);
    sliders_.append(slider);
    sliderVBox_->addLayout(sl);
    valueLbls.append(std::pair(sMin, sMax));

    connect(slider, &QSlider::valueChanged, this, &SidePanel::onAnySliderChanged);
    connect(valEnter, &QLineEdit::editingFinished, this, &SidePanel::onValueEditChanged);
}

void SidePanel::loadHandControllor(grip::HandSettings hand_cfg) {
    for (auto s : hand_cfg.structs) {
        auto *ctrlHl = new QHBoxLayout();
        auto *mode_lable = new QLabel(s.first.c_str()); 
        mode_box = new QComboBox();

        if (s.first != "WRIST") {
            for (auto p : hand_cfg.control_policies) {
                mode_box->addItem(p.c_str());
            }
        } else {
            for (auto p : hand_cfg.wrist_control) {
                mode_box->addItem(p.c_str());
            }
        }
        ctrlHl->addWidget(mode_lable);
        ctrlHl->addWidget(mode_box);
        sliderVBox_->addLayout(ctrlHl);

        for (auto d : s.second.dofs) {
            loadSliderModule(QString(d.name.c_str()), d.route.first, d.route.second);
        }

        connect(mode_box, &QComboBox::currentTextChanged,
                this, [=]()
                { emit mode_change(mode_lable->text(), mode_box->currentText()); });
    }
}

void SidePanel::loadActuatorControllor(grip::ActuatorSettings act_cfg) {
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
        valueLabels_[i]->setText(QString("%1").arg((float)sliders_[i]->value() / 100.0));
    if (btnMode_->text() == "Mode: Follow")
        emit valuesChanged();
}

void SidePanel::onValueEditChanged() 
{
    for (int i = 0; i < valueLabels_.size(); ++i) {
        sliders_[i]->setValue((int)(atof(valueLabels_[i]->text().toUtf8().constData()) * 100.0));
    }
}

void SidePanel::onMaxVelocityChanged()
{
    auto curr_mode = mode_box->currentText();
    float max_vel = (float)sliders_[0]->value() / 100.0;
    if (curr_mode == "Velocity") {
        sliders_[2]->setRange((int)(-1 * max_vel * 100.0), (int)(max_vel * 100.0));
        valueLbls[2].first->setText(QString::number((-1 * max_vel)));
        valueLbls[2].second->setText(QString::number((max_vel)));
    } else if (curr_mode == "MIT") {
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
    if (curr_mode == "Torque") {
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

void SidePanel::onSaveAction()
{
    // Toggle add-row at end
    auto *layout = qobject_cast<QVBoxLayout *>(actionListWidget_->layout());
    if (addingRowIndex_ == -1)
    {
        auto *row = new QWidget;
        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 0, 0, 0);
        auto *addBtn = new QPushButton();
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
                        {
                            vialdate(nameEdit, nameBtn);
                        }
                    }

                    if (old == nameBtn)
                    {

                        if (now == btnDelete_)
                        {
                            onDeleteAction(nameBtn);
                        }
                        else if (now == btnEditName_)
                        {
                            nameEdit->show();
                            nameEdit->setFocus();
                            nameBtn->hide();
                        }
                    }
                });

        connect(nameEdit, &QLineEdit::editingFinished,
                this, [=]()
                { vialdate(nameEdit, nameBtn); });
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

// void SidePanel::onToggleActions()
// {
//     bool vis = actionListWidget_->isVisible();
//     actionListWidget_->setVisible(!vis);
//     btnCollapseActions_->setArrowType(vis ? Qt::DownArrow : Qt::UpArrow);
// }

// void SidePanel::onToggleActionSets()
// {
//     bool vis = actionSetPanel_->isVisible();
//     actionSetPanel_->setVisible(!vis);
//     btnCollapseSets_->setArrowType(vis ? Qt::DownArrow : Qt::UpArrow);
// }

void SidePanel::onDeleteAction(QPushButton *btn)
{
    auto *layout = qobject_cast<QVBoxLayout *>(actionListWidget_->layout());
    int idx = store_->findActionByName(btn->text());
    QLayoutItem *item = layout->takeAt(idx);

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
        // qDebug() << "Deleting" << idx;
        store_->removeAction(idx);
    }
}

// -------- helpers --------
void SidePanel::applyActionToSliders(int actionIndex)
{
    auto vals = store_->actionJoints(actionIndex);
    for (int i = 0; i < sliders_.size() && i < vals.size(); ++i)
    {
        int v = std::clamp(int(vals[i]), kDegMin, kDegMax);
        sliders_[i]->setValue(v);
    }
    emit valuesChanged();
}

void SidePanel::persistStore() { store_->save(); }

bool SidePanel::validateNewActionName(const QString &n, const QString &o) const
{
    if (n.trimmed().isEmpty())
        return false;

    int i = -1;
    if (o.trimmed().isEmpty() || o != n)
    {
        i = store_->findActionByName(n);
    }
    return i == -1;
}

void SidePanel::vialdate(QLineEdit *edit, QPushButton *btn)
{
    if (edit->property("vaildating").toBool())
        return;
    edit->setProperty("vaildating", true);

    // if (validateNewActionName(edit->text(), idx)) {
    if (validateNewActionName(edit->text(), btn->text()))
    {
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

        persistStore();
    }
    else
    {
        QMessageBox::warning(this, "Invalid name", "Name must be unique and non-empty.");
        edit->setFocus();
        edit->selectAll();
    }

    QTimer::singleShot(0, edit, [edit]
                       { edit->setProperty("vaildating", false); });
}

void SidePanel::modeChange(const QString &mode)
{
    /** TODO: Complete mode change */
    if (mode == "Start Camera")
    {
        for (auto s : sliders_)
            s->setEnabled(false);
        for (auto e : valueLabels_)
            e->setEnabled(false);
    }
    else
    {
        for (auto s : sliders_)
            s->setEnabled(true);
        for (auto e : valueLabels_)
            e->setEnabled(true);
    }
}

void SidePanel::applyCameraData(const QVector<double> &data)
{
    // auto vals = store_->actionJoints(actionIndex);
    for (int i = 0; i < sliders_.size() && i < data.size(); ++i)
    {
        float f = data[i];
        float v = std::clamp(float(f), float(kDegMin), float(kDegMax));
        sliders_[i]->setValue(v * 100);
    }
    emit valuesChanged();
}

void SidePanel::updateJoints(const float thumb, const float index, const float middle, const float ring, const float pinky)
{
    // for (auto l : currValueLabels_) {
    //     l->setText("  (%1)".arg(sliders_[i]->value()));
    // }
    currValueLabels_[3]->setText(QString("  (%1)").arg((index / float(M_PI) * 180), 0, 'f', 2));
}

void SidePanel::onControlChanged()
{
    auto curr_mode = mode_box->currentText();
    if (curr_mode != "MIT") {
        nameLabels_[2]->setText(curr_mode);
        sliders_[3]->setEnabled(false);
        valueLabels_[3]->setEnabled(false);
        nameLabels_[3]->setText("-");
        valueLabels_[2]->setText(QString("0"));
        valueLabels_[3]->setText(QString("0"));
        sliders_[2]->setValue(0);
        sliders_[3]->setValue(0);

        if (curr_mode == "Position") {
            sliders_[2]->setRange((int)(act_cfg_.route.first * 100.0), (int)(act_cfg_.route.second * 100.0));
            valueLbls[2].first->setText(QString::number((act_cfg_.route.first)));
            valueLbls[2].second->setText(QString::number((act_cfg_.route.second)));
        } else if (curr_mode == "Velocity") {
            float max_vel = (float)sliders_[0]->value() / 100.0;
            sliders_[2]->setRange((int)(-1 * max_vel * 100.0), (int)(max_vel * 100.0));
            valueLbls[2].first->setText(QString::number((-1 * max_vel)));
            valueLbls[2].second->setText(QString::number((max_vel)));
        } else if (curr_mode == "Torque") {
            float max_current = (float)sliders_[1]->value() / 100.0;
            float max_torque = 3 / 2 * max_current * act_cfg_.psi_f * (float)act_cfg_.n_pole_pairs;
            sliders_[2]->setRange((int)(-1 * max_torque * 100.0), (int)(max_torque * 100.0));
            valueLbls[2].first->setText(QString::number((-1 * max_torque)));
            valueLbls[2].second->setText(QString::number((max_torque)));
        }
    } else {
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

std::string SidePanel::get_control_mode() {
    return mode_box->currentText().toUtf8().constData();
}

void SidePanel::removeAll() {
    sliders_.clear();
    valueLabels_.clear();
    currValueLabels_.clear();
    nameLabels_.clear();
    valueLbls.clear();
    
    QLayoutItem *item;
    while ((item = sliderVBox_->takeAt(0)) != nullptr)
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

    auto *layout = qobject_cast<QVBoxLayout *>(actionListWidget_->layout());
    while ((item = layout->takeAt(0)) != nullptr)
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
    delete layout;

    delete store_;
}

void SidePanel::applyActionNameToSliders(const QString &name)
{
    int idx = store_->findActionByName(name);
    if (idx >= 0) applyActionToSliders(idx);
}
