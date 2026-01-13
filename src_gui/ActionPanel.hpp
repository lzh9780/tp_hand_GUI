#pragma once
#include <QFrame>
#include <QVector>
#include <unordered_map>
#include "grip/settings.hpp"

class QVBoxLayout;
class QComboBox;
class QTimer;
class QLabel;
class QPushButton;
class QSlider;
class QLineEdit;

class ActionStore;

class ActionPanel : public QFrame
{
    Q_OBJECT
public:
    explicit ActionPanel(QWidget *parent = nullptr);
    void buildUI();
    void addAction(QString name);
    void updateStore(ActionStore *store);
    void loadFlowList();
    void clearAll();
    void reload();

signals:
    void actionUpdate(const QString& msg);

private:
    void play();
    void load();
    void clearActionList();
    void loopAction();
    void changeSpeed();
    void actionMoveUp(int idx);
    void actionMoveDown(int idx);
    void removeActionByIndex(int idx);
    void saveActionSet();
    void actionSetSaveAs();
    void addActionToTable(QString name);
    void buttonModeChange();
    void deleteCurrentActionSet();
    void validate(QLineEdit* edit);

private:
    QVBoxLayout* root;
    QComboBox* selectBox;
    QVector<QLabel*> tableLines;
    QVBoxLayout* tableLyt;
    QPushButton* playBtn;
    QPushButton* saveBtn;
    QPushButton* saveAsBtn;
    QPushButton* deleteBtn;
    QSlider* speedSlider;
    QLineEdit* nameEdit;

    int stopTime[5] = {2500, 1500, 1000, 750, 500};
    int currStopTime = 1000;
    bool cycle = false;
    ActionStore* store_{nullptr};
    QTimer* timer_;
    int curr_idx = 0;
};