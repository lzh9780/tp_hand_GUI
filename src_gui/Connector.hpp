#pragma once
#include <QFrame>
#include <vector>
#include <string>

class QWidget;
class QPushButton;
class QComboBox;
class QCheckBox;
class QLabel;
class QGroupBox;

/**
 * Connector (GUI)
 * ---------------
 * Entry point of the application: pick a hand model and open/close the
 * connection to the hardware.
 *
 * Signals:
 *  - deviceConnect("Connect" | "Disconnect")
 *  - modelChange(<model name>)
 *  - stateCheck(check state of the "simulation mode" box)
 */
class Connector : public QFrame
{
    Q_OBJECT
public:
    explicit Connector(QWidget *parent, std::vector<std::string> models);
    void onChange(bool on);

signals:
    void deviceConnect(const QString& msg);
    void modelChange(const QString& msg);
    void stateCheck(const Qt::CheckState state);

private slots:
    void onConnect();
    void onModelChange();

private:
    QPushButton* connectBtn{nullptr};
    QComboBox* model_box{nullptr};
    QCheckBox* deviceState{nullptr};
    QLabel* statusLabel_{nullptr};
};
