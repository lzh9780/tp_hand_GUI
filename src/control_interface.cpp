#include "grip/control_interface.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>

namespace grip
{

    // ControlInterface::ControlInterface(const std::vector<int> &devices,
    //                                    const std::unordered_map<int, std::string> &joint_key_of,
    //                                    const NativePolicySettings &policy_cfg,
    //                                    std::chrono::milliseconds period)
    //     : devices_(devices), joint_key_(joint_key_of), policy_(policy_cfg), control_period_(period)

    ControlInterface::ControlInterface(const HandSettings &hand_cfg,
                                       std::chrono::milliseconds period)
        : policy_(hand_cfg.native_policy), control_period_(period)
    {

        hand_ = hand_cfg.structs;
        for (const auto &[key, value] : hand_cfg.structs)
        {
            for (const auto &m : value.motors)
                devices_.push_back(m.id);
        }

        /* To match the current algorithm. Remove later */
        joint_key_[5] = "thumb_mcp";
        joint_key_[7] = "index_finger_mcp";
        joint_key_[4] = "middle_finger_mcp";
        joint_key_[6] = "ring_finger_mcp";
        joint_key_[3] = "pinky_mcp";
        // joint_key_[1] = "PITCH";
        // joint_key_[2] = "YAW";
        ctrl_key_[5] = "THUMB";
        ctrl_key_[7] = "INDEX_FINGER";
        ctrl_key_[4] = "MIDDLE_FINGER";
        ctrl_key_[6] = "RING_FINGER";
        ctrl_key_[3] = "PINKY";
        ctrl_key_[1] = "PITCH";
        ctrl_key_[2] = "YAW";
        /* To match the current algorithm. Remove later */

        std::sort(devices_.begin(), devices_.end(),
                  [](int a, int b)
                  { return (uint32_t)a < (uint32_t)b; });
        for (auto d : devices_)
        {
            state_[d] = Telemetry{};
            targets_rad_[ctrl_key_[d]] = 0.0f;
        }

        fc_init(&ctrl_);
        fc_set_motor(&ctrl_, FC_INDEX_MCP, 0x07);
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

    void ControlInterface::run_calibration()
    {
        using namespace std::chrono_literals;

        {
            send_broadcast(devices_,
                           MsgKind::SET_MOTOR_PARAM,
                           (uint8_t)ParamField::CAN_MODE,
                           std::vector<int32_t>(devices_.size(), (uint32_t)10));
            std::this_thread::sleep_for(10ms);
        }
        // 1) MAX_CURRENT
        {
            send_broadcast(devices_,
                           MsgKind::SET_MOTOR_PARAM,
                           (uint8_t)ParamField::MAX_CURRENT,
                           std::vector<int32_t>(devices_.size(), 1500));
            std::this_thread::sleep_for(10ms);
        }
        // 2) CALIBRATE negative speed
        {
            send_broadcast(devices_,
                           MsgKind::MOTOR_COMMAND,
                           (uint8_t)MotorMode::CALIBRATE,
                           std::vector<int32_t>(devices_.size(), -100));
            std::this_thread::sleep_for(10ms);
        }
        // 3) wait for stall -> stop -> write zero
        while (!shutting_down_)
        {
            {
                std::lock_guard<std::mutex> lk(mtx_);
                if (all_stalled_locked())
                {
                    send_broadcast(devices_,
                                   MsgKind::MOTOR_COMMAND,
                                   (uint8_t)MotorMode::SPEED,
                                   std::vector<int32_t>(devices_.size(), 0));
                    std::this_thread::sleep_for(500ms);
                    send_broadcast(devices_,
                                   MsgKind::SET_MOTOR_PARAM,
                                   (uint8_t)ParamField::ENCODER_POS,
                                   std::vector<int32_t>(devices_.size(), -8));
                    send_broadcast(devices_,
                           MsgKind::SET_MOTOR_PARAM,
                           (uint8_t)ParamField::MAX_CURRENT,
                           std::vector<int32_t>(devices_.size(), 2500));
            std::this_thread::sleep_for(10ms);
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        {
            std::lock_guard<std::mutex> lk(mtx_);
            calibrated_ = true;
        }
        cv_.notify_all();
        std::cout << "Calibration complete.\n\n\n\n";
    }

    bool ControlInterface::all_stalled_locked() const
    {
        for (auto d : devices_)
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
                const uint32_t dev_bits = (f.id & 0x01F);
                const uint32_t kind_bits = (f.id & 0x3C0);
                // std::cout << (int)dev_bits << " " << f.data.size() << "\n";
                if (kind_bits == (uint32_t)MsgKind::MOTOR_TELEMETRY)
                {
                    auto dev = (int)dev_bits;
                    if (std::find(devices_.begin(), devices_.end(), dev) == devices_.end())
                        continue;
                    if (f.data.size() < 14)
                        continue;
                    Telemetry t;
                    t.speed = rd_i16(&f.data[0]);
                    t.iq_mA = rd_i16(&f.data[2]);
                    t.encoder_counts = rd_i32(&f.data[4]);
                    t.bus_mV = rd_u16(&f.data[8]);
                    t.motor_degC = (int8_t)f.data[10];
                    t.fault = rd_u16(&f.data[11]);
                    t.state = f.data[13];
                    std::lock_guard<std::mutex> lk(mtx_);
                    state_[dev] = t;
#if GRIP_DEBUG_RX
                    if ((int)dev == 1 || (int)dev == 2) 
                    std::cout << "dev=" << (int)dev << " iq=" << t.iq_mA << " enc=" << t.encoder_counts << "\n";
#endif
                }
                else if (kind_bits == (uint32_t)MsgKind::SENSOR_TELEMETRY)
                {
                    std::lock_guard<std::mutex> lk(mtx_);
                    JointAngles j;
                    j.thumb_mcp = float(rd_i16(&f.data[0])) / 65536.0f * float(M_PI);
                    j.thumb_ip = float(rd_i16(&f.data[2])) / 65536.0f * float(M_PI);
                    j.index_mcp = float(rd_i16(&f.data[4])) >= 0 ? float(rd_i16(&f.data[4])) / 65536.0f * float(M_PI) * 6 / 5 : joint_angles_.index_mcp;
                    j.index_pip = float(rd_i16(&f.data[6])) >= 0 ? float(rd_i16(&f.data[6])) / 65536.0f * float(M_PI) * 6 / 5 : joint_angles_.index_pip;
                    j.index_dip = float(rd_i16(&f.data[8])) >= 0 ? float(rd_i16(&f.data[8])) / 65536.0f * float(M_PI) * 6 / 5 : joint_angles_.index_dip;
                    j.middle_mcp = float(rd_i16(&f.data[10])) / 65536.0f * float(M_PI);
                    j.middle_pip = float(rd_i16(&f.data[12])) / 65536.0f * float(M_PI);
                    j.middle_dip = float(rd_i16(&f.data[14])) / 65536.0f * float(M_PI);
                    j.ring_mcp = float(rd_i16(&f.data[16])) / 65536.0f * float(M_PI);
                    j.ring_pip = float(rd_i16(&f.data[18])) / 65536.0f * float(M_PI);
                    j.ring_dip = float(rd_i16(&f.data[20])) / 65536.0f * float(M_PI);
                    j.pinky_mcp = float(rd_i16(&f.data[22])) / 65536.0f * float(M_PI);
                    j.pinky_pip = float(rd_i16(&f.data[24])) / 65536.0f * float(M_PI);
                    j.pinky_dip = float(rd_i16(&f.data[26])) / 65536.0f * float(M_PI);
                    // std::cout << j.index_mcp <<" "<< j.index_pip<<" " << j.index_dip<<"\n";
                    // std::cout << float(rd_u16(&f.data[4])) <<" "<< float(rd_u16(&f.data[6]))<<" " << float(rd_u16(&f.data[8]))<<"\n";

                    double time_diff = (double(f.timestamp) - double(time_stamp_)) / 10000;
                    // std::cout << f.timestamp << " " << time_stamp_ << " " << time_diff << '\n';
                    if (time_diff < 0.001)
                        continue;
                    // std::cout << "time_diff: " << time_diff << "\n";
                    // std::cout << "vel_mcp: " << ((j.index_mcp - joint_angles_.index_mcp) / time_diff) << "\n";
                    // std::cout << "vel_pip: " << ((j.index_pip - joint_angles_.index_pip) / time_diff) << "\n";
                    // std::cout << "vel_dip: " << ((j.index_dip - joint_angles_.index_dip) / time_diff) << "\n";
                    JointAngles j_v;
                    j_v.index_mcp = (j.index_mcp - joint_angles_.index_mcp) / time_diff;
                    j_v.index_pip = (j.index_pip - joint_angles_.index_pip) / time_diff;
                    j_v.index_dip = (j.index_dip - joint_angles_.index_dip) / time_diff;

                    joint_angles_ = j;
                    joint_vels_ = j_v;
                    time_stamp_ = f.timestamp;
                }
                else if (kind_bits == (uint32_t)MsgKind::BOARDSTATE)
                {
                    uint16_t s = rd_i16(&f.data[0]);
                    // std::cout << s << "\n";
                    std::lock_guard<std::mutex> lk(mtx_);
                    board_state_ = int(s);
                }
            }
        }
    }

    void ControlInterface::control_loop()
    {
        {
            send_broadcast(devices_,
                           MsgKind::SET_MOTOR_PARAM,
                           (uint8_t)ParamField::CAN_MODE,
                           std::vector<int32_t>(devices_.size(), (uint32_t)10));
            // std::this_thread::sleep_for(10ms);
            send_single(0x1e,
                        MsgKind::SET_MOTOR_PARAM,
                        (uint8_t)ParamField::CAN_MODE,
                        (uint32_t)10);
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
            std::vector<std::pair<int, int32_t>> cmds;
            {
                std::lock_guard<std::mutex> lk(mtx_);
                drain_pending_targets_locked();
                for (auto d : devices_)
                {
                    // std::cout << "device: " << joint_key_.at(d);
                    // if (d != 1 && d != 2)
                    // {
                    //     int32_t enc = state_[d].encoder_counts;
                    //     double tgt = targets_rad_[ctrl_key_[d]];
                    //     const std::string &key = joint_key_.at(d);
                    //     // std::cout << " encoder: " << enc;
                    //     int32_t mA = policy_.command_mA(key, tgt, enc*256);
                    //     // std::cout << " control method: Hall, target: " << tgt << "\n";
                    //     cmds.push_back({d, mA});
                    // } else
                    if (ctrl_key_[d] == "INDEX_FINGER")
                    {
                        int motor;
                        int32_t mA;
                        // std::cout << joint_angles_.index_mcp <<" "<< joint_angles_.index_pip<<" " << joint_angles_.index_dip<<"\n";
                        fc_step(&ctrl_,
                                FC_INDEX_MCP,
                                targets_rad_[ctrl_key_[d]],
                                joint_angles_.index_mcp,
                                joint_angles_.index_pip,
                                joint_angles_.index_dip,
                                joint_vels_.index_mcp,
                                joint_vels_.index_pip,
                                joint_vels_.index_dip,
                                &motor,
                                &mA);
                        cmds.push_back({d, mA});
                    }
                    else if (d == 1)
                    {
                        in.pitch = targets_rad_["PITCH"];
                        in.yaw = targets_rad_["YAW"];
                        in.roll = 0.0;
                        in.left_counts = state_[1].encoder_counts*256;
                        in.right_counts = state_[2].encoder_counts*256;
                        // in.left_counts = 10000;
                        // in.right_counts = 10000;

                        if (auto wcmds = wrist_.step(in))
                        {
                            for (auto &c : *wcmds)
                            {
                                cmds.push_back({c.motor_id, c.command});
                                // std::cout << c.motor_id << " " << c.command << "\n";
                            }
                        }
                    }
                }
            }
            for (auto &c : cmds){
                send_single(c.first, MsgKind::MOTOR_COMMAND, (uint8_t)MotorMode::SPEED, (int16_t)c.second, (int16_t)0);
                // std::cout << "commands: " << c.first << " data: " << c.second << "\n";
            }
            std::this_thread::sleep_until(next);
        }
    }

    void ControlInterface::send_broadcast(std::vector<int> devs, MsgKind kind, uint8_t field, const std::vector<int32_t> &values)
    {
        auto id = make_can_id(0x1f, kind);
        auto p = pack_broadcast_payload(devs, field, values);
        bus_.transmitFD(bus_channel_, std::vector<ZCAN_FD_MSG>{CanBus::buildFD(id, p, (uint8_t)bus_channel_, 0)});
    }

    void ControlInterface::send_broadcast(std::vector<int> devs, MsgKind kind, uint8_t field, const std::vector<int16_t> &values1, const std::vector<int16_t> &values2)
    {
        auto id = make_can_id(0x1f, kind);
        auto p = pack_broadcast_payload(devs, field, values1, values2);
        bus_.transmitFD(bus_channel_, std::vector<ZCAN_FD_MSG>{CanBus::buildFD(id, p, (uint8_t)bus_channel_, 0)});
    }

    void ControlInterface::send_single(int dev, MsgKind kind, uint8_t field, int32_t value)
    {
        auto id = make_can_id(dev, kind);
        auto p = pack_single_payload(dev, field, value);
        bus_.transmitFD(bus_channel_, std::vector<ZCAN_FD_MSG>{CanBus::buildFD(id, p, (uint8_t)bus_channel_, 0)});
    }

    void ControlInterface::send_single(int dev, MsgKind kind, uint8_t field, int16_t value1, int16_t value2)
    {
        auto id = make_can_id(dev, kind);
        auto p = pack_single_payload(dev, field, value1, value2);
        bus_.transmitFD(bus_channel_, std::vector<ZCAN_FD_MSG>{CanBus::buildFD(id, p, (uint8_t)bus_channel_, 0)});
    }

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

} // namespace grip
