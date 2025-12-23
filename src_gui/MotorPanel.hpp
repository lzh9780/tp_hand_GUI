#pragma once
#include <QFrame>
#include <unordered_map>
#include "grip/settings.hpp"

class QVBoxLayout;

class MotorPanel : public QFrame
{
    Q_OBJECT
public:
    explicit MotorPanel(QWidget *parent = nullptr);
    void load(grip::HandSettings hand);
    void removeAll();

private:
    QVBoxLayout* root;
};