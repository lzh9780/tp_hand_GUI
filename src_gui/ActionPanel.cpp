#include "ActionPanel.hpp"
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QLabel>
#include <QString>
#include <QComboBox>
#include <QDebug>
#include <QListWidget>
#include <QScrollArea>
#include <QPushButton>
#include <QCheckBox>
#include <QLineEdit>
#include <QTimer>
#include <QToolButton>
#include <QInputDialog>
#include <QMessageBox>

#include "ActionStore.hpp"

namespace
{
// Placeholder entry meaning "no action set loaded yet".
const QString kNewFlow = QStringLiteral("New flow...");
}

ActionPanel::ActionPanel(QWidget *parent)
    : QFrame(parent)
{
    buildUI();

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &ActionPanel::loopAction);
}

ActionPanel::~ActionPanel()
{
    delete store_;
    store_ = nullptr;
}

void ActionPanel::buildUI()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(buildLibraryPane());
    splitter->addWidget(buildFlowPane());
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);
    splitter->setChildrenCollapsible(false);

    root->addWidget(splitter, 1);
}

QWidget *ActionPanel::buildLibraryPane()
{
    auto *box = new QGroupBox(QStringLiteral("Actions"));
    auto *lyt = new QVBoxLayout(box);
    lyt->setContentsMargins(8, 10, 8, 8);
    lyt->setSpacing(4);

    actionList_ = new QListWidget();
    actionList_->setToolTip("Double-click an action to apply it to the joints.");

    newBtn_ = new QPushButton(QStringLiteral("New"));
    newBtn_->setToolTip("Capture the current joint pose as a new action");
    renameBtn_ = new QPushButton(QStringLiteral("Rename"));
    renameBtn_->setToolTip("Rename the selected action");
    deleteBtn_ = new QPushButton(QStringLiteral("Delete"));
    deleteBtn_->setToolTip("Delete the selected action");
    addToSetBtn_ = new QPushButton(QStringLiteral("Add to set »"));
    addToSetBtn_->setToolTip("Append the selected action to the current action set");

    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(4);
    btnRow->addWidget(newBtn_);
    btnRow->addWidget(renameBtn_);
    btnRow->addWidget(deleteBtn_);
    btnRow->addStretch(1);

    nameEdit_ = new QLineEdit();
    nameEdit_->setPlaceholderText("Action name, then press Enter");
    nameEdit_->hide();

    lyt->addLayout(btnRow);
    lyt->addWidget(actionList_, 1);
    lyt->addWidget(nameEdit_);
    lyt->addWidget(addToSetBtn_);

    connect(newBtn_, &QPushButton::clicked, this, &ActionPanel::onNewAction);
    connect(renameBtn_, &QPushButton::clicked, this, &ActionPanel::onRenameAction);
    connect(deleteBtn_, &QPushButton::clicked, this, &ActionPanel::onDeleteAction);
    connect(addToSetBtn_, &QPushButton::clicked, this, &ActionPanel::onAddToSet);
    connect(actionList_, &QListWidget::itemActivated, this, &ActionPanel::onActionActivated);
    connect(actionList_, &QListWidget::itemSelectionChanged,
            this, &ActionPanel::onLibrarySelectionChanged);
    connect(nameEdit_, &QLineEdit::returnPressed, this, &ActionPanel::commitNameEdit);
    connect(nameEdit_, &QLineEdit::editingFinished, this, &ActionPanel::commitNameEdit);

    return box;
}

