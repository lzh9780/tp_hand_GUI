#pragma once
#include <QMainWindow>
#include <vector>
#include <QThread>
#include "grip/can_defs.hpp"
#include "grip/settings.hpp"
#include "ActionStore.hpp"
#include "PyCamWorker.h"
#include "OgreWidget.h"
#include "MotorPanel.hpp"

class SidePanel;
class Connector;
class QTextEdit;
class QThread;
class QString;
class QTabWidget;
class CameraWidget;

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
    void onConnectorDeviceConnect();
    void onCameraWidgetControlSwitch(const QString &msg);
    void readJointAngles();
    void onConnectorModelChange(const QString &model_name);
    // void onSidePanelControlModeChaneg(const QString &module_name, const QString &control_mode);

private:
    void buildUI();
    void openCameraTab();
    void openConnectorTab();
    void openLogTab();
    void openUrdfLoaderTab();
    void openMotorControlTab();
    void openDiagram();

private:
    std::vector<int> devices_;
    grip::ControlInterface* ctrl_{nullptr};
    const grip::AppSettings cfg_;
    grip::DeviceSettings dev_cfg_;
    grip::HandSettings hand_cfg_;
    grip::ActuatorSettings act_cfg_;

    QWidget* mainContent;
    QWidget* central;
    SidePanel* side_{nullptr};
    QTextEdit* log_{nullptr};
    OgreWidget* ogre_;
    MotorPanel* motor_;
    PyCamWorker* worker_ = nullptr;
    QThread* workerThread_ = nullptr;
    Connector* connect_;
    CameraWidget* camera_;
    std::string config_path_;
    std::string action_path_;
    QStringList joint_names;

    QTabWidget* upperTabs;
    QTabWidget* lowerTabs;
};
