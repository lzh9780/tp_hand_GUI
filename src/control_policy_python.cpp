#include "grip/control_policy_python.hpp"
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <cstdlib>

namespace py = pybind11;
namespace grip {

PythonPolicy::PythonPolicy(const PythonPolicySettings& cfg) : cfg_(cfg) {
    if (cfg_.use_headless_matplotlib) std::setenv("MPLBACKEND", "Agg", 1);
    guard_ = std::make_unique<py::scoped_interpreter>();

    py::module sys = py::module::import("sys");
    sys.attr("path").attr("insert")(0, cfg_.script_dir);

    py::module mod = py::module::import(cfg_.module.c_str());
    py_obj_ = mod.attr(cfg_.class_name.c_str())();
}

PythonPolicy::~PythonPolicy() {
    py_obj_ = py::none();
    guard_.reset();
}

int32_t PythonPolicy::compute(const std::string& joint_key, float target_rad, int32_t current_counts) {
    std::lock_guard<std::mutex> lk(lock_);
    auto out = py_obj_.attr("main")(joint_key, target_rad, current_counts);
    return out.cast<py::tuple>()[1].cast<int32_t>();
}

} // namespace grip
