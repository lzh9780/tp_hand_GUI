#include "grip/control_interface.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <cmath>

namespace grip
{

    ControlInterface::ControlInterface(const HandSettings &hand_cfg,
                                       std::chrono::milliseconds period)
        : control_period_(period)
    {

        // hand_ = hand_cfg.structs;
        for (const auto &[key, value] : hand_cfg.structs)
        {
            ModuleState ms;
            for (const auto &dof : value.dofs)
            {
                DofState ds;
                ds.name = dof.name;
                for (const auto &m : value.motors)
                    ds.motors.push_back(m);
                ms.dofs.push_back(ds);
            }
            for (const auto &m : value.motors)
            {
                motors_.push_back(m);
                Telemetry t;
                ms.motors.emplace(m, t);
                // std::cout << m << " in " << key << "\n"; 
            }
            for (const auto &j : value.joints) 
            {
                JointState js;
                ms.joints.emplace(j, js);
            }
            hand_.emplace(key, ms);

            // if (key != "WRIST") ms.control_mode = 2;
            // HandControl c{ms};
            // ModuleLayout lay = c.layout();
            // ModuleInputSnapshot in = make_module_input(lay);
            // ModuleOutputSnapshot out = make_module_output(lay);

            // ControlModule cm;
            // cm.ms = ms;
            // cm.c = c;
            // cm.lay = lay;
            // cm.in = in;
            // cm.out = out;

            // ctrl_mod_.emplace(key, cm);
        }
    }

    ControlInterface::ControlInterface(const ActuatorSettings &actuator_cfg,
                                       std::chrono::milliseconds period)
        : control_period_(period)
    {
        motors_.push_back(actuator_cfg.device_id);
        state_[actuator_cfg.device_id] = Telemetry{};

        targets_rad_["Max Velocity"] = 0.0f;
        targets_rad_["Max Current"] = 0.0f;
        targets_rad_["Position"] = 0.0f;
        targets_rad_["Velocity"] = 0.0f;
        targets_rad_["Torque"] = 0.0f;
        targets_rad_["-"] = 0.0f;
    }

    ControlInterface::~ControlInterface()
    {
        stop_all();
        try
        {
            bus_.close();
        }
        catch (...)
        {
        }
    }

    HandStateMap ControlInterface::get_hand_runtime_data()
    {
        std::lock_guard<std::mutex> lk(mtx_);
        HandStateMap hand = hand_;
        return hand;
    }

