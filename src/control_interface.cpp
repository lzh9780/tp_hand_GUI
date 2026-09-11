#include "grip/control_interface.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <cmath>
#include <stdexcept>
#include <cstring>

namespace grip
{

    ControlInterface::ControlInterface(const HandSettings &hand_cfg,
                                       std::chrono::milliseconds period)
        : control_period_(period)
    {
        for (const auto &[key, value] : hand_cfg.structs)
        {
            // using config data to init state structure
            JointGroup jp;
            for (const auto &joint : value)
            {
                if (joint.id == -1)
                    continue;
                DevState j;
                jp.joints.emplace(joint.name, j);
                DevState m;
                jp.motors.emplace(joint.name, m);
                jp.control_mode.emplace(joint.name, 0);
                // add joint name index
                joint_names_.emplace(joint.id, joint.name);
            }
            state_.emplace(key, jp);
        }
        device_type = 0;
        num_joint = joint_names_.size();
    }

    ControlInterface::~ControlInterface()
    {
        stop_all();
        try
        {
            ctrl_.close_bus();
        }
        catch (...)
        {
        }
    }

    void ControlInterface::start_bus(const CanDeviceSettings &cfg)
    {
        if (ctrl_.start_bus(cfg) == Status::Ok)
            std::cout << "Can device open successfully: devID = 0x" << std::hex << ctrl_.dev_id()
                      << std::dec << " (slave id " << ctrl_.hand_id() << "), hand side = "
                      << ctrl_.hand_side() << "\n";
        else
        {
            std::string error = ctrl_.last_error();
            ctrl_.close_bus();
            throw std::runtime_error(error);
        }
        reset_filters();
        rx_thread_ = std::thread([this]
                                 { receiver_loop(); });
    }

    void ControlInterface::stop_all()
    {
        shutting_down_ = true;
        cv_.notify_all();
        if (rx_thread_.joinable())
            rx_thread_.join();
        if (ctrl_thread_.joinable())
            ctrl_thread_.join();
    }

    void ControlInterface::start_control_loop()
    {
        ctrl_thread_ = std::thread([this]
                                   { control_loop(); });
    }

    void ControlInterface::submit_target(std::string ctrl_joint, float value)
    {
        std::lock_guard<std::mutex> ql(inbox_mtx_);
        inbox_.emplace_back(ctrl_joint, value);
    }

    void ControlInterface::submit_control_mode(std::string ctrl_joint, std::string mode)
    {
        std::lock_guard<std::mutex> ql(inbox_mtx_);
        mode_inbox_.emplace_back(ctrl_joint, mode);
    }

    void ControlInterface::submit_vel_ref(std::string ctrl_joint, int vel)
    {
        std::lock_guard<std::mutex> ql(inbox_mtx_);
        vel_inbox_.emplace_back(ctrl_joint, vel);
    }

    void ControlInterface::submit_torque_ref(std::string ctrl_joint, int torque)
    {
        std::lock_guard<std::mutex> ql(inbox_mtx_);
        torque_inbox_.emplace_back(ctrl_joint, torque);
    }

    void ControlInterface::drain_pending_targets_locked()
    {
        std::lock_guard<std::mutex> ql(inbox_mtx_);
        while (!inbox_.empty())
        {
            auto [d, r] = inbox_.front();
            inbox_.pop_front();
            target_values_[d] = r;
        }
        while (!mode_inbox_.empty())
        {
            auto [d, m] = mode_inbox_.front();
            mode_inbox_.pop_front();
            ControlMode mode = control_mode_from_string(m);
            if (static_cast<int>(mode) == 0xff)
                continue;
            control_mode_[d] = static_cast<int>(mode);
        }
        while (!vel_inbox_.empty())
        {
            auto [d, r] = vel_inbox_.front();
            vel_inbox_.pop_front();
            targets_vel_[d] = r;
        }
        while (!torque_inbox_.empty())
        {
            auto [d, r] = torque_inbox_.front();
            torque_inbox_.pop_front();
            targets_torque_[d] = r;
        }
    }

