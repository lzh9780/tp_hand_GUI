#pragma once
#include <unordered_map>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <deque>
#include "hand_interface.hpp"
#include "settings.hpp"
#include "data_def.hpp"
#include "one_euro_filter.hpp"

namespace grip {

#define GRIP_DEBUG_RX 0

class ControlInterface {
public:
    // build with device list and a joint_key lookup
    ControlInterface(const HandSettings& hand_cfg,
                     std::chrono::milliseconds control_period = std::chrono::milliseconds(1));

    ~ControlInterface();

    // bring-up
    void start_bus(const CanDeviceSettings &cfg);
    void stop_all();

    // loops
    void start_control_loop();

    // target API (GUI or any producer can call this). `value` is the raw slider
    // count: in angle control modes it is 0.01 deg (deg*100, matching the protocol
    // ×100 encoding); in hall-counts mode it is the raw hall count.
    void submit_target(std::string ctrl_joint, float value);
    void submit_vel_ref(std::string ctrl_joint, int vel);
    void submit_torque_ref(std::string ctrl_joint, int torque);
    void submit_control_mode(std::string ctrl_joint, std::string mode);

    // device identity — resolved from the first CAN frame / SDO during start_bus()
    int hand_id() const;              /* slaveID (devID & 0x1F) */
    uint32_t dev_id() const;          /* full devID (handID | slaveID) */
    std::string hand_side() const;    /* "left" / "right" / "unknown" */

    // state
    HandStateMap snapshot() const;
    /* Latest bus telemetry (voltage / current / temperatures / fault word). */
    BusInfo bus_info() const;
    int get_board_state() const;
    void set_module_control_mode(std::string module, int mode);

    // One Euro Filter for the data received from CAN
    void set_filter_enabled(bool on);
    bool filter_enabled() const { return filter_enabled_; }
    void set_filter_params(double freq, double min_cutoff, double beta, double d_cutoff);
    void reset_filters();

private:
    void receiver_loop();
    void control_loop();

    void drain_pending_targets_locked();

    /* lazily create a per-signal One Euro Filter */
    OneEuroFilter &get_filter(std::unordered_map<std::string, OneEuroFilter> &m,
                              const std::string &name);

private:
    // hw
    HandInterface ctrl_;
    uint32_t bus_channel_{0};

    // model
    std::vector<int> motors_;

    // shared state
    int num_joint = 0;
    int device_type = 0;
    HandStateMap state_;
    BusData bus_info_; /* updated by receiver_loop(), guarded by mtx_ */
    std::unordered_map<std::string, float> target_values_; // raw slider target counts
    std::unordered_map<std::string, int> targets_vel_;
    std::unordered_map<std::string, int> targets_torque_;
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
    std::deque<std::pair<std::string,int>> vel_inbox_;
    std::deque<std::pair<std::string,int>> torque_inbox_;
    std::deque<std::pair<std::string,std::string>> mode_inbox_;

    // One Euro Filters for CAN feedback (one per signal, keyed by name)
    bool filter_enabled_ = true;
    double f_freq_ = 100.0, f_min_cutoff_ = 0.5, f_beta_ = 0.05, f_d_cutoff_ = 1.0;
    std::unordered_map<std::string, OneEuroFilter> joint_pos_filters_;
    std::unordered_map<std::string, OneEuroFilter> motor_pos_filters_;
    std::unordered_map<std::string, OneEuroFilter> torque_filters_;
    std::unordered_map<std::string, OneEuroFilter> tip_tactile_filters_;
    std::unordered_map<std::string, OneEuroFilter> tip_prox_filters_;
};

} // namespace grip
