#include "../third_party/HandDriver/UDEServer.h"

#include <QTimer>
#include <QObject>
#include <QVector>

class TeleOperation : public QObject {
    Q_OBJECT

public:
    explicit TeleOperation(QObject* parent=nullptr);

public slots:
    void start();   // call after moving this object to a QThread
    void stop();
    void onTick();

signals:
    void newJoint(const QVector<double>&);
    void runningChanged(bool running);
    void status(const QString& msg);
    void error(const QString& msg);

private:
    QTimer* timer_ = nullptr;
    bool running_ = false;

    UDEGloveSDK* sdk = nullptr;
};
