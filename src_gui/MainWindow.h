#pragma once
#include <mutex>
#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <vector>
#include "grip/settings.hpp"
#include "grip/control_interface.hpp"
#include "ActionStore.hpp"

#include "DraggableTabWidget.hpp"
#include "OgreWidget.h"
#include "MotorPanel.hpp"
#include "PlotPanel.hpp"
#include "TactileVisual.hpp"
#include "grip/data_def.hpp"

class SidePanel;
class ConnectionBar;
class SettingsWindow;
class QTextEdit;
class QThread;
class QString;
class QWidget;
class QToolBar;
class QAction;
class CameraWidget;
class ActionPanel;

Q_DECLARE_METATYPE(QVector<float>)

namespace grip { class ControlInterface; }

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(const grip::AppSettings& cfg,
               QWidget* parent=nullptr);
    ~MainWindow() override;

private slots:
    void onUpperTabCloseRequested(int idx);
    void onSidePanelValuesChanged();   // Follow mode → immediate send
    void onSidePanelCommitted();       // Setup mode → Execute
    void onSidePanelNotify(const QString& msg);
    void onConnectorDeviceConnect(const QString &msg);
    void onConnectionBarCanConfigChanged(int device_index, int channel_index);
    void onSimulationModeChanged(bool simulation);
    void onCanConfigChanged(int device_type, int device_index, int channel_index,
                            int core_clock_hz, bool termination_enabled,
                            int arb_tseg1, int arb_tseg2, int arb_sjw, int arb_brp,
                            int data_tseg1, int data_tseg2, int data_sjw, int data_brp);
    void onFilterChanged(bool enabled, double freq, double min_cutoff,
                         double beta, double d_cutoff);
    void onFilterReset();
    void onModel3DConfigChanged(bool dual, bool use_feedback);
    void openSettings();

private:
    /* 3D model helpers: load single/dual URDF and pick the driving data source. */
    void loadModel3D();
    bool model3DShowsSource(const char *source) const;
    bool model3DDual() const;
    void readJointAngles();
    void onConnectorModelChange(const QString &model_name);
    void onActionPanelActionUpdate(const QString &msg);
    void onJointPanelControlModeChange(const QString &module_name, const QString &control_mode);
    void onJointPanelVelRefModeChange(const QString &module_name, const int vel_ref);
    void onJointPanelTorqueRefModeChange(const QString &module_name, const int torque_ref);
    void onSidePanelControlChanged(const QString &name, const QString &mode);

private:
    void buildUI();
    void buildToolBar();
    void buildLogPanel();

    // Panel shortcuts. Every helper reuses an existing tab instead of
    // appending a duplicate one (see addOrShowTab).
    void openCameraTab();
    void openUrdfLoaderTab();
    void openMotorControlTab();
    void openActionPanelTab();
    void openDiagram();
    void openTactileImage();
    void toggleLogPanel();

    void onDeviceConnect();
    void onDeviceDisconnect();

    void addOrShowTab(QWidget* widget, const QString& title);
    void appendLog(const QString& msg);
    void setConnectedStatus(bool connected);

    std::unordered_map<std::string, float> dofsToJoints(std::string dof_name, float value, bool is_hall);
    std::unordered_map<std::string, float> jointsToDofs(std::string joint_name, float value);

private:
    // program modules
    std::vector<int> devices_;
    grip::ControlInterface* ctrl_{nullptr};
    const grip::AppSettings cfg_;
    grip::DeviceSettings dev_cfg_{};
    grip::HandSettings hand_cfg_{};
    grip::ActuatorSettings act_cfg_{};

    std::unordered_map<std::string, std::string> joints_inverted_index;
    std::unordered_map<int, std::string> dof_map;

    // params
    bool withCan = true;
    grip::CanDeviceSettings can_cfg_{};

    // widgets
    QWidget* central{nullptr};
    SidePanel* side_{nullptr};
    QTextEdit* log_{nullptr};
    QWidget* logPanel_{nullptr};
    OgreWidget* ogre_{nullptr};
    MotorPanel* motor_{nullptr};
    ActionPanel* actionFlow_{nullptr};
    ConnectionBar* connBar_{nullptr};
    TactileVisual* tactile_{nullptr};
    CameraWidget* camera_{nullptr};

    QToolBar* toolBar_{nullptr};
    QAction* actModel3D_{nullptr};
    QAction* actActionSets_{nullptr};
    QAction* actJointModes_{nullptr};
    QAction* actDiagram_{nullptr};
    QAction* actHandDetect_{nullptr};
    QAction* actTactile_{nullptr};
    QAction* actSettings_{nullptr};
    QAction* actLog_{nullptr};

    SettingsWindow* settingsWindow_{nullptr};

    /* Last applied 3D-model choice, so unrelated "Apply" clicks do not rebuild it. */
    bool model3DDual_{true};
    bool model3DUseFeedback_{true};

    DraggableTabWidget* upperTabs{nullptr};

    std::string config_path_;
    std::string action_path_;
    std::string urdf_path_;
    QString selectedModel_;
    QStringList dof_names;
    QTimer* read_timer_ = nullptr;

    std::mutex mtx_;
};
