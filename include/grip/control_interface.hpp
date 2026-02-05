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

namespace grip {

#define GRIP_DEBUG_RX 0

class ControlInterface {
public:
    // build with device list and a joint_key lookup
    ControlInterface(const HandSettings& hand_cfg,
                     std::chrono::milliseconds control_period = std::chrono::milliseconds(1));
    
    ControlInterface(const ActuatorSettings& actuator_cfg,
                     std::chrono::milliseconds control_period = std::chrono::milliseconds(1));

    ~ControlInterface();

    // bring-up
    void start_bus(const CanDeviceSettings& cfg);
    void stop_all();
    void run_calibration();

    // loops
    void start_control_loop();

    // target API (GUI or any producer can call this): radians!
    void submit_target(std::string ctrl_joint, float radians);

    // state
    std::unordered_map<int, Telemetry> snapshot() const;
    int get_board_state() const;
    void set_module_control_mode(std::string module, int mode);
    HandStateMap get_hand_runtime_data();

private:
    void receiver_loop();
    void control_loop();
    
    bool all_stalled_locked() const;

    void send_to_broad(std::vector<CmdStruct> cmd);

    void drain_pending_targets_locked();
    Telemetry messageDecode(const uint8_t *p);

private:
    // hw
    CanBus bus_{33,0};
    uint32_t bus_channel_{0};

    // model
    std::vector<int> motors_;
    HandStateMap hand_{};

    // shared state
    int device_type = 0;
    std::unordered_map<int, Telemetry> state_;
    std::unordered_map<std::string, float> targets_rad_;
    int board_state_;
    std::string mode_{"Position"};
    // std::unordered_map<std::string, ControlModule> ctrl_mod_;

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
