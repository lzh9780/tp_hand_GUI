#pragma once

#include <QTimer>
#include <QObject>
#include <QVector>
#include <QImage>
#include <memory>
#include <mutex>

class TeleopBase : public QObject
{
    Q_OBJECT
public:
    explicit TeleopBase(QObject *parent = nullptr) : QObject(parent) {};
    virtual ~TeleopBase() { stop(); };

public slots:
    virtual void start() {};
    virtual void stop() {};
    virtual void onTick() {};

signals:
    void newFrame(const QImage &img);
    void newJoint(const QVector<double> &);
    void runningChanged(bool running);
    void status(const QString &msg);
    void error(const QString &msg);

protected:
    QTimer *timer_ = nullptr;
    bool running_ = false;
};
