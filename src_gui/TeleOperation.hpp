#pragma once

#include <QTimer>
#include <QObject>
#include <QVector>
#include <QImage>
#include <memory>
#include <mutex>
#include <atomic>

class TeleopBase : public QObject
{
    Q_OBJECT
public:
    explicit TeleopBase(QObject *parent = nullptr) : QObject(parent) {};
    virtual ~TeleopBase() { stop(); };

public slots:
    virtual void start() { resetDelivery(); };
    virtual void stop() {};
    virtual void onTick() {};

    // Called by the GUI thread once it has consumed a delivered sample.
    //
    // The workers poll at ~120 Hz and push their samples across threads with a
    // queued connection. When the GUI cannot keep up (it renders the 3D view,
    // updates the sliders and writes to the hand), every unconsumed sample
    // stays queued, so the event queue grows without bound and the process
    // eventually dies. Requiring an acknowledgement caps the queue at a single
    // outstanding sample and turns the pipeline into "latest value wins".
    void acknowledgeJoints() { jointsPending_.store(false, std::memory_order_release); }
    void acknowledgeFrame() { framePending_.store(false, std::memory_order_release); }

signals:
    void newFrame(const QImage &img);
    void newJoint(const QVector<double> &);
    void runningChanged(bool running);
    void status(const QString &msg);
    void error(const QString &msg);

protected:
    // Returns true when the emitter may post the next sample. Call this right
    // before emitting; if it returns false, simply drop the sample and try
    // again on the next tick - a newer one is always on its way.
    bool requestJointDelivery()
    {
        bool expected = false;
        return jointsPending_.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel, std::memory_order_relaxed);
    }

    bool requestFrameDelivery()
    {
        bool expected = false;
        return framePending_.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel, std::memory_order_relaxed);
    }

    // Call from start(): a lost acknowledgement must never stall the pipeline.
    void resetDelivery()
    {
        jointsPending_.store(false, std::memory_order_release);
        framePending_.store(false, std::memory_order_release);
    }

    QTimer *timer_ = nullptr;
    bool running_ = false;

private:
    std::atomic<bool> jointsPending_{false};
    std::atomic<bool> framePending_{false};
};
