#pragma once
#include <QMainWindow>
#include <vector>
#include "grip/can_defs.hpp"
#include "grip/settings.hpp"
#include "ActionStore.hpp"

class SidePanel;
class TopPanel;
class QTextEdit;

namespace grip { class ControlInterface; }

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(const grip::AppSettings& cfg,
               const std::vector<grip::JointDevice>& devices,
               grip::ControlInterface* ctrl,
               QWidget* parent=nullptr);

protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onSidePanelValuesChanged();   // Follow mode → immediate send
    void onSidePanelCommitted();       // Setup mode → Execute
    void onSidePanelNotify(const QString& msg);
    void onTopPanelDeviceConnect();

private:
    void buildUI(const QStringList& jointNames);

private:
    std::vector<grip::JointDevice> devices_;
    grip::ControlInterface* ctrl_{nullptr};
    const grip::AppSettings cfg_;

    QWidget* mainContent;
    QWidget* central;
    SidePanel* side_{nullptr};
    TopPanel* top_{nullptr};
    QTextEdit* log_{nullptr};
};
