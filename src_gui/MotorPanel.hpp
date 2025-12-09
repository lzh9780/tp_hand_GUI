#pragma once
#include <QFrame>
#include <unordered_map>
#include "grip/settings.hpp"

class QWidget;
class QPushButton;
class QComboBox;

class MotorPanel : public QFrame
{
    Q_OBJECT
public:
    explicit MotorPanel(QWidget *parent);
//     void onChange(bool on);
    void load(grip::HandSettings hand);

// signals:
//     void deviceConnect();
//     void controlSwitch(const QString& msg);
//     void modelChange(const QString& msg);

// private slots:
//     void onConnect();
//     void onSwitch();
//     void onModelChange();

// private:
//     QPushButton* switchBtn;
//     QPushButton* connectBtn;
//     QComboBox* model_box;
};