QWidget *ActionPanel::buildFlowPane()
{
    auto *box = new QGroupBox(QStringLiteral("Action Set"));
    auto *lyt = new QVBoxLayout(box);
    lyt->setContentsMargins(8, 10, 8, 8);
    lyt->setSpacing(4);

    // --- flow selector row ---
    selectBox = new QComboBox();
    selectBox->addItem(kNewFlow);
    selectBox->setCurrentIndex(0);

    saveBtn = new QPushButton(QStringLiteral("Save"));
    saveBtn->setToolTip("Store the steps under the selected action set");
    saveAsBtn = new QPushButton(QStringLiteral("Save as"));
    saveAsBtn->setToolTip("Store the steps under a new action set");
    deleteFlowBtn_ = new QPushButton(QStringLiteral("Delete"));
    deleteFlowBtn_->setToolTip("Delete the selected action set");

    auto *selectRow = new QHBoxLayout();
    selectRow->setSpacing(4);
    selectRow->addWidget(selectBox, 1);
    selectRow->addWidget(saveBtn);
    selectRow->addWidget(saveAsBtn);
    selectRow->addWidget(deleteFlowBtn_);

    // --- playback row ---
    playBtn = new QPushButton(QStringLiteral("Play"));
    auto *cyclePlay = new QCheckBox(QStringLiteral("Cycle"));
    cyclePlay->setToolTip("Restart from the top when the last step finished");
    speedSlider = new QSlider(Qt::Horizontal);
    speedSlider->setRange(1, 5);
    speedSlider->setValue(3);
    speedSlider->setMinimumWidth(60);

    auto *playRow = new QHBoxLayout();
    playRow->setSpacing(4);
    playRow->addWidget(playBtn);
    playRow->addWidget(cyclePlay);
    playRow->addStretch(1);
    playRow->addWidget(new QLabel(QStringLiteral("Speed:")));
    playRow->addWidget(new QLabel(QStringLiteral("1")));
    playRow->addWidget(speedSlider);
    playRow->addWidget(new QLabel(QStringLiteral("5")));

    // --- steps ---
    flowList_ = new QListWidget();
    flowList_->setToolTip("Ordered steps executed by Play");

    upBtn_ = new QToolButton();
    upBtn_->setArrowType(Qt::UpArrow);
    upBtn_->setFixedSize(26, 26);
    upBtn_->setToolTip("Move the selected step up");
    downBtn_ = new QToolButton();
    downBtn_->setArrowType(Qt::DownArrow);
    downBtn_->setFixedSize(26, 26);
    downBtn_->setToolTip("Move the selected step down");
    removeStepBtn_ = new QPushButton(QStringLiteral("Remove"));
    removeStepBtn_->setToolTip("Remove the selected step");

    auto *stepRow = new QHBoxLayout();
    stepRow->setSpacing(4);
    stepRow->addWidget(upBtn_);
    stepRow->addWidget(downBtn_);
    stepRow->addStretch(1);
    stepRow->addWidget(removeStepBtn_);

    lyt->addLayout(selectRow);
    lyt->addLayout(playRow);
    lyt->addWidget(flowList_, 1);
    lyt->addLayout(stepRow);

    connect(cyclePlay, &QCheckBox::clicked, this,
            [this, cyclePlay]() { cycle = (cyclePlay->checkState() == Qt::Checked); });
    connect(playBtn, &QPushButton::clicked, this, &ActionPanel::play);
    connect(speedSlider, &QSlider::valueChanged, this, &ActionPanel::changeSpeed);
    connect(selectBox, &QComboBox::currentTextChanged, this, &ActionPanel::loadFlow);
    connect(saveBtn, &QPushButton::clicked, this, &ActionPanel::saveFlow);
    connect(saveAsBtn, &QPushButton::clicked, this, &ActionPanel::saveFlowAs);
    connect(deleteFlowBtn_, &QPushButton::clicked, this, &ActionPanel::deleteFlow);
    connect(upBtn_, &QToolButton::clicked, this, &ActionPanel::onStepUp);
    connect(downBtn_, &QToolButton::clicked, this, &ActionPanel::onStepDown);
    connect(removeStepBtn_, &QPushButton::clicked, this, &ActionPanel::onStepRemove);
    connect(flowList_, &QListWidget::itemSelectionChanged,
            this, &ActionPanel::onFlowSelectionChanged);

    changeSpeed();
    updateFlowButtons();
    updateLibraryButtons();

    return box;
}

// ============ store ============

void ActionPanel::setStore(ActionStore *store)
{
    if (store_ == store)
        return;

    stopPlayback();
    endNameEdit();

    // This panel owns the store: drop the previous one before adopting the new.
    delete store_;
    store_ = store;

    clearFlowSteps();
    reloadLibrary();
    reloadFlowList();
    loadFlow();
}

void ActionPanel::setPoseProvider(std::function<QVector<double>()> provider)
{
    poseProvider_ = std::move(provider);
}

QVector<double> ActionPanel::actionValues(const QString &name) const
{
    if (!store_)
        return {};
    const int idx = store_->findActionByName(name);
    if (idx < 0)
        return {};
    return store_->actionJoints(idx);
}

void ActionPanel::persist() const
{
    if (store_)
        store_->save();
}

// ============ library ============

void ActionPanel::reloadLibrary()
{
    actionList_->clear();
    // Refresh the buttons on the empty-store path too, otherwise "New" and
    // friends stay enabled after a disconnect.
    if (!store_)
    {
        updateLibraryButtons();
        return;
    }
    for (int i = 0; i < store_->actionCount(); ++i)
        actionList_->addItem(store_->actionName(i));
    updateLibraryButtons();
}

void ActionPanel::onLibrarySelectionChanged()
{
    updateLibraryButtons();
}

