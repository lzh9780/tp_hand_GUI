#include "grip/settings.hpp"
#include "grip/control_interface.hpp"
#include <QApplication>
#include "MainWindow.h"
#include <csignal>
#include <pybind11/embed.h>

using namespace grip;

// static JointDevice to_dev(const std::string &n)
// {
//     if (n == "THUMB")
//         return JointDevice::THUMB;
//     if (n == "INDEX_FINGER")
//         return JointDevice::INDEX_FINGER;
//     if (n == "MIDDLE_FINGER")
//         return JointDevice::MIDDLE_FINGER;
//     if (n == "RING_FINGER")
//         return JointDevice::RING_FINGER;
//     if (n == "PINKY_FINGER")
//         return JointDevice::PINKY_FINGER;
//     throw std::runtime_error("Unknown joint " + n);
// }
int main(int argc, char **argv)
{
    const std::string cfg_path = (argc > 1) ? argv[1] : "configs/config.yaml";
    AppSettings cfg = load_app_settings(cfg_path);

    // std::vector<JointDevice> devs;
    // devs.reserve(cfg.joints.size());
    // std::unordered_map<JointDevice, std::string> key_of;
    // for (auto &n : cfg.joints)
    // {
    //     auto d = to_dev(n);
    //     devs.push_back(d);
    //     // joint key mapping consistent with policy tables:
    //     if (n == "THUMB")
    //         key_of[d] = "thumb_mcp";
    //     else if (n == "INDEX_FINGER")
    //         key_of[d] = "index_finger_mcp";
    //     else if (n == "MIDDLE_FINGER")
    //         key_of[d] = "middle_finger_mcp";
    //     else if (n == "RING_FINGER")
    //         key_of[d] = "ring_finger_mcp";
    //     else if (n == "PINKY_FINGER")
    //         key_of[d] = "pinky_mcp";
    // }

    // auto *ctrl = new ControlInterface(devs, key_of, cfg.native_policy, std::chrono::milliseconds(1));
    // ctrl->start_bus(cfg.can_device);
    // ctrl->run_calibration(cfg.calibration);
    // ctrl->start_control_loop();

    pybind11::scoped_interpreter guard{};
    pybind11::gil_scoped_release release;

    QApplication app(argc, argv);

    MainWindow window(cfg);
    window.show();

    return app.exec();

    // std::signal(SIGINT, +[](int){});
#if defined(__linux__)
    pause();
#else
    while (true)
        std::this_thread::sleep_for(std::chrono::seconds(1));
#endif

    // ctrl->stop_all();
    return 0;
}
