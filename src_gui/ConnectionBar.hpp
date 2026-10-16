#pragma once
#include <QFrame>
#include <string>
#include <vector>

class QPushButton;
class QComboBox;
class QLabel;
class QSpinBox;

/**
 * ConnectionBar (GUI)
 * -------------------
 * Single row at the top of the window. It is the only non-floating strip and
 * replaces both the old toolbar / panel row and the Connector panel
 * (UI design notes #4, #6, #11):
 *
 *   Model [.....]  Device [0]  Channel [0]  [Connect]  [Enable]  status ...
 *                                                                  ... [Settings]
 *
 * The hand is armed with "Enable" instead of a control-mode combo box: pressing
 * it switches every joint to `joint_position`, "Disable" switches them back to
 * `zero_torque`.
 *
 * The CAN device index / channel are edited here because they are part of the
 * day-to-day workflow, while the rest of the bus configuration (type, clock,
 * bit timing) still lives in the Settings pop-up. Both sides write into the
 * same CanConfig, so edits are mirrored through canConfigChanged()/setCanConfig().
 *
 * Signals:
 *  - deviceConnect(msg): Connect/Disconnect pressed (msg is the button text)
 *  - enableChanged(on):  Enable/Disable pressed (true => joint_position)
 *  - modelChange(msg):   selected model name
 *  - canConfigChanged(...): device index / channel changed
 *  - settingsRequested(): the trailing "Settings" button was pressed
 *  - statusMessage(msg): free-form text to show in the log
 */
class ConnectionBar : public QFrame
{
    Q_OBJECT
public:
    explicit ConnectionBar(QWidget *parent, std::vector<std::string> models);

    void onChange(bool on);
    QString currentModel() const;
    void setStatus(const QString &text);

    /* Enable / Disable button state without re-emitting the signal. */
    bool isEnabled_() const;
    void setEnabledState(bool enabled);

    /* Mirror the CAN device index / channel (kept in sync with the settings). */
    void setDeviceLocation(int device_index, int channel_index);

signals:
    void deviceConnect(const QString &msg);
    void enableChanged(bool enable);
    void modelChange(const QString &msg);
    void canConfigChanged(int device_index, int channel_index);
    void settingsRequested();
    void statusMessage(const QString &msg);

private slots:
    void onConnect();
    void onModelChange();
    void onToggleEnable();

private:
    void emitDeviceLocation();
    void updateEnableButton();

    QComboBox *modelBox_{nullptr};
    QSpinBox *deviceIndex_{nullptr};
    QSpinBox *channelIndex_{nullptr};
    QPushButton *connectBtn_{nullptr};
    QPushButton *enableBtn_{nullptr};
    QLabel *statusLabel_{nullptr};
};
