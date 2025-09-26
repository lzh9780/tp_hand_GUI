#pragma once
#include <unordered_map>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include "can_defs.hpp"
#include "protocol_codec.hpp"
#include "can_bus.hpp"
#include "settings.hpp"

namespace grip {

class ControlInterface {
public:
    using PolicyFn = std::function<int32_t(JointDevice, float, int32_t)>;

    ControlInterface(const std::vector<JointDevice>& devices, PolicyFn policy);

    ~ControlInterface();

    explicit ControlInterface(const std::vector<JointDevice>& devices,
                              PolicyFn policy,
                              std::chrono::milliseconds control_period = std::chrono::milliseconds(1));

    void start_bus(const CanDeviceSettings& bus);
    void stop_all();

    void run_calibration(const CalibrationSettings& calib);
    void start_control_loop();

    void set_target(JointDevice dev, float radians);
    void set_default_targets(float others_rad, float thumb_rad);

    std::unordered_map<JointDevice, Telemetry> snapshot() const;

private:
    void receiver_loop();
    void control_loop();

    bool all_stalled_locked() const;

    void send_broadcast(MsgKind kind, uint8_t field, const std::vector<int32_t>& values);
    void send_single(JointDevice dev, MsgKind kind, uint8_t field, int32_t value);
    void send_multi(MsgKind kind, uint8_t field,
                    const std::vector<std::pair<JointDevice, int32_t>>& items);

private:
    CanBus bus_{33, 0};
    uint32_t bus_channel_{0};

    std::vector<JointDevice> devices_;
    std::unordered_map<JointDevice, Telemetry> state_;
    std::unordered_map<JointDevice, float> targets_rad_;

    PolicyFn policy_fn_;

    std::thread rx_thread_;
    std::thread ctrl_thread_;
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::atomic<bool> shutting_down_{false};
    bool calibrated_{false};
};

} // namespace grip
