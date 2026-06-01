#pragma once
#include <QFrame>

class QWidget;
class QPushButton;
class QComboBox;
class QCheckBox;

class Connector : public QFrame
{
    Q_OBJECT
public:
    explicit Connector(QWidget *parent, std::vector<std::string> models);
    void onChange(bool on);

signals:
    void deviceConnect(const QString& msg);
    // void controlSwitch(const QString& msg);
    void modelChange(const QString& msg);
    void stateCheck(const Qt::CheckState state);

private slots:
    void onConnect();
    // void onSwitch();
    void onModelChange();

private:
    QPushButton* switchBtn;
    QPushButton* connectBtn;
    QComboBox* model_box;
    QCheckBox* deviceState;
};