    void ControlInterface::receiver_loop()
    {
        // Buffers reused across iterations (no per-frame allocation).
        std::vector<RealtimeData> realtime(num_joint + 1);
        std::vector<JointTels> data(num_joint + 1);
        std::vector<TipTels> tip_data(5);
        BusData bus_data;

        // Last timestamp already fed into the filter, per signal. TPDO1 (runtime) and
        // TPDO2 (sensor) can land in different batches, so a signal may be unchanged
        // even though frames arrived — it must not be filtered twice.
        std::vector<uint32_t> last_joint_ts(num_joint + 1, 0);
        std::vector<uint32_t> last_tip_ts(5, 0);

        while (!shutting_down_)
        {
            uint32_t got = 0;
            Status st = ctrl_.receive_step(realtime, data, tip_data, bus_data, num_joint, &got);
            if (st != Status::Ok)
            {
                std::cout << ctrl_.last_error();
                continue;
            }

            // No new frame: the buffers still hold the previous sample. Filtering it
            // again would converge the filter onto that stale value and cancel the
            // smoothing, so only touch the state when fresh data arrived.
            if (got == 0)
                continue;

            std::lock_guard<std::mutex> lk(mtx_);
            bus_info_ = bus_data; // latest bus telemetry for the UI

            // Decoders fill their buffers by axis-block index i (block i <-> axis id i+1,
            // see protocol 九 / 6.1), so the feedback of joint id j lives at index j-1.
            for (int j = 1; j < num_joint + 1; j++)
            {
                auto name_it = joint_names_.find(j);
                if (name_it == joint_names_.end())
                    continue;
                const std::string &name = name_it->second;
                const int idx = j - 1;

                // Only feed the filter when this signal actually carries a new sample.
                if (data[idx].timestamp_ms == last_joint_ts[idx])
                    continue;
                last_joint_ts[idx] = data[idx].timestamp_ms;

                // CAN timestamp is in ms; the filter works in seconds.
                const double t_sec = static_cast<double>(data[idx].timestamp_ms) * 1e-3;
                const float joint_pos = filter_enabled_
                    ? static_cast<float>(get_filter(joint_pos_filters_, name).filter(data[idx].joint_position, t_sec))
                    : data[idx].joint_position;
                const float motor_pos = filter_enabled_
                    ? static_cast<float>(get_filter(motor_pos_filters_, name).filter(data[idx].motor_position, t_sec))
                    : data[idx].motor_position;

                DevState joint_state;
                joint_state.position = joint_pos;
                joint_state.velocity = 0;
                joint_state.effort = 0;

                // Motor torque feedback comes from TPDO1 (RealtimeData::curr_torque).
                // Filter it like the positions: the raw value is noisy.
                const float motor_torque = filter_enabled_
                    ? static_cast<float>(get_filter(torque_filters_, name).filter(realtime[idx].curr_torque, t_sec))
                    : static_cast<float>(realtime[idx].curr_torque);

                DevState motor_state;
                motor_state.position = motor_pos;
                motor_state.velocity = 0;
                // TPDO1 "torq" is the motor current in mA (raw, not normalised).
                motor_state.effort = motor_torque;

                for (auto &[key, val] : state_)
                {
                    auto jit = val.joints.find(name);
                    if (jit == val.joints.end())
                        continue;
                    jit->second = joint_state;
                    val.motors[name] = motor_state;
                    val.control_mode[name] = static_cast<int>(realtime[idx].control_mode);
                }
            }
            for (size_t i = 0; i < 5; i++)
            {
                const std::string &dev = tip_data[i].device;
                if (dev.empty())
                    continue;

                if (tip_data[i].timestamp_ms == last_tip_ts[i])
                    continue;
                last_tip_ts[i] = tip_data[i].timestamp_ms;

                TipTels ts = tip_data[i];
                if (filter_enabled_)
                {
                    const double t_sec = static_cast<double>(ts.timestamp_ms) * 1e-3;
                    ts.tip_tactile = static_cast<int16_t>(
                        std::lround(get_filter(tip_tactile_filters_, dev).filter(ts.tip_tactile, t_sec)));
                    ts.tip_proximity_sense = static_cast<uint16_t>(
                        std::lround(get_filter(tip_prox_filters_, dev).filter(ts.tip_proximity_sense, t_sec)));
                }

                auto buf = state_.find(dev);
                if (buf != state_.end())
                    buf->second.tip_sensor = ts;
            }
        }
    }