void ActionPanel::updateLibraryButtons()
{
    const bool hasStore = (store_ != nullptr);
    const bool hasSelection = (actionList_->currentItem() != nullptr);

    newBtn_->setEnabled(hasStore);
    renameBtn_->setEnabled(hasStore && hasSelection);
    deleteBtn_->setEnabled(hasStore && hasSelection);
    addToSetBtn_->setEnabled(hasStore && hasSelection);
}

void ActionPanel::onActionActivated(QListWidgetItem *item)
{
    if (!item)
        return;
    emit actionUpdate(item->text());
}

void ActionPanel::onAddToSet()
{
    auto *item = actionList_->currentItem();
    if (!item)
        return;
    addFlowStep(item->text());
}

void ActionPanel::beginNameEdit(bool rename)
{
    if (!store_)
        return;

    if (rename)
    {
        auto *item = actionList_->currentItem();
        if (!item)
            return;
        nameMode_ = NameMode::Rename;
        nameEdit_->setText(item->text());
    }
    else
    {
        nameMode_ = NameMode::Create;
        nameEdit_->clear();
    }

    nameEdit_->show();
    nameEdit_->setFocus();
    nameEdit_->selectAll();
}

void ActionPanel::endNameEdit()
{
    nameMode_ = NameMode::None;
    nameEdit_->blockSignals(true);
    nameEdit_->clear();
    nameEdit_->hide();
    nameEdit_->blockSignals(false);
}

void ActionPanel::onNewAction()
{
    beginNameEdit(false);
}

void ActionPanel::onRenameAction()
{
    beginNameEdit(true);
}

void ActionPanel::cancelNameEdit()
{
    endNameEdit();
}

void ActionPanel::commitNameEdit()
{
    if (nameMode_ == NameMode::None)
        return;

    const NameMode mode = nameMode_;
    const QString name = nameEdit_->text().trimmed();

    if (name.isEmpty())
    {
        endNameEdit();
        return;
    }

    if (store_->findActionByName(name) != -1)
    {
        QMessageBox::warning(this, "Invalid name", "Name must be unique and non-empty.");
        nameEdit_->setFocus();
        nameEdit_->selectAll();
        return;
    }

    if (mode == NameMode::Create)
    {
        QVector<double> pose;
        if (poseProvider_)
            pose = poseProvider_();
        store_->addAction(name, pose);
    }
    else
    {
        auto *item = actionList_->currentItem();
        if (!item)
        {
            endNameEdit();
            return;
        }

        const QString old = item->text();
        if (name == old)
        {
            endNameEdit();
            return;
        }

        const auto sets = store_->findActionInSet(old);
        if (!sets.isEmpty())
        {
            QMessageBox msgBox;
            msgBox.setTextFormat(Qt::MarkdownText);
            msgBox.setWindowTitle("Rename Action");
            msgBox.setText(QString("#### Are you sure to rename action %1 to %2?").arg(old, name));
            msgBox.setInformativeText("The action is referenced by one or more action sets.");
            msgBox.setIcon(QMessageBox::Warning);
            auto *renameBtn = msgBox.addButton(QMessageBox::Ok);
            msgBox.addButton(QMessageBox::Cancel);
            auto *removeBtn = msgBox.addButton("Delete All", QMessageBox::ActionRole);

            QString detail;
            for (const auto &s : sets)
                detail.append(QString("  - %1\n").arg(s));
            msgBox.setDetailedText(detail);
            msgBox.exec();

            if (msgBox.clickedButton() == removeBtn)
                store_->removeActionFromSet(old);
            else if (msgBox.clickedButton() != renameBtn)
            {
                endNameEdit();
                return;
            }
            else
                store_->renameActionInSet(old, name);
        }

        const int idx = store_->findActionByName(old);
        if (idx >= 0)
            store_->setActionName(idx, name);

        // Steps shown on the right track the rename so the set stays playable.
        renameSteps(old, name);
    }

    endNameEdit();
    reloadLibrary();

    // Reselect the entry that was just created or renamed.
    auto found = actionList_->findItems(name, Qt::MatchExactly);
    if (!found.isEmpty())
        actionList_->setCurrentItem(found.first());

    dirty_ = true;
    persist();
    updateLibraryButtons();
    updateFlowButtons();
}

