#pragma once
#include <QFrame>

class QWidget;
class QPushButton;
class QImage;
class QLabel;
class QResizeEvent;

class CameraWidget : public QFrame
{
    Q_OBJECT
public:
    explicit CameraWidget(QWidget *parent = nullptr);
    void onChange(bool on);
    void setFrame(const QImage &img);

protected:
    void resizeEvent(QResizeEvent *e) override;

signals:
    void controlSwitch(const QString &msg);

private slots:
    void onSwitch();

private:
    void updatePixmap();

private:
    QPushButton *switchBtn;
    QLabel *img_label_{nullptr};

    QImage last_frame_;
};
