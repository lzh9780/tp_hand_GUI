#include "grip/settings.hpp"
#include "grip/control_interface.hpp"
#include "grip/control_policy_native.hpp"
#include <unordered_map>
#include <csignal>

using namespace grip;

static JointDevice parse_device(const std::string& name) {
    if (name == "THUMB")         return JointDevice::THUMB;
    if (name == "INDEX_FINGER")  return JointDevice::INDEX_FINGER;
    if (name == "MIDDLE_FINGER") return JointDevice::MIDDLE_FINGER;
    if (name == "RING_FINGER")   return JointDevice::RING_FINGER;
    if (name == "PINKY_FINGER")  return JointDevice::PINKY_FINGER;
    throw std::runtime_error("Unknown device name: " + name);
}

int main(int argc, char** argv) {
    const std::string cfg_path = (argc > 1) ? argv[1] : "config.yaml";
    AppSettings s = load_settings(cfg_path);

    std::vector<JointDevice> devices;
    std::unordered_map<JointDevice,std::string> key_of;
    for (auto& b : s.joints) {
        auto id = parse_device(b.device);
        devices.push_back(id);
        key_of[id] = b.key;
    }

    NativePolicy policy_impl(s.native_policy);
    ControlInterface::PolicyFn policy = [&policy_impl, &key_of](JointDevice d, float tgt, int32_t pos){
        return policy_impl.compute(key_of.at(d), tgt, pos);
    };

    ControlInterface ctrl(devices, policy);
    ctrl.start_bus(s.can_device);
    ctrl.run_calibration(s.calibration);
    ctrl.set_default_targets(s.hold_targets.others_rad, s.hold_targets.thumb_rad);
    ctrl.start_control_loop();

    std::signal(SIGINT, +[](int){});
#if defined(__linux__)
    pause();
#else
    while (true) std::this_thread::sleep_for(std::chrono::seconds(1));
#endif

    ctrl.stop_all();
    return 0;
}
