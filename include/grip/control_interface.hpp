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

#define GRIP_DEBUG_RX 0

class ControlInterface {
public:
    // build with device list and a joint_key lookup
    ControlInterface(const HandSettings& hand_cfg,
                     std::chrono::milliseconds control_period = std::chrono::milliseconds(1));

    ~ControlInterface();

    // bring-up
    void start_bus(const CanDeviceSettings& cfg);
    void stop_all();
    // void run_calibration(const CalibrationSettings& calib);

    // loops
    void start_control_loop();

    // target API (GUI or any producer can call this): radians!
    void submit_target(std::string ctrl_joint, float radians);

    // state
    std::unordered_map<int, Telemetry> snapshot() const;
    JointAngles get_joint_angles() const;

private:
    void receiver_loop();
    void control_loop();
    // bool all_stalled_locked() const;

    void send_broadcast(std::vector<int> devs, MsgKind kind, uint8_t field, const std::vector<int32_t>& values);
    void send_broadcast(std::vector<int> devs, MsgKind kind, uint8_t field, const std::vector<int16_t> &values1, const std::vector<int16_t> &values2);
    void send_single(int dev, MsgKind kind, uint8_t field, int32_t value);
    void send_single(int dev, MsgKind kind, uint8_t field, int16_t value1, int16_t value2);

    void drain_pending_targets_locked();

private:
    // hw
    CanBus bus_{33,0};
    uint32_t bus_channel_{0};

    // model
    std::vector<int> devices_;
    std::unordered_map<std::string, ModuleStruct> hand_;
    std::unordered_map<int, std::string> joint_key_; // Matched the current algorithm. Will be removed later
    std::unordered_map<int, std::string> ctrl_key_; // Matched the current algorithm. Will be removed later
    NativePolicy policy_;
    JointAngles offset_;

    // shared state
    std::unordered_map<int, Telemetry> state_;
    std::unordered_map<std::string, float> targets_rad_;
    JointAngles joint_angles_;

    // threads
    std::thread rx_thread_, ctrl_thread_;
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::atomic<bool> shutting_down_{false};
    bool calibrated_{false};
    std::chrono::milliseconds control_period_{10};

    // target inbox
    std::mutex inbox_mtx_;
    std::deque<std::pair<std::string,float>> inbox_;
};

} // namespace grip