void ActionPanel::onDeleteAction()
{
    if (!store_)
        return;
    auto *item = actionList_->currentItem();
    if (!item)
        return;

    const QString name = item->text();
    const auto sets = store_->findActionInSet(name);

    QMessageBox msgBox;
    msgBox.setTextFormat(Qt::MarkdownText);
    msgBox.setWindowTitle("Delete Action");
    msgBox.setText(QString("#### Are you sure to delete action %1?").arg(name));
    msgBox.setInformativeText("This action will also be removed from any action set that uses it.");
    msgBox.setIcon(QMessageBox::Warning);
    auto *confirmBtn = msgBox.addButton(QMessageBox::Ok);
    msgBox.addButton(QMessageBox::Cancel);

    QString detail;
    if (sets.isEmpty())
        detail = QString("No action set contains action %1").arg(name);
    else
        for (const auto &s : sets)
            detail.append(QString("  - %1\n").arg(s));
    msgBox.setDetailedText(detail);
    msgBox.exec();

    if (msgBox.clickedButton() != confirmBtn)
        return;

    const int idx = store_->findActionByName(name);
    if (idx >= 0)
        store_->removeAction(idx);
    if (!sets.isEmpty())
        store_->removeActionFromSet(name);

    dropSteps(name);
    endNameEdit();
    reloadLibrary();

    dirty_ = true;
    persist();
    updateLibraryButtons();
    updateFlowButtons();
}

// ============ action set ============

void ActionPanel::reloadFlowList()
{
    if (!selectBox)
        return;

    const QString previous = selectBox->currentText();
    selectBox->blockSignals(true);
    selectBox->clear();
    selectBox->addItem(kNewFlow);
    if (store_)
        for (int i = 0; i < store_->actionSetCount(); ++i)
            selectBox->addItem(store_->actionSetName(i));
    const int idx = selectBox->findText(previous);
    selectBox->setCurrentIndex(idx >= 0 ? idx : 0);
    selectBox->blockSignals(false);
}

QVector<QString> ActionPanel::currentSteps() const
{
    QVector<QString> steps;
    if (!flowList_)
        return steps;
    steps.reserve(flowList_->count());
    for (int i = 0; i < flowList_->count(); ++i)
        steps.append(flowList_->item(i)->text());
    return steps;
}

void ActionPanel::clearFlowSteps()
{
    if (flowList_)
        flowList_->clear();
    highlightStep(-1);
}

void ActionPanel::addFlowStep(const QString &name)
{
    if (!flowList_)
        return;
    flowList_->addItem(name);
    dirty_ = true;
    updateFlowButtons();
}

void ActionPanel::removeFlowStep(int row)
{
    if (!flowList_ || row < 0 || row >= flowList_->count())
        return;
    delete flowList_->takeItem(row);
    dirty_ = true;
    updateFlowButtons();
}

void ActionPanel::moveFlowStep(int row, int delta)
{
    if (!flowList_)
        return;
    const int target = row + delta;
    if (row < 0 || row >= flowList_->count() || target < 0 || target >= flowList_->count())
        return;

    QListWidgetItem *item = flowList_->takeItem(row);
    flowList_->insertItem(target, item);
    flowList_->setCurrentItem(item);
    dirty_ = true;
    updateFlowButtons();
}

void ActionPanel::renameSteps(const QString &from, const QString &to)
{
    if (!flowList_)
        return;
    for (int i = 0; i < flowList_->count(); ++i)
        if (flowList_->item(i)->text() == from)
            flowList_->item(i)->setText(to);
}

void ActionPanel::dropSteps(const QString &name)
{
    if (!flowList_)
        return;
    for (int i = flowList_->count() - 1; i >= 0; --i)
        if (flowList_->item(i)->text() == name)
            delete flowList_->takeItem(i);
}

void ActionPanel::loadFlow()
{
    stopPlayback();
    clearFlowSteps();

    if (!store_ || selectBox->currentText() == kNewFlow)
    {
        dirty_ = false;
        updateFlowButtons();
        return;
    }

    const int idx = store_->findActionSetByName(selectBox->currentText());
    if (idx == -1)
    {
        // The selected set disappeared from the store: rebuild the list and
        // fall back to the placeholder.
        reloadFlowList();
        loadFlow();
        return;
    }

    for (const auto &s : store_->actionSetList(idx))
    {
        if (store_->findActionByName(s) == -1)
        {
            QMessageBox::warning(this, "Action Not Found",
                QString("The action %1 hasn't been found. Please check the storage file.").arg(s));
            continue;
        }
        addFlowStep(s);
    }

    dirty_ = false;
    updateFlowButtons();
}

