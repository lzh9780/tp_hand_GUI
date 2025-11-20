#pragma once
#include <QFrame>
#include <QList>
#include <QStringList>
#include <QJsonArray>

class QSlider;
class QLabel;
class QScrollArea;
class QWidget;
class QVBoxLayout;
class QSplitter;
class QPushButton;
class QToolButton;
class QLineEdit;
class QImage;
class QComboBox;

class ActionStore; // fwd

/**
 * SidePanel (GUI)
 * ---------------
 * Matches your original views:
 * - Top control row with mode toggle ("Mode: Follow"/"Mode: Setup") and "Execute"
 * - Collapsible Actions and Action Sets section (with name edit, save, delete)
 * - Slider area: one row per joint name, labels + numeric readout + min/max ticks
 *
 * Data (actions/sets) comes from ActionStore.
 * Emits:
 *  - valuesChanged(): slider moved (send immediately in Follow mode)
 *  - committed(): Execute clicked (send batch in Setup mode)
 *  - notify(text): status lines for MainWindow logging
 */
class SidePanel : public QFrame {
    Q_OBJECT
public:
    explicit SidePanel(QWidget *parent);

    // Read current slider values (angles in degrees)
    QList<int> jointDegrees() const;

    // Mode string exactly as UI shows it ("Mode: Follow"/"Mode: Setup")
    QString modeText() const;
    void setFrame(const QImage img);
    void modeChange(const QString& mode);
    void applyCameraData(const QVector<double>& data);
    void updateJoints(const float thumb, const float index, const float middle, const float ring, const float pinky);
    void loadActions();
    void loadSliders(const QStringList &jointNames);
    void loadActuatorControllor();
    void updateStore(ActionStore *store);

signals:
    void valuesChanged();
    void committed();                  // Execute in Setup mode
    void notify(const QString& msg);   // status/log message

private slots:
    void onAnySliderChanged();
    void onToggleMode();
    void onExecute();
    void onSaveAction();               // begin/cancel inline add
    void onActionNameClicked();        // apply saved action to sliders
    void onFocusChanged(QWidget* old, QWidget* now);
    // void onNameEditingFinished();
    void onToggleActions();
    void onToggleActionSets();
    void onControlChange();
    void onValueEditChanged();

private:
    // Build sections
    void buildControlRow();
    void buildActionsPanel();
    void buildSliderPanel();

    // Helpers
    void applyActionToSliders(int actionIndex);
    void persistStore();
    bool validateNewActionName(const QString& n, const QString& o) const;
    void vialdate(QLineEdit* edit, QPushButton* btn);
    void onDeleteAction(QPushButton* btn);
    void loadSliderModule(QString name, int max, int min);

private:
    // Data
    ActionStore* store_{nullptr};
    int addingRowIndex_{-1}; // index for inline "Save"/"Cancel" row

    // UI: control row
    QWidget* controlRow_{nullptr};
    QPushButton* btnMode_{nullptr};
    QPushButton* btnExecute_{nullptr};
    QPushButton* btnSave_{nullptr};
    QPushButton* btnDelete_{nullptr};
    QPushButton* btnEditName_{nullptr}; // rename current focused action

    // UI: collapsible actions
    QWidget* actionsPanel_{nullptr};
    QWidget* actionListWidget_{nullptr};
    QWidget* actionSetPanel_{nullptr};
    QToolButton* btnCollapseActions_{nullptr};
    QToolButton* btnCollapseSets_{nullptr};

    // UI: Image 
    QLabel* img_label_{nullptr};

    // UI: sliders
    QWidget* sliderPanel_{nullptr};
    QScrollArea* sliderScroll_{nullptr};
    QScrollArea* actionScroll_{nullptr};
    QVBoxLayout* sliderVBox_{nullptr};
    QList<QSlider*> sliders_;
    QList<QLineEdit*>  valueLabels_;  // number next to each slider
    QList<QLabel*>  currValueLabels_;
    QList<QLabel*>  nameLabels_;

    // UI: actuator
    QComboBox* mode_box;

    // cache joint count
    int jointCount_{0};
};
