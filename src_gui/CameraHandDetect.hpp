#pragma once
#pragma push_macro("slots")
#undef slots
#include <Python.h>
#pragma pop_macro("slots")
#include <pybind11/embed.h>
#include <pybind11/numpy.h>

#include "TeleOperation.hpp"

namespace py = pybind11;

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
