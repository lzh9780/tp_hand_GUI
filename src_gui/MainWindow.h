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
#include "LogLevel.hpp"

#include "OgreWidget.h"
#include "PlotPanel.hpp"
#include "grip/data_def.hpp"

class SidePanel;
class ConnectionBar;
class SettingsWindow;
class SideDock;
class MainWidget;
class LogPanel;
class QThread;
class QString;
class QWidget;
class CameraWidget;
class ActionPanel;
class TactilePanel;

Q_DECLARE_METATYPE(QVector<float>)

namespace grip { class ControlInterface; }

/**
 * MainWindow
 * ----------
 * Owns the application state and the widgets; the window layout itself lives in
 * MainWidget (connection bar on top, 3D model in the middle, panel columns at
 * the two edges).
 *
 * Reference: UI_Design.md
 *  - no menu bar / tool bar: the only chrome is the single connection row and
 *    the "Settings" button at its end;
 *  - the 3D model is the central background, never a dock tab;
 *  - the right column hosts the plots, the tactile pairs and the log (bus
 *    telemetry); the left column hosts joint control, tracking and actions;
 *  - Enable/Disable on the connection bar switches every joint between
 *    `joint_position` and `zero_torque`, so no per-joint control-mode UI exists.
 */
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(const grip::AppSettings& cfg,
               QWidget* parent=nullptr);
    ~MainWindow() override;

private slots:
    void onSidePanelValuesChanged();   // slider moved -> send immediately
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
    void onModel3DConfigChanged(bool use_feedback);
    void openSettings();

    // Connection-bar Enable/Disable: whole hand <-> joint_position / zero_torque.
    void onEnableChanged(bool enable);

private:
    /* 3D model helpers. The scene holds a single model driven by one data
       source (command targets or measured feedback). */
    void loadModel3D();
    bool model3DShowsSource(const char *source) const;
    void readJointAngles();
    void onConnectorModelChange(const QString &model_name);
    void onActionPanelActionUpdate(const QString &msg);

private:
    void buildUI();
    void buildDocks();
    QWidget *buildLogPanel();

    // Panel shortcuts; each brings its dock tab to the front (and restores the
    // column if it was collapsed).
    void showCameraPanel();
    void showPlotPanel();
    void showTactilePanel();
    void showLogPanel();
    // Collapse / restore the two columns, leaving only their tab strips.
    void toggleLeftDock();
    void toggleRightDock();

    void onDeviceConnect();
    void onDeviceDisconnect();

    /* Log helpers: `level` drives the tag and colour written by the LogPanel. */
    void appendLog(const QString& msg);
    void appendLog(LogLevel level, const QString& msg);
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
    /* tactile config sensor name -> HandStateMap module key carrying its data. */
    std::unordered_map<std::string, std::string> tactileModule_;

    // params
    bool withCan = true;
    grip::CanDeviceSettings can_cfg_{};

    // widgets
    MainWidget* canvas_{nullptr};
    SideDock* leftDock_{nullptr};
    SideDock* rightDock_{nullptr};
    LogPanel* logPanel_{nullptr};
    SidePanel* side_{nullptr};
    OgreWidget* ogre_{nullptr};
    ActionPanel* actionFlow_{nullptr};
    PlotPanel* plotPanel_{nullptr};
    ConnectionBar* connBar_{nullptr};
    TactilePanel* tactile_{nullptr};
    CameraWidget* camera_{nullptr};

    SettingsWindow* settingsWindow_{nullptr};

    /* Last applied 3D-model choice, so unrelated "Apply" clicks do not rebuild it. */
    bool model3DUseFeedback_{false};

    /* Edge detector for the bus fault, so it is logged on change only. */
    bool busFaultActive_{false};

    /* Live feed for the diagram panel: independent of the 10 ms feedback loop so
       the plots keep sampling at their own rate. */
    QTimer* plot_timer_{nullptr};

    std::string config_path_;
    std::string action_path_;
    std::string urdf_path_;
    QString selectedModel_;
    QStringList dof_names;
    QTimer* read_timer_ = nullptr;

    std::mutex mtx_;
};
