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
    auto* saveBtn = new QPushButton("Save");
    saveBtn->setEnabled(false);
    auto* saveAsBtn = new QPushButton("Save as");
    saveAsBtn->setEnabled(false);
    auto* deleteBtn = new QPushButton("Delete");
    deleteBtn->setEnabled(false);
    select = new QComboBox();
    select->addItem("New flow...");
    // for (const auto s : savedflows)
    //     select->addItem(s);
    select->setCurrentIndex(0);
    auto* nameEdit = new QLineEdit();
    nameEdit->hide();
    auto* speedLbl = new QLabel("  Play\nSpeed: ");
    auto* speedSlider = new QSlider(Qt::Horizontal);
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
    selectCol->addWidget(select);
    selectCol->addWidget(nameEdit);
    selectCol->addLayout(selectBtnRow);
    valueRow->addWidget(new QLabel("1"));
    valueRow->addWidget(speedSlider);
    valueRow->addWidget(new QLabel("5"));
    
    controlRow->addLayout(playCol);
    controlRow->addLayout(selectCol);
    controlRow->addWidget(speedLbl);
    controlRow->addLayout(valueRow);

    connect(cyclePlay, &QCheckBox::clicked, this, [=]{cycle = (cyclePlay->checkState() == Qt::Checked);});
    connect(playBtn, &QPushButton::clicked, this, &ActionPanel::play);
    connect(select, &QComboBox::currentTextChanged, this, &ActionPanel::load);
    root->addLayout(controlRow);

    // Action List table
    auto* table = new QScrollArea();
    auto* tableContent = new QWidget();
    tableLyt = new QVBoxLayout(tableContent);
    tableLyt->setSpacing(5);

    table->setWidget(tableContent);
    table->setWidgetResizable(true);
    root->addWidget(table);

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &ActionPanel::loopAction);
}

void ActionPanel::addAction(QString* name)
{
    return;
}

void ActionPanel::removeAction(QString* name)
{
    return;
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
                "QLabel { background: #ffffff;}");
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
        timer_->start(1000);
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
        select->addItem(store_->actionSetName(i));
    }
}

void ActionPanel::load()
{
    clearActionList();
    if (select->currentText() == "New flow...") return;

    int idx = store_->findActionSetByName(select->currentText());
    for (const auto &s : store_->actionSetList(idx))
    {
        if (store_->findActionByName(s) == -1)
        {
            /* TODO: give a warning box*/
            continue;
        }
        
        auto* w = new QLabel(s);
        tableLyt->addWidget(w);
        tableLines.append(w);
    }
    tableLyt->addItem(new QSpacerItem(10, 10, QSizePolicy::Fixed, QSizePolicy::Expanding));
}

void ActionPanel::updateStore(ActionStore *store)
{
    store_ = store;
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
    select->setCurrentIndex(0);
    while (select->count() > 1)
        select->removeItem(1);
}
