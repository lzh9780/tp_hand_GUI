#include "ActionPanel.hpp"
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QString>
#include <QComboBox>
#include <QDebug>
#include <QSpacerItem>
#include <QScrollArea>
#include <QPushButton>
#include <QCheckBox>
#include <QLineEdit>
#include <QTimer>
#include <QToolButton>
#include <QApplication>
#include <QMessageBox>

#include "ActionStore.hpp"

ActionPanel::ActionPanel(QWidget *parent)
    : QFrame(parent)
{
    root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    buildUI();
}

void ActionPanel::buildUI()
{
    auto* controlRow = new QHBoxLayout();
    playBtn = new QPushButton("Play");
    auto* cyclePlay = new QCheckBox(QString("Cycle"));
    saveBtn = new QPushButton("Save");
    saveBtn->setEnabled(false);
    saveAsBtn = new QPushButton("Save as");
    saveAsBtn->setEnabled(false);
    deleteBtn = new QPushButton("Delete");
    deleteBtn->setEnabled(false);
    selectBox = new QComboBox();
    selectBox->addItem("New flow...");
    selectBox->setCurrentIndex(0);
    nameEdit = new QLineEdit();
    nameEdit->hide();
    auto* speedLbl = new QLabel("  Play\nSpeed: ");
    speedSlider = new QSlider(Qt::Horizontal);
    speedSlider->setRange((int)1, (int)5);
    speedSlider->setValue(3);
    speedSlider->setMinimumWidth(60);

    auto* playCol = new QVBoxLayout();
    auto* selectCol = new QVBoxLayout();
    auto* selectBtnRow = new QHBoxLayout();
    auto* valueRow = new QHBoxLayout();

    playCol->addWidget(playBtn);
    playCol->addWidget(cyclePlay);
    selectBtnRow->addWidget(saveBtn);
    selectBtnRow->addWidget(saveAsBtn);
    selectBtnRow->addWidget(deleteBtn);
    selectCol->addWidget(selectBox);
    selectCol->addWidget(nameEdit);
    selectCol->addLayout(selectBtnRow);
    valueRow->addWidget(new QLabel("1"));
    valueRow->addWidget(speedSlider);
    valueRow->addWidget(new QLabel("5"));
    
    controlRow->addLayout(playCol);
    controlRow->addLayout(selectCol);
    controlRow->addWidget(speedLbl);
    controlRow->addLayout(valueRow);

    root->addLayout(controlRow);

    // Action List table
    auto* table = new QScrollArea();
    auto* tableContent = new QWidget();
    tableLyt = new QVBoxLayout(tableContent);
    tableLyt->setSpacing(5);

    table->setWidget(tableContent);
    table->setWidgetResizable(true);
    root->addWidget(table);

    connect(cyclePlay, &QCheckBox::clicked, this, [=]{cycle = (cyclePlay->checkState() == Qt::Checked);});
    connect(playBtn, &QPushButton::clicked, this, &ActionPanel::play);
    connect(selectBox, &QComboBox::currentTextChanged, this, &ActionPanel::load);
    connect(speedSlider, &QSlider::valueChanged, this, &ActionPanel::changeSpeed);
    connect(deleteBtn, &QPushButton::clicked, this, &ActionPanel::deleteCurrentActionSet);
    connect(saveBtn, &QPushButton::clicked, this, &ActionPanel::saveActionSet);
    connect(saveAsBtn, &QPushButton::clicked, this, &ActionPanel::actionSetSaveAs);

    connect(qApp, &QApplication::focusChanged, this,
            [=](QWidget *old, QWidget *now)
            {
                if (old == nameEdit && now != nameEdit)
                    validate(nameEdit);
            });

    connect(nameEdit, &QLineEdit::editingFinished,
            this, [=]()
            { validate(nameEdit); });

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &ActionPanel::loopAction);
}

void ActionPanel::addAction(QString name)
{
    QLayoutItem *item;
    if (item = tableLyt->takeAt(tableLines.size()))
        delete item->widget();
    
    addActionToTable(name);

    tableLyt->addItem(new QSpacerItem(10, 10, QSizePolicy::Fixed, QSizePolicy::Expanding));
    buttonModeChange();
}

