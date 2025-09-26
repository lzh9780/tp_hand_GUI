#pragma once
#include <string>
#include <memory>
#include <mutex>
#include <pybind11/embed.h>
#include "settings.hpp"

namespace grip {

class PythonPolicy {
public:
    explicit PythonPolicy(const PythonPolicySettings& cfg);
    ~PythonPolicy();

    int32_t compute(const std::string& joint_key, float target_rad, int32_t current_counts);

private:
    PythonPolicySettings cfg_;
    std::unique_ptr<pybind11::scoped_interpreter> guard_;
    pybind11::object py_obj_;
    std::mutex lock_;
};

} // namespace grip
