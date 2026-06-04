#pragma once
#include <QFrame>
#include <QString>
#include <QCheckBox>
#include <QList>
#include <unordered_map>
#include "grip/settings.hpp"

class QVBoxLayout;
class QComboBox;

class MotorPanel : public QFrame
{
    Q_OBJECT
public:
    explicit MotorPanel(QWidget *parent = nullptr);
    void load(grip::HandSettings hand);
    bool checkJointStandalone(QString joint_name);
    void removeAll();
    void applyModeChange(QString name, QString mode);

signals:
    void mode_change(const QString& module, const QString& control_mode);
    // void single_control(const QString& name, const bool single);

private:
    QVBoxLayout* root;
    QList<QString> names; 
    QList<QCheckBox*> single_checkbox;
    QList<QComboBox*> mode_selecters;
};