    void ControlInterface::control_loop()
    {
        // {
        //     std::unique_lock<std::mutex> lk(mtx_);
        //     cv_.wait(lk, [this]
        //              { return calibrated_ || shutting_down_; });
        // }
        if (shutting_down_)
            return;

        while (!shutting_down_)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(control_period_));
            std::vector<CmdStruct> cmds(num_joint);
            {
                std::lock_guard<std::mutex> lk(mtx_);
                drain_pending_targets_locked();
                for (auto &[id, name] : joint_names_)
                {
                    CmdStruct cs;
                    cs.dev_id = static_cast<size_t>(id);
                    cs.control_mode = static_cast<ControlMode>(control_mode_[name]);
                    // target values are raw slider counts (angle modes: 0.01 deg i.e. deg*100;
                    // hall mode: hall counts). /100 -> degree/counts, then encode scales *100.
                    cs.cmd_joint_angle = target_values_[name] / 100.0f;
                    cs.joint_vel_ref = targets_vel_[name] / 100.0f;
                    cs.motor_torque_ref = targets_torque_[name];
                    cmds[cs.dev_id - 1] = cs;
                    // std::cout << "dev_id=" << cs.dev_id << ", control_mode=" << cs.control_mode << ", joint_pos=" << cs.cmd_joint_angle
                    //           << ", vel_ref=" << cs.joint_vel_ref << ", motor_torque=" << cs.motor_torque_ref << std::endl;
                }
                // std::cout << std::endl;
            }
            ctrl_.send_command(cmds);
        }
    }

    HandStateMap ControlInterface::snapshot() const
    {
        std::lock_guard<std::mutex> lk(mtx_);
        return state_;
    }

    BusData ControlInterface::bus_info() const
    {
        std::lock_guard<std::mutex> lk(mtx_);
        return bus_info_;
    }

    int ControlInterface::hand_id() const
    {
        return ctrl_.hand_id();
    }

    uint32_t ControlInterface::dev_id() const
    {
        return ctrl_.dev_id();
    }

    std::string ControlInterface::hand_side() const
    {
        return ctrl_.hand_side();
    }

    int ControlInterface::get_board_state() const
    {
        return board_state_;
    }

    OneEuroFilter &ControlInterface::get_filter(std::unordered_map<std::string, OneEuroFilter> &m,
                                                const std::string &name)
    {
        auto it = m.find(name);
        if (it == m.end())
            it = m.emplace(name, OneEuroFilter(f_freq_, f_min_cutoff_, f_beta_, f_d_cutoff_)).first;
        return it->second;
    }

    void ControlInterface::set_filter_enabled(bool on)
    {
        filter_enabled_ = on;
    }

    void ControlInterface::set_filter_params(double freq, double min_cutoff, double beta, double d_cutoff)
    {
        // The receiver thread touches the same filters while holding mtx_.
        std::lock_guard<std::mutex> lk(mtx_);

        f_freq_ = freq > 0.0 ? freq : f_freq_;
        f_min_cutoff_ = min_cutoff;
        f_beta_ = beta;
        f_d_cutoff_ = d_cutoff;

        auto apply = [&](std::unordered_map<std::string, OneEuroFilter> &m)
        {
            for (auto &[name, f] : m)
                f.set_params(f_freq_, f_min_cutoff_, f_beta_, f_d_cutoff_);
        };
        apply(joint_pos_filters_);
        apply(motor_pos_filters_);
        apply(torque_filters_);
        apply(tip_tactile_filters_);
        apply(tip_prox_filters_);
    }

    void ControlInterface::reset_filters()
    {
        std::lock_guard<std::mutex> lk(mtx_);

        auto clear = [](std::unordered_map<std::string, OneEuroFilter> &m)
        {
            for (auto &[name, f] : m)
                f.reset();
        };
        clear(joint_pos_filters_);
        clear(motor_pos_filters_);
        clear(torque_filters_);
        clear(tip_tactile_filters_);
        clear(tip_prox_filters_);
    }

    void ControlInterface::set_module_control_mode(std::string module, int mode)
    {
        return;
    }

} // namespace grip
