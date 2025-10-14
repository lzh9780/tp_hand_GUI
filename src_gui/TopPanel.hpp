#pragma once
#include <QFrame>

class QWidget;
class QPushButton;

class TopPanel : public QFrame
{
    Q_OBJECT
public:
    explicit TopPanel(QWidget *parent);

signals:
    void deviceConnect();
    void controlSwitch(const QString& msg);

private slots:
    void onConnect();
    void onSwitch();

private:
    QPushButton* switchBtn;
};