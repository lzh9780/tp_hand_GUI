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
 * Single-row connection strip that sits above the work area (replaces the old
 * Connector panel/tab):
 *
 *   Model [.....]  Device [0]  Channel [0]  [Connect]  Not connected
 *
 * The CAN device index / channel are edited here because they are part of the
 * day-to-day workflow, while the rest of the bus configuration (type, clock,
 * bit timing) still lives in the Settings pop-up. Both sides write into the
 * same CanConfig, so edits are mirrored through canConfigChanged()/setCanConfig().
 *
 * Signals:
 *  - deviceConnect(msg): Connect/Disconnect pressed (msg is the button text)
 *  - modelChange(msg):   selected model name
 *  - canConfigChanged(...): device index / channel changed
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

    /* Mirror the CAN device index / channel (kept in sync with the settings). */
    void setDeviceLocation(int device_index, int channel_index);

signals:
    void deviceConnect(const QString &msg);
    void modelChange(const QString &msg);
    void canConfigChanged(int device_index, int channel_index);
    void statusMessage(const QString &msg);

private slots:
    void onConnect();
    void onModelChange();

private:
    void emitDeviceLocation();

    QComboBox *modelBox_{nullptr};
    QSpinBox *deviceIndex_{nullptr};
    QSpinBox *channelIndex_{nullptr};
    QPushButton *connectBtn_{nullptr};
    QLabel *statusLabel_{nullptr};
};
