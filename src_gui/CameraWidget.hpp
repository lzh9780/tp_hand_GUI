#pragma once
#include <QFrame>
#include <QVector>

class QWidget;
class QPushButton;
class QImage;
class QLabel;
class QResizeEvent;
class PyCamWorker;
class TeleOperation;
class QThread;
class QComboBox;

class CameraWidget : public QFrame
{
    Q_OBJECT
public:
    explicit CameraWidget(QWidget *parent = nullptr);
    void onChange(bool on);
    void setFrame(const QImage &img);
    std::vector<std::string> getFullList();
    void stopAll();

protected:
    void resizeEvent(QResizeEvent *e) override;

signals:
    void controlSwitch(const QString &msg);
    void dataUpdate(const QVector<double>&);
    void status(const QString& msg);
    void error(const QString& msg);

private slots:
    void onSwitch();
    void record();
    void onControlSwitch(const QString &msg);
    void dataProcess(const QVector<double> &joints);

private:
    void stopThread();
    void updatePixmap();
    void startCamera();
    void startTeleopration();

private:
    QComboBox* selectBox = nullptr;
    QPushButton *switchBtn;
    QLabel *img_label_{nullptr};
    PyCamWorker* worker_ = nullptr;
    TeleOperation* UDEworker_ = nullptr;
    QThread* workerThread_ = nullptr;

    QImage last_frame_;
    QVector<double> last_data{};
    QVector<double> calibrate_data{};
};
