#pragma once
#include <QMainWindow>
#include <vector>
#include "grip/can_defs.hpp"
#include "grip/settings.hpp"
#include "ActionStore.hpp"

class SidePanel;
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

private:
    void buildUI(const QStringList& jointNames);

private:
    std::vector<grip::JointDevice> devices_;
    grip::ControlInterface* ctrl_{nullptr};

    SidePanel* side_{nullptr};
    QTextEdit* log_{nullptr};
};
