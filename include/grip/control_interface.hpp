#pragma once
#include <unordered_map>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <deque>
#include "can_defs.hpp"
#include "protocol_codec.hpp"
#include "can_bus.hpp"
#include "settings.hpp"
#include "control_policy_native.hpp"

namespace grip {

// #define GRIP_DEBUG_RX 1

class ControlInterface {
public:
    // build with device list and a joint_key lookup
    ControlInterface(const std::vector<JointDevice>& devices,
                     const std::unordered_map<JointDevice, std::string>& joint_key_of,
                     const NativePolicySettings& policy_cfg,
                     std::chrono::milliseconds control_period = std::chrono::milliseconds(1));

    ~ControlInterface();

    // bring-up
    void start_bus(const CanDeviceSettings& cfg);
    void stop_all();
    void run_calibration(const CalibrationSettings& calib);

    // loops
    void start_control_loop();

    // target API (GUI or any producer can call this): radians!
    void submit_target(JointDevice dev, float radians);
    void set_default_targets(float others_rad, float thumb_rad);

    // state
    std::unordered_map<JointDevice, Telemetry> snapshot() const;

private:
    void receiver_loop();
    void control_loop();
    bool all_stalled_locked() const;

    void send_broadcast(MsgKind kind, uint8_t field, const std::vector<int32_t>& values);
    void send_single(JointDevice dev, MsgKind kind, uint8_t field, int32_t value);

    void drain_pending_targets_locked();

private:
    // hw
    CanBus bus_{33,0};
    uint32_t bus_channel_{0};

    // model
    std::vector<JointDevice> devices_;
    std::unordered_map<JointDevice, std::string> joint_key_;  // device -> joint key used by policy
    NativePolicy policy_;

    // shared state
    std::unordered_map<JointDevice, Telemetry> state_;
    std::unordered_map<JointDevice, float> targets_rad_;

    // threads
    std::thread rx_thread_, ctrl_thread_;
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::atomic<bool> shutting_down_{false};
    bool calibrated_{false};
    std::chrono::milliseconds control_period_{10};

    // target inbox
    std::mutex inbox_mtx_;
    std::deque<std::pair<JointDevice,float>> inbox_;
};

} // namespace grip
