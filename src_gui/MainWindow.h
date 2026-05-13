#pragma once
#include <mutex>
#include <QMainWindow>
#include <vector>
#include "grip/settings.hpp"
#include "grip/control_interface.hpp"
#include "ActionStore.hpp"

#include "OgreWidget.h"
#include "MotorPanel.hpp"
#include "PlotPanel.hpp"
#include "grip/data_def.hpp"

class SidePanel;
class Connector;
class QTextEdit;
class QThread;
class QString;
class QTabWidget;
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

protected:
    void resizeEvent(QResizeEvent *event) override;

signals:
    // void requestStop();

private slots:
    void onUpperTabCloseRequested(int idx);
    void onLowerTabCloseRequested(int idx);
    void onSidePanelValuesChanged();   // Follow mode → immediate send
    void onSidePanelCommitted();       // Setup mode → Execute
    void onSidePanelNotify(const QString& msg);
    void onConnectorDeviceConnect(const QString &msg);
    void onConnectorStateCheck(Qt::CheckState state);
    void readJointAngles();
    void onConnectorModelChange(const QString &model_name);
    void onActionPanelActionUpdate(const QString &msg);
    void onSidePanelAddAction(const QString &msg);
    void onJointPanelControlModeChange(const QString &module_name, const QString &control_mode);

private:
    void buildUI();
    void openCameraTab();
    void openConnectorTab();
    void openLogTab();
    void openUrdfLoaderTab();
    void openMotorControlTab();
    void openDiagram();
    void onDeviceConnect();
    void onDeviceDisconnect();
    void openActionPanelTab();
    std::unordered_map<std::string, float> dofsToJoints(std::string dof_name, float value);
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

    // widgets
    QWidget* mainContent;
    QWidget* central;
    SidePanel* side_{nullptr};
    QTextEdit* log_{nullptr};
    OgreWidget* ogre_;
    MotorPanel* motor_;
    ActionPanel* actionFlow_;
    Connector* connect_;
    CameraWidget* camera_;
    // PlotPanel* plot_;
    std::string config_path_;
    std::string action_path_;
    std::string urdf_path_;
    QStringList dof_names;
    QTimer* read_timer_ = nullptr;

    QTabWidget* upperTabs;
    QTabWidget* lowerTabs;

    std::mutex mtx_;
};
