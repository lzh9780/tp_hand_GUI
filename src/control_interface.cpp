#include "grip/control_interface.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>

namespace grip
{

    ControlInterface::ControlInterface(const std::vector<JointDevice> &devices,
                                       const std::unordered_map<JointDevice, std::string> &joint_key_of,
                                       const NativePolicySettings &policy_cfg,
                                       std::chrono::milliseconds period)
        : devices_(devices), joint_key_(joint_key_of), policy_(policy_cfg), control_period_(period)
    {
        std::sort(devices_.begin(), devices_.end(),
                  [](JointDevice a, JointDevice b)
                  { return (uint32_t)a < (uint32_t)b; });
        for (auto d : devices_)
        {
            state_[d] = Telemetry{};
            targets_rad_[d] = 0.0f;
        }
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
    }

    void ControlInterface::run_calibration(const CalibrationSettings &c)
    {
        using namespace std::chrono_literals;
        // 1) MAX_CURRENT
        {
            auto id = make_can_id(JointDevice::BROADCAST, MsgKind::SET_MOTOR_PARAM);
            auto payload = pack_broadcast_payload(devices_, (uint8_t)ParamField::MAX_CURRENT,
                                                  std::vector<int32_t>(devices_.size(), c.current_limit_mA));
            auto f = CanBus::buildFD(id, payload, (uint8_t)bus_channel_, 0);
            bus_.transmitFD(bus_channel_, std::vector<ZCAN_FD_MSG>{f});
            std::this_thread::sleep_for(10ms);
        }
        // 2) CALIBRATE negative speed
        {
            auto id = make_can_id(JointDevice::BROADCAST, MsgKind::MOTOR_COMMAND);
            auto payload = pack_broadcast_payload(devices_, (uint8_t)MotorMode::CALIBRATE,
                                                  std::vector<int32_t>(devices_.size(), c.backdrive_speed));
            auto f = CanBus::buildFD(id, payload, (uint8_t)bus_channel_, 0);
            bus_.transmitFD(bus_channel_, std::vector<ZCAN_FD_MSG>{f});
            std::this_thread::sleep_for(10ms);
        }
        // 3) wait for stall -> stop -> write zero
        while (!shutting_down_)
        {
            {
                std::lock_guard<std::mutex> lk(mtx_);
                if (all_stalled_locked())
                {
                    auto id = make_can_id(JointDevice::BROADCAST, MsgKind::MOTOR_COMMAND);
                    auto p1 = pack_broadcast_payload(devices_, (uint8_t)MotorMode::CALIBRATE, std::vector<int32_t>(devices_.size(), 0));
                    bus_.transmitFD(bus_channel_, std::vector<ZCAN_FD_MSG>{CanBus::buildFD(id, p1, (uint8_t)bus_channel_, 0)});
                    std::this_thread::sleep_for(500ms);
                    auto id2 = make_can_id(JointDevice::BROADCAST, MsgKind::SET_MOTOR_PARAM);
                    auto p2 = pack_broadcast_payload(devices_, (uint8_t)ParamField::ENCODER_POS, std::vector<int32_t>(devices_.size(), c.encoder_zero_offset));
                    bus_.transmitFD(bus_channel_, std::vector<ZCAN_FD_MSG>{CanBus::buildFD(id2, p2, (uint8_t)bus_channel_, 0)});
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
        std::cout << "Calibration complete.\n";
    }

    void ControlInterface::start_control_loop()
    {
        ctrl_thread_ = std::thread([this]
                                   { control_loop(); });
    }

    void ControlInterface::submit_target(JointDevice dev, float radians)
    {
        std::lock_guard<std::mutex> ql(inbox_mtx_);
        inbox_.emplace_back(dev, radians);
    }

    void ControlInterface::set_default_targets(float others_rad, float thumb_rad)
    {
        std::lock_guard<std::mutex> lk(mtx_);
        for (auto d : devices_)
            targets_rad_[d] = (d == JointDevice::THUMB) ? thumb_rad : others_rad;
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
                if (kind_bits == (uint32_t)MsgKind::MOTOR_TELEMETRY)
                {
                    auto dev = (JointDevice)dev_bits;
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
                    std::cout << "dev=" << (int)dev << " iq=" << t.iq_mA << " enc=" << t.encoder_counts << "\n";
#endif
                }
            }
        }
    }

    void ControlInterface::control_loop()
    {
        using clock = std::chrono::steady_clock;
        auto next = clock::now();

        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait(lk, [this]
                     { return calibrated_ || shutting_down_; });
        }
        if (shutting_down_)
            return;

        while (!shutting_down_)
        {
            next += control_period_;
            std::vector<std::pair<JointDevice, int32_t>> cmds;
            {
                std::lock_guard<std::mutex> lk(mtx_);
                drain_pending_targets_locked();
                for (auto d : devices_)
                {
                    int32_t enc = state_[d].encoder_counts;
                    double tgt = targets_rad_[d];
                    const std::string &key = joint_key_.at(d);
                    int32_t mA = policy_.command_mA(key, tgt, enc);
                    cmds.push_back({d, mA});
                }
            }
            for (auto &c : cmds)
                send_single(c.first, MsgKind::MOTOR_COMMAND, (uint8_t)MotorMode::SPEED, c.second);
            std::this_thread::sleep_until(next);
        }
    }

    void ControlInterface::send_broadcast(MsgKind kind, uint8_t field, const std::vector<int32_t> &values)
    {
        auto id = make_can_id(JointDevice::BROADCAST, kind);
        auto p = pack_broadcast_payload(devices_, field, values);
        bus_.transmitFD(bus_channel_, std::vector<ZCAN_FD_MSG>{CanBus::buildFD(id, p, (uint8_t)bus_channel_, 0)});
    }

    void ControlInterface::send_single(JointDevice dev, MsgKind kind, uint8_t field, int32_t value)
    {
        auto id = make_can_id(dev, kind);
        auto p = pack_single_payload(dev, field, value);
        bus_.transmitFD(bus_channel_, std::vector<ZCAN_FD_MSG>{CanBus::buildFD(id, p, (uint8_t)bus_channel_, 0)});
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

    std::unordered_map<JointDevice, Telemetry> ControlInterface::snapshot() const
    {
        std::lock_guard<std::mutex> lk(mtx_);
        return state_;
    }

} // namespace grip