void ActionPanel::addActionToTable(QString name)
{
    auto* actionRow = new QWidget();
    auto* actionLyt = new QHBoxLayout(actionRow);
    auto* nameLbl = new QLabel(name);
    auto* removeBtn = new QPushButton("-");
    removeBtn->setFixedSize(25, 25);
    auto* moveUpBtn = new QToolButton;
    moveUpBtn->setArrowType(Qt::UpArrow);
    moveUpBtn->setFixedSize(25, 25);
    auto* moveDownBtn = new QToolButton;
    moveDownBtn->setArrowType(Qt::DownArrow);
    moveDownBtn->setFixedSize(25, 25);
    auto* spacer = new QSpacerItem(10, 10, QSizePolicy::Expanding, QSizePolicy::Fixed);

    actionLyt->addWidget(nameLbl);
    actionLyt->addItem(spacer);
    actionLyt->addWidget(moveUpBtn);
    actionLyt->addWidget(moveDownBtn);
    actionLyt->addWidget(removeBtn);
    actionLyt->setContentsMargins(0, 0, 0, 0);

    tableLyt->addWidget(actionRow);
    tableLines.append(nameLbl);

    int idx = tableLines.size() - 1;
    connect(moveUpBtn, &QToolButton::clicked, this, [=]() {
        actionMoveUp(idx);
    });
    connect(moveDownBtn, &QToolButton::clicked, this, [=]() {
        actionMoveDown(idx);
    });
    connect(removeBtn, &QPushButton::clicked, this, [=]() {
        removeActionByIndex(idx);
    });
}

void ActionPanel::removeActionByIndex(int idx)
{
    while (idx != tableLines.size() - 1) {
        actionMoveDown(idx);
        idx += 1;
    }
    
    QLayoutItem *item;
    item = tableLyt->takeAt(tableLines.size() - 1);
    if (item->widget())
    {
        delete item->widget();
    }
    tableLines.remove(tableLines.size() - 1);
    buttonModeChange();
}

void ActionPanel::buttonModeChange()
{
    if (tableLines.size() <= 0) 
    {
        saveBtn->setEnabled(false);
        saveAsBtn->setEnabled(false);
    } else if (selectBox->currentText() == "New flow...") {
        saveBtn->setEnabled(true);
        saveAsBtn->setEnabled(false);
    } else {
        int idx = store_->findActionSetByName(selectBox->currentText());
        auto currSet = store_->actionSetList(idx);
        
        if (tableLines.size() != currSet.size())
        {
            saveBtn->setEnabled(true);
            saveAsBtn->setEnabled(true);
        } else {
            bool same = true;
            int i = 0;
            while (i < tableLines.size()) {
                if (tableLines[i]->text() != currSet[i]) 
                {
                    same = false;
                    break;
                }
                i += 1;
            }
            saveBtn->setEnabled(!same);
            saveAsBtn->setEnabled(true);
        }
    }
}

void ActionPanel::loopAction()
{
    int prev_idx = curr_idx - 1;
    if (prev_idx < 0) prev_idx = tableLines.size() - 1;
    tableLines[prev_idx]->setStyleSheet(
            "QLabel { background: #f0f0f0;}");
    
    if (curr_idx == tableLines.size())
    {
        if (cycle) 
            curr_idx = 0;
        else
        {
            play();
            return;
        }
    }

    tableLines[curr_idx]->setStyleSheet(
                "QLabel { background: #3a3df8; color: #ffffff}");
    emit actionUpdate(tableLines[curr_idx]->text());
    
    curr_idx += 1;
}

void ActionPanel::play()
{
    if (!playBtn) return;
    if (tableLines.size() == 0) return;

    if (playBtn->text() == "Play") 
    {
        playBtn->setText("Stop");
        loopAction();
        timer_->start(currStopTime);
    } else if (playBtn->text() == "Stop") 
    {
        playBtn->setText("Play");
        timer_->stop();
        int prev_idx = curr_idx - 1;
        if (prev_idx < 0) prev_idx = 0;
        tableLines[prev_idx]->setStyleSheet(
                "QLabel { background: #f0f0f0;}");
        curr_idx = 0;
    }
}