void ActionPanel::updateFlowButtons()
{
    const bool hasStore = (store_ != nullptr);
    const int steps = flowList_ ? flowList_->count() : 0;
    const bool isNew = selectBox->currentText() == kNewFlow;
    const int row = flowList_ ? flowList_->currentRow() : -1;

    saveBtn->setEnabled(hasStore && steps > 0 && (isNew || dirty_));
    saveAsBtn->setEnabled(hasStore && steps > 0);
    deleteFlowBtn_->setEnabled(hasStore && !isNew);
    playBtn->setEnabled(steps > 0);
    speedSlider->setEnabled(steps > 0);

    upBtn_->setEnabled(row > 0);
    downBtn_->setEnabled(row >= 0 && row < steps - 1);
    removeStepBtn_->setEnabled(row >= 0);
}

void ActionPanel::onStepUp()
{
    moveFlowStep(flowList_->currentRow(), -1);
}

void ActionPanel::onStepDown()
{
    moveFlowStep(flowList_->currentRow(), +1);
}

void ActionPanel::onStepRemove()
{
    removeFlowStep(flowList_->currentRow());
}

void ActionPanel::onFlowSelectionChanged()
{
    updateFlowButtons();
}

void ActionPanel::saveFlow()
{
    if (!store_ || flowList_->count() == 0)
        return;

    // "New flow..." has no name yet, so saving always goes through "Save as".
    if (selectBox->currentText() == kNewFlow)
    {
        saveFlowAs();
        return;
    }

    const int idx = store_->findActionSetByName(selectBox->currentText());
    if (idx == -1)
    {
        reloadFlowList();
        loadFlow();
        return;
    }

    store_->setActionSetValues(idx, currentSteps());
    persist();
    dirty_ = false;
    updateFlowButtons();
}

void ActionPanel::saveFlowAs()
{
    if (!store_ || flowList_->count() == 0)
        return;

    bool ok = false;
    const QString name = QInputDialog::getText(this, "Save Action Set", "Action set name:",
                                               QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;

    if (store_->findActionSetByName(name) != -1)
    {
        QMessageBox::warning(this, "Invalid name", "Name must be unique and non-empty.");
        return;
    }

    store_->addActionSet(name, currentSteps());
    persist();
    dirty_ = false;

    reloadFlowList();
    selectBox->setCurrentText(name); // triggers loadFlow(), which re-adds steps
}

void ActionPanel::deleteFlow()
{
    if (!store_)
        return;

    const QString name = selectBox->currentText();
    const int idx = store_->findActionSetByName(name);
    if (idx == -1)
        return;

    QMessageBox msgBox;
    msgBox.setTextFormat(Qt::MarkdownText);
    msgBox.setWindowTitle("Delete Action Set");
    msgBox.setText(QString("#### Are you sure to delete action set %1?").arg(name));
    msgBox.setIcon(QMessageBox::Warning);
    auto *confirmBtn = msgBox.addButton(QMessageBox::Ok);
    msgBox.addButton(QMessageBox::Cancel);
    msgBox.exec();

    if (msgBox.clickedButton() != confirmBtn)
        return;

    stopPlayback();
    store_->removeActionSet(idx);
    persist();
    reloadFlowList();
    loadFlow();
}

// ============ playback ============

void ActionPanel::highlightStep(int row)
{
    if (!flowList_)
        return;
    for (int i = 0; i < flowList_->count(); ++i)
    {
        auto *item = flowList_->item(i);
        const bool active = (i == row);
        item->setBackground(active ? QColor("#3a3df8") : QColor("#ffffff"));
        item->setForeground(active ? QColor("#ffffff") : QColor("#000000"));
    }
}

void ActionPanel::stopPlayback()
{
    if (timer_)
        timer_->stop();
    currStep_ = 0;
    if (playBtn)
        playBtn->setText("Play");
    highlightStep(-1);
}

void ActionPanel::play()
{
    if (!playBtn || !timer_ || flowList_->count() == 0)
        return;

    if (playBtn->text() == "Play")
    {
        playBtn->setText("Stop");
        currStep_ = 0;
        loopAction();
        timer_->start(currStopTime);
    }
    else
    {
        stopPlayback();
    }
}

void ActionPanel::loopAction()
{
    if (flowList_->count() == 0)
    {
        stopPlayback();
        return;
    }

    if (currStep_ >= flowList_->count())
    {
        if (cycle)
            currStep_ = 0;
        else
        {
            stopPlayback();
            return;
        }
    }

    highlightStep(currStep_);
    emit actionUpdate(flowList_->item(currStep_)->text());
    currStep_ += 1;
}

void ActionPanel::changeSpeed()
{
    const int level = qBound(1, speedSlider->value(), 5);
    currStopTime = stopTime[level - 1];
    // Apply the new interval immediately instead of waiting for a restart.
    if (timer_ && timer_->isActive())
        timer_->start(currStopTime);
}
