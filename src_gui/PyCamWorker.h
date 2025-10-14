#pragma once
#pragma push_macro("slots")
#undef slots
#include <Python.h>
#pragma pop_macro("slots")
#include <pybind11/embed.h>
#include <pybind11/numpy.h>
// #define PYBIND11_NO_KEYWORDS
#include <QObject>
#include <QImage>
#include <QTimer>
#include <memory>
#include <QVector>


namespace py = pybind11;

class PyCamWorker : public QObject {
    Q_OBJECT
public:
    explicit PyCamWorker(QObject* parent=nullptr);
    ~PyCamWorker() override;

public slots:
    void start();   // call after moving this object to a QThread
    void stop();
    void onTick();

signals:
    void newFrame(const QImage& img);
    void newJoint(const QVector<double>&);
    void status(const QString& msg);
    void error(const QString& msg);

private:
    // static QImage bgrMatToQImage(const uint8_t* data, int rows, int cols, int step);

    bool running_ = false;
    QTimer* timer_ = nullptr;

    // Python side
    std::unique_ptr<py::scoped_interpreter> interp_;
    py::object get_latest_fn;
    py::object get_joint_fn;
    py::object stop_fn_;
    py::object start_fn;
    QImage frameBGR_;
};