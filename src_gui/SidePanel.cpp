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

static constexpr int kDegMin = 0; // degrees
static constexpr int kDegMax = 140;

SidePanel::SidePanel(QWidget *parent,
                     const QStringList &jointNames,
                     ActionStore *store)
    : QFrame(parent), store_(store), jointCount_(jointNames.size())
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    buildControlRow();
    buildActionsPanel();
    buildSliderPanel(jointNames);

    // Splitter: actions (top) + sliders (bottom)
    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(actionsPanel_);
    splitter->addWidget(sliderScroll_);
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
    btnEditName_ = new QPushButton("Edit Name");
    btnMode_ = new QPushButton("Mode: Follow");
    btnExecute_ = new QPushButton("Execute");
    btnExecute_->setEnabled(false);

    h->addWidget(btnSave_);
    h->addWidget(btnDelete_);
    h->addWidget(btnEditName_);
    h->addStretch(1);
    h->addWidget(btnMode_);
    h->addWidget(btnExecute_);

    connect(btnMode_, &QPushButton::clicked, this, &SidePanel::onToggleMode);
    connect(btnExecute_, &QPushButton::clicked, this, &SidePanel::onExecute);
    connect(btnSave_, &QPushButton::clicked, this, &SidePanel::onSaveAction);
    connect(btnDelete_, &QPushButton::clicked, this, &SidePanel::onDeleteAction);
}

void SidePanel::buildActionsPanel()
{
    actionsPanel_ = new QWidget(this);
    auto *v = new QVBoxLayout(actionsPanel_);
    v->setContentsMargins(6, 6, 6, 6);
    v->setSpacing(4);

    // Header: "Actions" [collapse]
    auto *header = new QWidget;
    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(0, 0, 0, 0);
    btnCollapseActions_ = new QToolButton;
    btnCollapseActions_->setArrowType(Qt::UpArrow);
    btnCollapseActions_->setFixedSize(20, 20);
    connect(btnCollapseActions_, &QToolButton::clicked, this, &SidePanel::onToggleActions);
    hl->addWidget(btnCollapseActions_);
    hl->addWidget(new QLabel("Actions:"));
    v->addWidget(header);

    // Actions list container
    actionListWidget_ = new QWidget;
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
        auto *nameBtn = new QPushButton(store_->actionName(i));
        nameBtn->setStyleSheet(
            "QPushButton { border: 1px solid #ffffff; background: #ffffff; text-align: left; padding-left: 10px; }"
            "QPushButton:focus  { border: 2px solid #000000; background: #f0f0f0; text-align: left; padding-left: 10px; }");
        nameBtn->setFixedHeight(24);
        auto *nameEdit = new QLineEdit(store_->actionName(i));
        nameEdit->hide();

        rl->addWidget(nameBtn);
        rl->addWidget(nameEdit);
        listLayout->addWidget(row);

        // Clicking the name applies the stored joint values to sliders
        connect(nameBtn, &QPushButton::clicked, this, &SidePanel::onActionNameClicked);

        // Inline rename UX (Edit Name button brings focus to the QLineEdit)
        connect(btnEditName_, &QPushButton::clicked, this, [=]
                { nameEdit->show(); nameEdit->setFocus(); nameBtn->hide(); });
        connect(nameEdit, &QLineEdit::editingFinished, this, &SidePanel::onNameEditingFinished);
    }

    // Header: "Action Sets" [collapse] (kept for UI parity)
    auto *header2 = new QWidget;
    auto *hl2 = new QHBoxLayout(header2);
    hl2->setContentsMargins(0, 0, 0, 0);
    btnCollapseSets_ = new QToolButton;
    btnCollapseSets_->setArrowType(Qt::UpArrow);
    btnCollapseSets_->setFixedSize(20, 20);
    connect(btnCollapseSets_, &QToolButton::clicked, this, &SidePanel::onToggleActionSets);
    hl2->addWidget(btnCollapseSets_);
    hl2->addWidget(new QLabel("Action Sets:"));
    v->addWidget(actionListWidget_);
    v->addWidget(header2);
    actionSetPanel_ = new QWidget;
    actionSetPanel_->setMinimumHeight(80);
    actionSetPanel_->setStyleSheet("background: #fff;");
    v->addWidget(actionSetPanel_);
}

