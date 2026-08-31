#pragma once
#include <QFrame>
#include <QVector>
#include <QHash>
#include <functional>
#include <unordered_map>
#include "grip/settings.hpp"

class QVBoxLayout;
class QComboBox;
class QTimer;
class QLabel;
class QPushButton;
class QSlider;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QCheckBox;
class QToolButton;

class ActionStore;

/**
 * ActionPanel
 * -----------
 * Owns both the action library and the action-set (flow) editor. The saved
 * actions used to live in SidePanel; they now sit next to the flow that
 * consumes them so a pose can be captured, ordered and played without leaving
 * the tab.
 *
 *   +---------------------------+----------------------------------------+
 *   | Actions                   | Action Set                               |
 *   | [New][Rename][Delete]     | [flow combo] [Save][Save as][Delete]     |
 *   | ┌───────────────────┐     | [Play] [x]Cycle   Speed 1[-----]5        |
 *   | │ saved poses       │     | ┌──────────────────────────────────┐     |
 *   | │                   │     | │ step 1                           │     |
 *   | └───────────────────┘     | │ step 2                           │     |
 *   | [Add to set ->]           | └──────────────────────────────────┘     |
 *   +---------------------------+----------------------------------------+
 *
 * The panel takes ownership of the ActionStore handed to setStore().
 */
class ActionPanel : public QFrame
{
    Q_OBJECT
public:
    explicit ActionPanel(QWidget *parent = nullptr);
    ~ActionPanel() override;

    // Takes ownership of `store`; deletes any previous one. nullptr clears.
    void setStore(ActionStore *store);
    // Supplies the live joint pose captured when a new action is created.
    void setPoseProvider(std::function<QVector<double>()> provider);

    // Joint values stored for `name`; empty when the action is unknown.
    QVector<double> actionValues(const QString &name) const;

signals:
    void actionUpdate(const QString& msg);

private slots:
    // --- action library ---
    void onNewAction();
    void onRenameAction();
    void onDeleteAction();
    void onAddToSet();
    void onActionActivated(QListWidgetItem *item);
    void onLibrarySelectionChanged();
    void commitNameEdit();
    void cancelNameEdit();

    // --- action set ---
    void play();
    void loopAction();
    void changeSpeed();
    void loadFlow();
    void saveFlow();
    void saveFlowAs();
    void deleteFlow();
    void onStepUp();
    void onStepDown();
    void onStepRemove();
    void onFlowSelectionChanged();

private:
    void buildUI();
    QWidget *buildLibraryPane();
    QWidget *buildFlowPane();

    void reloadLibrary();
    void reloadFlowList();
    void clearFlowSteps();
    void addFlowStep(const QString &name);
    void removeFlowStep(int row);
    void moveFlowStep(int row, int delta);
    void renameSteps(const QString &from, const QString &to);
    void dropSteps(const QString &name);
    void highlightStep(int row);
    void stopPlayback();

    void beginNameEdit(bool rename);
    void endNameEdit();
    void persist() const;
    QVector<QString> currentSteps() const;
    void updateLibraryButtons();
    void updateFlowButtons();

private:
    enum class NameMode { None, Create, Rename };

    // library
    QListWidget *actionList_{nullptr};
    QPushButton *newBtn_{nullptr};
    QPushButton *renameBtn_{nullptr};
    QPushButton *deleteBtn_{nullptr};
    QPushButton *addToSetBtn_{nullptr};
    QLineEdit *nameEdit_{nullptr};

    // flow
    QComboBox *selectBox{nullptr};
    QPushButton *playBtn{nullptr};
    QPushButton *saveBtn{nullptr};
    QPushButton *saveAsBtn{nullptr};
    QPushButton *deleteFlowBtn_{nullptr};
    QToolButton *upBtn_{nullptr};
    QToolButton *downBtn_{nullptr};
    QPushButton *removeStepBtn_{nullptr};
    QSlider *speedSlider{nullptr};
    QListWidget *flowList_{nullptr};

    // editing / playback state
    NameMode nameMode_{NameMode::None};
    bool cycle{false};
    int currStep_{0};
    bool dirty_{false};

    int stopTime[5] = {2500, 1500, 1000, 750, 500};
    int currStopTime{1000};
    QTimer *timer_{nullptr};

    ActionStore *store_{nullptr};
    std::function<QVector<double>()> poseProvider_;
};
