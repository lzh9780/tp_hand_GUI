#pragma once
#include <QFrame>

class QWidget;
class QPushButton;
class QComboBox;

class TopPanel : public QFrame
{
    Q_OBJECT
public:
    explicit TopPanel(QWidget *parent, std::vector<std::string> models);
    void onChange(bool on);

signals:
    void deviceConnect();
    void controlSwitch(const QString& msg);
    void modelChange(const QString& msg);

private slots:
    void onConnect();
    void onSwitch();
    void onModelChange();

private:
    QPushButton* switchBtn;
    QPushButton* connectBtn;
    QComboBox* model_box;
};