void SidePanel::buildSliderPanel(const QStringList &jointNames)
{
    sliderPanel_ = new QWidget;
    sliderVBox_ = new QVBoxLayout(sliderPanel_);
    sliderVBox_->setAlignment(Qt::AlignTop);

    for (const auto &name : jointNames)
    {
        // Header row: label + dynamic numeric readout
        auto *row = new QWidget;
        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(6, 2, 6, 2);
        auto *nameLbl = new QLabel(name);
        auto *valueLbl = new QLabel("  0");
        valueLabels_.append(valueLbl);
        hl->addWidget(nameLbl);
        hl->addStretch(1);
        hl->addWidget(valueLbl);
        sliderVBox_->addWidget(row);

        // Slider row: [0 .. 140] degrees
        auto *sRow = new QWidget;
        auto *sl = new QHBoxLayout(sRow);
        sl->setContentsMargins(12, 0, 12, 8);
        auto *sMin = new QLabel(QString::number(kDegMin));
        auto *slider = new QSlider(Qt::Horizontal);
        slider->setRange(kDegMin, kDegMax);
        slider->setValue(0);
        auto *sMax = new QLabel(QString::number(kDegMax));
        sl->addWidget(sMin);
        sl->addWidget(slider);
        sl->addWidget(sMax);
        sliders_.append(slider);
        sliderVBox_->addWidget(sRow);

        connect(slider, &QSlider::valueChanged, this, &SidePanel::onAnySliderChanged);
    }
    sliderVBox_->addStretch(1);

    sliderScroll_ = new QScrollArea;
    sliderScroll_->setWidget(sliderPanel_);
    sliderScroll_->setWidgetResizable(true);
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
        valueLabels_[i]->setText(QString("  %1").arg(sliders_[i]->value()));
    if (btnMode_->text() == "Mode: Follow")
        emit valuesChanged();
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
        auto *nameBtn = new QPushButton;
        nameBtn->hide();
        auto *nameEdit = new QLineEdit;
        hl->addWidget(nameBtn);
        hl->addWidget(nameEdit);
        layout->addWidget(row);
        addingRowIndex_ = layout->count() - 1;
        nameEdit->setFocus();
        connect(nameEdit, &QLineEdit::editingFinished, this, &SidePanel::onNameEditingFinished);
        connect(nameBtn, &QPushButton::clicked, this, &SidePanel::onActionNameClicked);
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

void SidePanel::onNameEditingFinished()
{
    auto *edit = qobject_cast<QLineEdit *>(sender());
    if (!edit)
        return;

    QString name = edit->text().trimmed();
    if (!validateNewActionName(name))
    {
        QMessageBox::warning(this, "Invalid name", "Name must be unique and non-empty.");
        edit->setFocus();
        edit->selectAll();
        return;
    }

    // Collect slider values (convert to doubles in 0..140 deg for storage)
    QVector<double> vals;
    vals.reserve(sliders_.size());
    for (auto *s : sliders_)
        vals.push_back(double(s->value()));

    // Was it an add-row or rename?
    auto *layout = qobject_cast<QVBoxLayout *>(actionListWidget_->layout());
    int rowIdx = -1;
    for (int i = 0; i < layout->count(); ++i)
    {
        auto *row = layout->itemAt(i)->widget();
        if (!row)
            continue;
        if (row->findChild<QLineEdit *>() == edit)
        {
            rowIdx = i;
            break;
        }
    }
    if (rowIdx == addingRowIndex_)
    {
        // Add
        store_->addAction(name, vals);
        // convert row to normal button
        auto *row = layout->itemAt(rowIdx)->widget();
        auto *nameBtn = row->findChild<QPushButton *>();
        nameBtn->setText(name);
        nameBtn->show();
        edit->hide();
        connect(nameBtn, &QPushButton::clicked, this, &SidePanel::onActionNameClicked);
        addingRowIndex_ = -1;
        btnSave_->setText("Save");
    }
    else
    {
        // Rename: find which button matches this row's old name
        // (simple approach: refresh store entry by current focus)
        // For brevity, we just add if not found.
        int idx = store_->findActionByName(name);
        if (idx < 0)
            store_->addAction(name, vals);
        // else setActionName/setActionJoints(idx, vals)
    }

    persistStore();
}

void SidePanel::onToggleActions()
{
    bool vis = actionListWidget_->isVisible();
    actionListWidget_->setVisible(!vis);
    btnCollapseActions_->setArrowType(vis ? Qt::DownArrow : Qt::UpArrow);
}

void SidePanel::onToggleActionSets()
{
    bool vis = actionSetPanel_->isVisible();
    actionSetPanel_->setVisible(!vis);
    btnCollapseSets_->setArrowType(vis ? Qt::DownArrow : Qt::UpArrow);
}

void SidePanel::onDeleteAction()
{
    auto *layout = qobject_cast<QVBoxLayout *>(actionListWidget_->layout());
    // delete last normal action (simple UX parity; you can wire focus-based delete if desired)
    for (int i = layout->count() - 1; i >= 0; --i)
    {
        auto *row = layout->itemAt(i)->widget();
        if (!row)
            continue;
        if (row->findChild<QPushButton *>())
        {
            // remove from store by button text
            auto *btn = row->findChild<QPushButton *>();
            int idx = store_->findActionByName(btn->text());
            if (idx >= 0)
                store_->removeAction(idx);
            layout->removeWidget(row);
            delete row;
            persistStore();
            break;
        }
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

bool SidePanel::validateNewActionName(const QString &n) const
{
    if (n.trimmed().isEmpty())
        return false;
    return store_->findActionByName(n) == -1;
}
