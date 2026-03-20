#include "grip/control_interface.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <cmath>
#include <stdexcept>

namespace grip
{

    ControlInterface::ControlInterface(const HandSettings &hand_cfg,
                                       std::chrono::milliseconds period)
        : control_period_(period)
    {
        for (const auto &[key, value] : hand_cfg.structs)
        {
            for (const auto &joint : value)
            {
                Telemetry t;
                state_.emplace(joint.id, t);
                joint_names_.emplace(joint.id, joint.name);
            }
        }
        device_type = 0;
        num_joint = state_.size();
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
        ctrl = grip_control_create(0, 1);
        if (grip_control_start_bus(ctrl, cfg) == GRIP_OK)
            std::cout << "Can device open successfully\n";
        else
            throw std::runtime_error("Can device fail to open");
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

    void ControlInterface::start_control_loop()
    {
        ctrl_thread_ = std::thread([this]
                                   { control_loop(); });
    }

    void ControlInterface::submit_target(std::string ctrl_joint, float radians)
    {
        std::lock_guard<std::mutex> ql(inbox_mtx_);
        inbox_.emplace_back(ctrl_joint, radians);
        // std::cout << ctrl_joint << radians << '\n';
    }

    void ControlInterface::drain_pending_targets_locked()
    {
        std::lock_guard<std::mutex> ql(inbox_mtx_);
        while (!inbox_.empty())
        {
            auto [d, r] = inbox_.front();
            inbox_.pop_front();
            targets_rad_[d] = r;
            // std::cout << d << " " << r << '\n';
        }
    }

    void ControlInterface::receiver_loop()
    {
        while (!shutting_down_)
        {
            Telemetry *data = (Telemetry *)malloc(num_joint * sizeof(Telemetry));
            grip_status_t st = grip_receive_step(ctrl, data, num_joint);
            if (st != GRIP_OK)
                std::cout << grip_get_last_error(ctrl);

            for (int j = 0; j < num_joint; j++)
            {
                grip_device_t id = (grip_device_t)data[j].dev_id;
                if (id != j)
                    continue;
                std::lock_guard<std::mutex> lk(mtx_);
                state_[id] = data[id];
            }
            free(data);
        }
    }

    void ControlInterface::control_loop()
    {
        using clock = std::chrono::steady_clock;
        auto next = clock::now();

        // {
        //     std::unique_lock<std::mutex> lk(mtx_);
        //     cv_.wait(lk, [this]
        //              { return calibrated_ || shutting_down_; });
        // }
        if (shutting_down_)
            return;

        while (!shutting_down_)
        {
            next += control_period_;
            CmdStruct *cmds = (CmdStruct *)malloc(num_joint * sizeof(CmdStruct *));
            {
                std::lock_guard<std::mutex> lk(mtx_);
                drain_pending_targets_locked();
                for (auto &[id, value] : state_)
                {
                    if (id != 5) continue;
                    CmdStruct cs;
                    cs.dev_id = static_cast<grip_device_t>(id);
                    cs.control_mode = static_cast<grip_control_mode_t>(0x20);
                    cs.cmd_joint_angle = targets_rad_[joint_names_[id]];
                    cs.joint_vel_ref = 3000;
                    cs.motor_torque_ref = 0;
                    cmds[id] = cs;
                }
            }
            grip_send_command(ctrl, cmds, num_joint);
            free(cmds);
            std::this_thread::sleep_until(next);
        }
    }

    StateMap ControlInterface::snapshot() const
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
