#pragma once
#pragma push_macro("slots")
#undef slots
#include <Python.h>
#pragma pop_macro("slots")
#include <pybind11/embed.h>
#include <pybind11/numpy.h>

#include "../third_party/HandDriver/UDEServer.h"

#include <QTimer>
#include <QObject>
#include <QVector>
#include <QImage>
#include <memory>

namespace py = pybind11;

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

class UDETeleop : public TeleopBase
{
    Q_OBJECT
public:
    explicit UDETeleop(QObject *parent = nullptr);
    ~UDETeleop() { stop(); };

public slots:
    void start() override;
    void stop() override;
    void onTick() override;

private:
    UDEGloveSDK *sdk = nullptr;
};

class PyCamWorker : public TeleopBase
{
    Q_OBJECT
public:
    explicit PyCamWorker(QObject *parent = nullptr);
    ~PyCamWorker() { stop(); };

public slots:
    void start() override;
    void stop() override;
    void onTick() override;

private:
    // Python side
    // std::unique_ptr<py::scoped_interpreter> interp_;
    py::object get_latest_fn;
    py::object get_joint_fn;
    py::object get_wrist_joint_fn;
    py::object stop_fn_;
    py::object start_fn;
    QImage frameBGR_;
};
