#pragma once
#include <QFrame>

class QWidget;
class QPushButton;
class QComboBox;
class QImage;
class QLabel;

class CameraWidget : public QFrame
{
    Q_OBJECT
public:
    explicit CameraWidget(QWidget *parent);
    void onChange(bool on);
    void setFrame(const QImage img);

signals:
    void controlSwitch(const QString& msg);

private slots:
    void onSwitch();
    
private:
    QPushButton* switchBtn;
    QLabel* img_label_{nullptr};
};