    void ControlInterface::start_bus(const CanDeviceSettings &cfg)
    {
        bus_channel_ = cfg.channel_index;
        bus_ = CanBus(cfg.device_type, cfg.device_index);
        bus_.open();
        BitTime arb{cfg.bit_timing.arbitration.tseg1, cfg.bit_timing.arbitration.tseg2, cfg.bit_timing.arbitration.sjw, cfg.bit_timing.arbitration.brp};
        BitTime dta{cfg.bit_timing.data_phase.tseg1, cfg.bit_timing.data_phase.tseg2, cfg.bit_timing.data_phase.sjw, cfg.bit_timing.data_phase.brp};
        bus_.configure(bus_channel_, cfg.core_clock_hz, arb, dta, cfg.termination_enabled);
        bus_.start(bus_channel_);
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
            bus_.close();
        }
        catch (...)
        {
        }
    }

    // void ControlInterface::run_calibration()
    // {
    //     using namespace std::chrono_literals;

    //     {
    //         send_broadcast(motors_,
    //                        MsgKind::SET_MOTOR_PARAM,
    //                        (uint8_t)ParamField::CAN_MODE,
    //                        std::vector<int32_t>(motors_.size(), (uint32_t)10));
    //         std::this_thread::sleep_for(10ms);
    //     }
    //     // 1) MAX_CURRENT
    //     {
    //         send_broadcast(motors_,
    //                        MsgKind::SET_MOTOR_PARAM,
    //                        (uint8_t)ParamField::MAX_CURRENT,
    //                        std::vector<int32_t>(motors_.size(), 1500));
    //         std::this_thread::sleep_for(10ms);
    //     }
    //     // 2) CALIBRATE negative speed
    //     {
    //         send_broadcast(motors_,
    //                        MsgKind::MOTOR_COMMAND,
    //                        (uint8_t)MotorMode::CALIBRATE,
    //                        std::vector<int32_t>(motors_.size(), -100));
    //         std::this_thread::sleep_for(10ms);
    //     }
    //     // 3) wait for stall -> stop -> write zero
    //     while (!shutting_down_)
    //     {
    //         {
    //             std::lock_guard<std::mutex> lk(mtx_);
    //             if (all_stalled_locked())
    //             {
    //                 send_broadcast(motors_,
    //                                MsgKind::MOTOR_COMMAND,
    //                                (uint8_t)MotorMode::SPEED,
    //                                std::vector<int32_t>(motors_.size(), 0));
    //                 std::this_thread::sleep_for(500ms);
    //                 send_broadcast(motors_,
    //                                MsgKind::SET_MOTOR_PARAM,
    //                                (uint8_t)ParamField::ENCODER_POS,
    //                                std::vector<int32_t>(motors_.size(), -8));
    //                 send_broadcast(motors_,
    //                        MsgKind::SET_MOTOR_PARAM,
    //                        (uint8_t)ParamField::MAX_CURRENT,
    //                        std::vector<int32_t>(motors_.size(), 2500));
    //         std::this_thread::sleep_for(10ms);
    //                 break;
    //             }
    //         }
    //         std::this_thread::sleep_for(std::chrono::milliseconds(50));
    //     }
    //     {
    //         std::lock_guard<std::mutex> lk(mtx_);
    //         calibrated_ = true;
    //     }
    //     cv_.notify_all();
    //     std::cout << "Calibration complete.\n\n\n\n";
    // }

    bool ControlInterface::all_stalled_locked() const
    {
        for (auto d : motors_)
        {
            auto it = state_.find(d);
            if (it == state_.end())
                return false;
            if (it->second.fault != (uint16_t)FaultCode::MOTOR_STALL)
                return false;
        }
        return true;
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
            auto frames = bus_.receiveFD(bus_channel_, 128, 10);
            for (auto &f : frames)
            {
                for (int i = 0; i < 4; ++i)
                {
                    int id = (uint8_t)f.data[i * 16];
                    if (std::find(motors_.begin(), motors_.end(), id) == motors_.end())
                        continue;
                    if (f.data.size() < 14)
                        continue;
                    Telemetry t = messageDecode(&f.data[i * 16]);
                    // std::cout <<  i * 16 << "\n";
                    // std::cout << "id=" << id << "\n";
                    std::lock_guard<std::mutex> lk(mtx_);
                    state_[id] = t;
#if GRIP_DEBUG_RX
                    if ((int)dev == 1 || (int)dev == 2) 
                    std::cout << "dev=" << (int)dev << " iq=" << t.iq_mA << " enc=" << t.encoder_counts << "\n";
#endif
                }
                // else if (kind_bits == (uint32_t)MsgKind::BOARDSTATE)
                // {
                //     uint16_t s = rd_i16(&f.data[0]);
                //     // std::cout << s << "\n";
                //     std::lock_guard<std::mutex> lk(mtx_);
                //     board_state_ = int(s);
                //     if (board_state_ == 1) 
                //         calibrated_ = true;
                //     else calibrated_ = false;
                // }
            }
        }
    }

    void ControlInterface::control_loop()
    {
        {
            // send_broadcast(motors_,
            //                MsgKind::SET_MOTOR_PARAM,
            //                (uint8_t)ParamField::CAN_MODE,
            //                std::vector<int32_t>(motors_.size(), (uint32_t)10));
            // std::this_thread::sleep_for(10ms);
            // send_single(0x1e,
            //             MsgKind::SET_MOTOR_PARAM,
            //             (uint8_t)ParamField::CAN_MODE,
            //             (uint32_t)10);
            // std::this_thread::sleep_for(10ms);
        }
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
            std::vector<CmdStruct> cmds;
            {
                std::lock_guard<std::mutex> lk(mtx_);
                drain_pending_targets_locked();
                if (hand_.size() != 0) 
                {
                    for (auto &[key, value] : hand_) 
                    {
                        for (auto &[id, state] : value.motors)
                        {
                            CmdStruct cs;
                            cs.joint_id = id;
                            cs.control_mode = 0x20;
                            cs.cmd_joint_angle = 0;
                            cs.joint_vel_ref = 3000;
                            cs.motor_current_ref = 0;
                            cmds.push_back(cs);
                        }
                    }
                } else {
                    int cmd_type = (uint8_t)MotorMode::HALL_GUARD;
                    int cmd_val = 0;
                    // std::lock_guard<std::mutex> lk(mtx_);
                    if (mode_ == "Position") {
                        cmd_type = (uint8_t)MotorMode::HALL_GUARD;
                        cmd_val = (int16_t) (targets_rad_["Position"] / 6.28 * 65535);
                    } else if (mode_ == "Velocity") {
                        cmd_type = (uint8_t)MotorMode::SPEED;
                        cmd_val = (int16_t) targets_rad_["Velocity"];
                        std::cout << "Velocity cmd sent "<< cmd_val << "\n";
                    } else if (mode_ == "Torque") {
                        cmd_type = (uint8_t)MotorMode::TORQUE_STEP;
                        cmd_val = (int16_t) (targets_rad_["Torque"]);
                    }
                    // send_single(1, MsgKind::MOTOR_COMMAND, (uint8_t)cmd_type, (int16_t)cmd_val, (int16_t)0);
                }
            }
            // for (auto &c : cmds)
            // {
            //     // send_single(c.first, MsgKind::MOTOR_COMMAND, (uint8_t)MotorMode::SPEED, (int16_t)c.second, (int16_t)0);
            //     // std::cout << "commands: " << c.first << " data: " << c.second << "\n";
                
            // }
            send_to_broad(cmds);
            std::this_thread::sleep_until(next);
        }
    }

    void ControlInterface::send_to_broad(std::vector<CmdStruct> cmds)
    {
        auto id = (uint32_t)0x200 + (uint32_t)0x20;
        auto p = pack_broadcast_payload(cmds);
        bus_.transmitFD(bus_channel_, std::vector<ZCAN_FD_MSG>{CanBus::buildFD(id, p, (uint8_t)bus_channel_, 0)});
    }

    // void ControlInterface::send_broadcast(std::vector<int> devs, MsgKind kind, uint8_t field, const std::vector<int16_t> &values1, const std::vector<int16_t> &values2)
    // {
    //     auto id = (uint32_t)0x200 | (uint32_t)0x20;
    //     auto p = pack_broadcast_payload(devs, field, values1, values2);
    //     bus_.transmitFD(bus_channel_, std::vector<ZCAN_FD_MSG>{CanBus::buildFD(id, p, (uint8_t)bus_channel_, 0)});
    // }

    // void ControlInterface::send_single(int dev, MsgKind kind, uint8_t field, int32_t value)
    // {
    //     auto id = make_can_id(dev, kind);
    //     auto p = pack_single_payload(dev, field, value);
    //     bus_.transmitFD(bus_channel_, std::vector<ZCAN_FD_MSG>{CanBus::buildFD(id, p, (uint8_t)bus_channel_, 0)});
    // }

    // void ControlInterface::send_single(int dev, MsgKind kind, uint8_t field, int16_t value1, int16_t value2)
    // {
    //     auto id = make_can_id(dev, kind);
    //     auto p = pack_single_payload(dev, field, value1, value2);
    //     bus_.transmitFD(bus_channel_, std::vector<ZCAN_FD_MSG>{CanBus::buildFD(id, p, (uint8_t)bus_channel_, 0)});
    // }

    std::unordered_map<int, Telemetry> ControlInterface::snapshot() const
    {
        std::lock_guard<std::mutex> lk(mtx_);
        return state_;
    }

    JointAngles ControlInterface::get_joint_angles() const
    {
        std::lock_guard<std::mutex> lk(mtx_);
        JointAngles j;
        j.thumb_mcp = joint_angles_.thumb_mcp;
        j.thumb_ip = joint_angles_.thumb_ip;
        j.index_mcp = joint_angles_.index_mcp;
        j.index_pip = joint_angles_.index_pip;
        j.index_dip = joint_angles_.index_dip;
        j.middle_mcp = joint_angles_.middle_mcp;
        j.middle_pip = joint_angles_.middle_pip;
        j.middle_dip = joint_angles_.middle_dip;
        j.ring_mcp = joint_angles_.ring_mcp;
        j.ring_pip = joint_angles_.ring_pip;
        j.ring_dip = joint_angles_.ring_dip;
        j.pinky_mcp = joint_angles_.pinky_mcp;
        j.pinky_pip = joint_angles_.pinky_pip;
        j.pinky_dip = joint_angles_.pinky_dip;
        return j;
    }

    int ControlInterface::get_board_state() const
    {
        return board_state_;
    }

    void ControlInterface::set_module_control_mode(std::string module, int mode)
    {
        auto m = hand_.find(module);
        if (m == hand_.end())
            return;
        auto &mr = m->second;
        mr.control_mode = mode;
    }

    Telemetry ControlInterface::messageDecode(const uint8_t *p)
    {
        Telemetry t;
        t.control_mode = (uint8_t)p[1];
        t.motor_position = rd_i16(&p[2]);
        t.joint_position = rd_i16(&p[4]);
        t.iq_mA = rd_i16(&p[6]);
        t.joint_speed = rd_i16(&p[8]);
        t.tip_tactile = rd_i16(&p[10]);
        t.motor_speed = rd_i16(&p[12]);
        t.temperature = (uint8_t)p[14];
        t.fault = (uint8_t)p[15];
        // std::cout << "motor_position = " << t.motor_position << "\n";
        return t;
    }

} // namespace grip