void ActionPanel::loadFlowList()
{
    for (int i = 0; i < store_->actionSetCount(); ++i)
    {
        selectBox->addItem(store_->actionSetName(i));
    }
}

void ActionPanel::changeSpeed()
{
    currStopTime = stopTime[speedSlider->value() - 1];
}

void ActionPanel::load()
{
    clearActionList();
    if (selectBox->currentText() == "New flow...") 
    {
        deleteBtn->setEnabled(false);
        saveAsBtn->setEnabled(false);
        return;
    }

    int idx = store_->findActionSetByName(selectBox->currentText());
    for (const auto &s : store_->actionSetList(idx))
    {
        if (store_->findActionByName(s) == -1)
        {
            /* TODO: give a warning box*/
            continue;
        }
        
        addActionToTable(s);
    }
    tableLyt->addItem(new QSpacerItem(10, 10, QSizePolicy::Fixed, QSizePolicy::Expanding));
    deleteBtn->setEnabled(true);
    saveAsBtn->setEnabled(true);
}

void ActionPanel::actionMoveUp(int idx)
{
    if (idx == 0) return;
    auto buffer = tableLines[idx - 1]->text();
    tableLines[idx - 1]->setText(tableLines[idx]->text());
    tableLines[idx]->setText(QString(buffer));
    buttonModeChange();
}

void ActionPanel::actionMoveDown(int idx)
{
    if (idx == tableLines.size() - 1) return;
    auto buffer = tableLines[idx + 1]->text();
    tableLines[idx + 1]->setText(tableLines[idx]->text());
    tableLines[idx]->setText(QString(buffer));
    buttonModeChange();
}

void ActionPanel::updateStore(ActionStore *store)
{
    store_ = store;
}

void ActionPanel::saveActionSet()
{
    if (tableLines.size() == 0) return;
    if (selectBox->currentText() != "New flow...") {
        QVector<QString> actions;
        for (const auto* a : tableLines)
        {    
            qDebug() << a->text();
            actions.append(a->text());
        }

        int idx = store_->findActionSetByName(selectBox->currentText());
        qDebug() << idx;
        store_->setActionSetValues(idx, actions);
        store_->save();
        saveBtn->setEnabled(false);
    } else {
        nameEdit->show();
        selectBox->hide();
        nameEdit->setFocus();
    }
}

void ActionPanel::actionSetSaveAs()
{
    if (tableLines.size() == 0) return;
    nameEdit->show();
    selectBox->hide();
    nameEdit->setFocus();
}

void ActionPanel::deleteCurrentActionSet()
{
    // TODO: Add a confirm window
    int idx = store_->findActionSetByName(selectBox->currentText());
    store_->removeActionSet(idx);
    store_->save();
    selectBox->removeItem(idx + 1);
    selectBox->setCurrentIndex(0);
}

void ActionPanel::validate(QLineEdit* edit)
{
    if (edit->property("vaildating").toBool())
        return;
    edit->setProperty("vaildating", true);

    bool pass = false;
    if (edit->text().trimmed().isEmpty())
        pass = false;
    else { 
        int i = store_->findActionSetByName(edit->text());
        if (i == -1) pass = true;
    }

    if (pass) {
        QVector<QString> actions;
        for (const auto* a : tableLines)
            actions.append(a->text());
        store_->addActionSet(edit->text(), actions);
        store_->save();
        selectBox->addItem(edit->text());
        selectBox->show();
        selectBox->setCurrentIndex(selectBox->count() - 1);
        edit->hide();
        edit->setText("");        
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

void ActionPanel::clearActionList()
{
    tableLines.clear();
    QLayoutItem *item;
    while ((item = tableLyt->takeAt(0)) != nullptr)
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
}

void ActionPanel::clearAll()
{
    clearActionList();
    selectBox->setCurrentIndex(0);
    while (selectBox->count() > 1)
        selectBox->removeItem(1);
    saveBtn->setEnabled(false);
    saveAsBtn->setEnabled(false);
    deleteBtn->setEnabled(false);
}
