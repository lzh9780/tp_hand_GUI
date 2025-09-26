#include "grip/control_interface.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>

namespace grip {

ControlInterface::ControlInterface(const std::vector<JointDevice>& devices, PolicyFn policy)
    : devices_(devices), policy_fn_(std::move(policy)) {
    std::sort(devices_.begin(), devices_.end(),
              [](JointDevice a, JointDevice b){ return (uint32_t)a < (uint32_t)b; });
    for (auto d : devices_) { state_[d] = Telemetry{}; targets_rad_[d] = 0.0f; }
}

ControlInterface::~ControlInterface() {
    stop_all();
    try { bus_.close(); } catch (...) {}
}

void ControlInterface::start_bus(const CanDeviceSettings& cfg) {
    bus_channel_ = cfg.channel_index;
    bus_ = CanBus(cfg.device_type, cfg.device_index);
    bus_.open();
    BitTime arb { cfg.bit_timing.arbitration.tseg1, cfg.bit_timing.arbitration.tseg2,
                  cfg.bit_timing.arbitration.sjw,   cfg.bit_timing.arbitration.brp };
    BitTime dta { cfg.bit_timing.data_phase.tseg1,  cfg.bit_timing.data_phase.tseg2,
                  cfg.bit_timing.data_phase.sjw,    cfg.bit_timing.data_phase.brp };
    bus_.configure(bus_channel_, cfg.core_clock_hz, arb, dta, cfg.termination_enabled);
    bus_.start(bus_channel_);
    rx_thread_ = std::thread([this]{ receiver_loop(); });
}

void ControlInterface::stop_all() {
    shutting_down_ = true;
    cv_.notify_all();
    if (rx_thread_.joinable())   rx_thread_.join();
    if (ctrl_thread_.joinable()) ctrl_thread_.join();
}

void ControlInterface::run_calibration(const CalibrationSettings& c) {
    using namespace std::chrono_literals;

    // limit current
    {
        std::vector<int32_t> vals(devices_.size(), c.current_limit_mA);
        send_broadcast(MsgKind::SET_MOTOR_PARAM, (uint8_t)ParamField::MAX_CURRENT, vals);
        std::this_thread::sleep_for(10ms);
    }

    // backdrive until stall
    while (!shutting_down_) {
        std::vector<int32_t> vals(devices_.size(), c.backdrive_speed);
        send_broadcast(MsgKind::MOTOR_COMMAND, (uint8_t)MotorMode::CALIBRATE, vals);
        std::this_thread::sleep_for(10ms);

        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (all_stalled_locked()) {
                std::vector<int32_t> zeros(devices_.size(), 0);
                send_broadcast(MsgKind::MOTOR_COMMAND, (uint8_t)MotorMode::CALIBRATE, zeros);
                std::this_thread::sleep_for(500ms);
                std::vector<int32_t> offs(devices_.size(), c.encoder_zero_offset);
                send_broadcast(MsgKind::SET_MOTOR_PARAM, (uint8_t)ParamField::ENCODER_POS, offs);
                break;
            }
        }
        std::this_thread::sleep_for(50ms);
    }

    {
        std::lock_guard<std::mutex> lk(mtx_);
        calibrated_ = true;
    }
    cv_.notify_all();
    std::cout << "Calibration complete.\n";
}

void ControlInterface::start_control_loop() {
    ctrl_thread_ = std::thread([this]{ control_loop(); });
}

void ControlInterface::set_target(JointDevice d, float radians) {
    std::lock_guard<std::mutex> lk(mtx_);
    targets_rad_[d] = radians;
}

void ControlInterface::set_default_targets(float others_rad, float thumb_rad) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto d : devices_) {
        bool thumb = (d == JointDevice::THUMB);
        targets_rad_[d] = thumb ? thumb_rad : others_rad;
    }
}

std::unordered_map<JointDevice, Telemetry> ControlInterface::snapshot() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return state_;
}

bool ControlInterface::all_stalled_locked() const {
    for (auto d : devices_) {
        auto it = state_.find(d);
        if (it == state_.end()) return false;
        if (it->second.fault != (uint16_t)FaultCode::MOTOR_STALL) return false;
    }
    return true;
}

void ControlInterface::send_broadcast(MsgKind kind, uint8_t field, const std::vector<int32_t>& values) {
    if (values.size() != devices_.size()) return;
    auto id = make_can_id(JointDevice::BROADCAST, kind);
    auto payload = pack_broadcast_payload(devices_, field, values);
    auto frame = CanBus::buildFD(id, payload, (uint8_t)bus_channel_, 0);
    bus_.transmitFD(bus_channel_, std::vector<ZCAN_FD_MSG>{frame});
}

void ControlInterface::send_single(JointDevice dev, MsgKind kind, uint8_t field, int32_t value) {
    auto id = make_can_id(dev, kind);
    auto payload = pack_single_payload(dev, field, value);
    auto frame = CanBus::buildFD(id, payload, (uint8_t)bus_channel_, 0);
    bus_.transmitFD(bus_channel_, std::vector<ZCAN_FD_MSG>{frame});
}

void ControlInterface::receiver_loop() {
    using namespace std::chrono_literals;
    while (!shutting_down_) {
        auto frames = bus_.receiveFD(bus_channel_, 100, 100);
        for (auto& f : frames) {
            const uint32_t dev_bits = (f.id & 0x01F);
            const uint32_t kind_bits= (f.id & 0x3C0);
            if (kind_bits == (uint32_t)MsgKind::MOTOR_TELEMETRY) {
                auto dev = (JointDevice)dev_bits;
                if (std::find(devices_.begin(), devices_.end(), dev) == devices_.end()) continue;
                if (f.data.size() < 14) continue;

                Telemetry t;
                t.speed           = rd_i16(&f.data[0]);
                t.iq_mA           = rd_i16(&f.data[2]);
                t.encoder_counts  = rd_i32(&f.data[4]);
                t.bus_mV          = rd_u16(&f.data[8]);
                t.motor_degC      = (int8_t)f.data[10];
                t.fault           = rd_u16(&f.data[11]);
                t.state           = f.data[13];

                std::lock_guard<std::mutex> lk(mtx_);
                state_[dev] = t;
            }
        }
        std::this_thread::sleep_for(30ms);
    }
}

void ControlInterface::control_loop() {
    using namespace std::chrono_literals;
    {
        std::unique_lock<std::mutex> lk(mtx_);
        cv_.wait(lk, [this]{ return calibrated_ || shutting_down_; });
    }
    if (shutting_down_) return;

    while (!shutting_down_) {
        std::vector<std::pair<JointDevice,int32_t>> commands;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            for (auto d : devices_) {
                int32_t pos = state_[d].encoder_counts;
                float   tgt = targets_rad_[d];
                commands.push_back({d, policy_fn_(d, tgt, pos)});
            }
        }
        for (auto& c : commands) {
            send_single(c.first, MsgKind::MOTOR_COMMAND, (uint8_t)MotorMode::SPEED, c.second);
        }
        std::this_thread::sleep_for(50ms);
    }
}

} // namespace grip
