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
            if (ctrl)
            {
                grip_control_close_bus(ctrl);
                ctrl = nullptr;
            }
        }
        catch (...)
        {
        }
    }

    void ControlInterface::start_bus()
    {
        grip_can_device_settings_t *cfg = grip_device_setting_create(33, 0, 0);
        ctrl = grip_control_create();
        if (grip_control_start_bus(ctrl, cfg) == GRIP_OK)
            std::cout << "Can device open successfully\n";
        else
        {
            const char* error = grip_get_last_error(ctrl);
            grip_control_close_bus(ctrl);
            throw std::runtime_error(error);
        }
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

    void ControlInterface::submit_target(std::string ctrl_joint, float radians)
    {
        std::lock_guard<std::mutex> ql(inbox_mtx_);
        inbox_.emplace_back(ctrl_joint, radians);
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
            targets_rad_[d] = r;
        }
        while (!mode_inbox_.empty())
        {
            auto [d, m] = mode_inbox_.front();
            mode_inbox_.pop_front();
            uint16_t mode_id = grip_control_mode_id(m.c_str());
            if (mode_id == 0xff)
                continue;
            control_mode_[d] = mode_id;
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
        while (!shutting_down_)
        {
            RealtimeData *realtime = (RealtimeData *)malloc((num_joint + 1) * sizeof(RealtimeData));
            JointTels *data = (JointTels *)malloc((num_joint + 1) * sizeof(JointTels));
            TipTels tip_data[5];
            BusData bus_data;
            grip_status_t st = grip_receive_step(ctrl, realtime, data, tip_data, &bus_data, num_joint);
            if (st != GRIP_OK)
            {
                std::cout << grip_get_last_error(ctrl);
                continue;
            }

            for (int j = 1; j < num_joint + 1; j++)
            {
                size_t id = (size_t)data[j].dev_id;
                size_t realtime_id = (size_t)realtime[j].dev_id;

                DevState joint_state;
                joint_state.position = data[id].joint_position;
                joint_state.velocity = 0;
                joint_state.effort = 0;

                DevState motor_state;
                motor_state.position = data[id].motor_position;
                motor_state.velocity = 0;
                motor_state.effort = 0;

                for (const auto &[key, val] : state_)
                {
                    if (joint_names_.find(id) != joint_names_.end() &&
                        val.joints.find(joint_names_[id]) != val.joints.end())
                    {
                        std::lock_guard<std::mutex> lk(mtx_);
                        if (id == j)
                        {
                            state_[key].joints[joint_names_[id]] = joint_state;
                            state_[key].motors[joint_names_[id]] = motor_state;
                        }
                        if (realtime_id == j)
                        {
                            state_[key].control_mode[joint_names_[id]] = (grip_control_mode_t)realtime[j].control_mode;
                        }
                        break;
                    }
                }
            }
            for (size_t i = 0; i < 5; i++)
            {
                TipTels ts;
                ts.device = tip_data[i].device;
                ts.timestamp_ms = tip_data[i].timestamp_ms;
                ts.tip_tactile = tip_data[i].tip_tactile;
                ts.tip_proximity_sense = tip_data[i].tip_proximity_sense;

                {
                    std::lock_guard<std::mutex> lk(mtx_);
                    std::string str;
                    str = tip_data[i].device ? tip_data[i].device : "";
                    auto buf = state_.find(str);
                    if (buf == state_.end())
                        continue;
                    else
                        buf->second.tip_sensor = ts;
                }
            }
            free(realtime);
            free(data);
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
            CmdStruct *cmds = (CmdStruct *)malloc(num_joint * sizeof(CmdStruct));
            {
                std::lock_guard<std::mutex> lk(mtx_);
                drain_pending_targets_locked();
                for (auto &[id, name] : joint_names_)
                {
                    CmdStruct cs;
                    cs.dev_id = static_cast<size_t>(id);
                    cs.control_mode = static_cast<grip_control_mode_t>(control_mode_[name]);
                    cs.cmd_joint_angle = targets_rad_[name] / 100.0f;
                    cs.joint_vel_ref = targets_vel_[name] / 100.0f;
                    cs.motor_torque_ref = targets_torque_[name];
                    cmds[cs.dev_id - 1] = cs;
                    // std::cout << "dev_id=" << cs.dev_id << ", control_mode=" << cs.control_mode << ", joint_pos=" << cs.cmd_joint_angle
                    //           << ", vel_ref=" << cs.joint_vel_ref << ", motor_torque=" << cs.motor_torque_ref << std::endl;
                }
                // std::cout << std::endl;
            }
            grip_send_command(ctrl, cmds, num_joint);
            free(cmds);
        }
    }

    HandStateMap ControlInterface::snapshot() const
    {
        std::lock_guard<std::mutex> lk(mtx_);
        return state_;
    }

    int ControlInterface::get_board_state() const
    {
        return board_state_;
    }

    void ControlInterface::set_module_control_mode(std::string module, int mode)
    {
        return;
    }

} // namespace grip
