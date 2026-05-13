#pragma once
#include <unordered_map>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <deque>
#include "interface.h"
#include "settings.hpp"
#include "data_def.hpp"

namespace grip {

#define GRIP_DEBUG_RX 0

class ControlInterface {
public:
    // build with device list and a joint_key lookup
    ControlInterface(const HandSettings& hand_cfg,
                     std::chrono::milliseconds control_period = std::chrono::milliseconds(1));

    ~ControlInterface();

    // bring-up
    void start_bus();
    void stop_all();

    // loops
    void start_control_loop();

    // target API (GUI or any producer can call this): radians!
    void submit_target(std::string ctrl_joint, float radians);
    void submit_control_mode(std::string ctrl_joint, std::string mode);

    // state
    HandStateMap snapshot() const;
    int get_board_state() const;
    void set_module_control_mode(std::string module, int mode);

private:
    void receiver_loop();
    void control_loop();

    void drain_pending_targets_locked();

private:
    // hw
    grip_control_t *ctrl = nullptr;
    uint32_t bus_channel_{0};

    // model
    std::vector<int> motors_;

    // shared state
    bool data_updated{false};
    int num_joint = 0;
    int device_type = 0;
    HandStateMap state_;
    std::unordered_map<std::string, float> targets_rad_;
    std::unordered_map<std::string, int> control_mode_;
    int board_state_;
    std::string mode_{"Position"};
    std::unordered_map<int, std::string> joint_names_;

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
    std::deque<std::pair<std::string,std::string>> mode_inbox_;
};

} // namespace grip
