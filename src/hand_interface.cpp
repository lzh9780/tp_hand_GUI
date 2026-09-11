#include "grip/hand_interface.hpp"

#include <cstring>
#include <ctime>
#include <algorithm>

extern "C"
{
#include "zcan.h"
}

namespace grip
{

    /* ================================================================== *
     *  String helpers
     * ================================================================== */
    const char *status_str(Status s)
    {
        switch (s)
        {
        case Status::Ok:
            return "OK";
        case Status::InvalidArg:
            return "invalid_arg";
        case Status::Io:
            return "io";
        case Status::Parse:
            return "parse";
        case Status::NoMem:
            return "no_mem";
        case Status::Internal:
            return "internal";
        case Status::Can:
            return "can";
        default:
            return "unknown";
        }
    }

    const char *module_str(Module d)
    {
        switch (d)
        {
        case Module::Wrist:
            return "WRIST";
        case Module::Thumb:
            return "THUMB";
        case Module::Index:
            return "INDEX_FINGER";
        case Module::Middle:
            return "MIDDLE_FINGER";
        case Module::Ring:
            return "RING_FINGER";
        case Module::Pinky:
            return "PINKY";
        default:
            return "unknown";
        }
    }

    const char *control_mode_str(ControlMode c)
    {
        switch (c)
        {
        case ControlMode::ZeroTorque:
            return "zero_torque_mode";
        // case ControlMode::Current: return "current_mode";
        case ControlMode::HallPos:
            return "hall_position_mode";
        case ControlMode::JointPos:
            return "joint_position_mode";
        // case ControlMode::Impedance: return "impedance_mode";
        // case ControlMode::CascadedPid: return "cascaded_PID_mode";
        case ControlMode::MitTorque:
            return "MIT_torque_mode";
        // case ControlMode::Hybrid: return "hybrid_mode";
        // case ControlMode::ForcePosHybrid: return "force_position_hybrid_mode";
        // case ControlMode::Emergency: return "emergency_mode";
        // case ControlMode::Homing: return "homing_mode";
        default:
            return "unknown";
        }
    }

    ControlMode control_mode_from_string(const std::string &mode_name)
    {
        if (mode_name == "zero_torque_mode" || mode_name == "zero_torque")
            return ControlMode::ZeroTorque;
        // if (mode_name == "current_mode" || mode_name == "current") return ControlMode::Current;
        if (mode_name == "hall_position_mode" || mode_name == "hall_position")
            return ControlMode::HallPos;
        if (mode_name == "joint_position_mode" || mode_name == "joint_position")
            return ControlMode::JointPos;
        // if (mode_name == "impedance_mode" || mode_name == "impedance") return ControlMode::Impedance;
        // if (mode_name == "cascaded_PID_mode" || mode_name == "cascaded_PID") return ControlMode::CascadedPid;
        if (mode_name == "MIT_torque_mode" || mode_name == "MIT_torque")
            return ControlMode::MitTorque;
        // if (mode_name == "hybrid_mode" || mode_name == "hybrid") return ControlMode::Hybrid;
        // if (mode_name == "force_position_hybrid_mode" || mode_name == "force_position_hybrid") return ControlMode::ForcePosHybrid;
        // if (mode_name == "emergency_mode" || mode_name == "emergency") return ControlMode::Emergency;
        // if (mode_name == "homing_mode" || mode_name == "homing") return ControlMode::Homing;

        return static_cast<ControlMode>(0xFF);
    }

    std::vector<std::string> fault_code_str(uint16_t code)
    {
        static const char *k_names[16] = {
            "bus_over_voltage",
            "bus_under_voltage",
            "MCU_over_temp",
            "MOS_over_temp",
            "board0_over_temp",
            "board1_over_temp",
            "bus_over_current",
            "phase_over_current",
            "current_unbalance",
            "DRV_fault",
            "PAMP_sensor_timeout",
            "DRVM1_timeout",
            "DRVM2_timeout",
            "unknown_fault_code",
            "unknown_fault_code",
            "unknown_fault_code",
        };

        std::vector<std::string> result;
        for (size_t i = 0; i < 16; i++)
        {
            if (code & (1u << i))
                result.emplace_back(k_names[i]);
        }
        return result;
    }

    /* ================================================================== *
     *  Little-endian helpers
     * ================================================================== */
    static int16_t rd_i16(const uint8_t *p) { return static_cast<int16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8)); }
    static uint16_t rd_u16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8)); }
    static int32_t rd_i32(const uint8_t *p)
    {
        return static_cast<int32_t>(p[0] | (static_cast<uint32_t>(p[1]) << 8) |
                                    (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24));
    }

    /* ================================================================== *
     *  Lifecycle
     * ================================================================== */
    HandInterface::~HandInterface()
    {
        close_bus();
    }

    uint32_t HandInterface::make_can_id(uint32_t dir) const
    {
        // CAN ID = (cmd << 7) | devID, devID = handID(0x60) | slaveID(0x1F) (protocol 三).
        // Use the devID observed on the bus so the board accepts our frames; before the
        // first frame is seen we can only fall back to the bare slave id.
        const uint32_t devid = devid_ ? devid_ : (static_cast<uint32_t>(hand_id_) & 0x1Fu);
        return (dir << 7) | (devid & 0x7Fu);
    }

    uint32_t HandInterface::to_millis(uint32_t ts_ticks)
    {
        // ZCAN_MSG_HDR::ts counts 0.1 ms ticks on a wrapping 32-bit counter.
        if (ts_prev_valid_ && ts_ticks < ts_prev_)
            ts_high_ += 1ull << 32; // counter wrapped, keep the stamp monotonic
        ts_prev_ = ts_ticks;
        ts_prev_valid_ = true;
        return static_cast<uint32_t>((ts_high_ + ts_ticks) / 10u); // ticks (0.1 ms) -> ms
    }

    Status HandInterface::open_can()
    {
        if (is_open_)
            return Status::Ok;
        if (!VCI_OpenDevice(device_type_, device_index_, 0))
            return Status::Io;
        is_open_ = true;
        return Status::Ok;
    }

    Status HandInterface::configure_can(uint32_t channel_index)
    {
        Status st = open_can();
        if (st != Status::Ok)
            return st;

        ZCAN_INIT zcfg;
        std::memset(&zcfg, 0, sizeof(zcfg));
        zcfg.clk = core_clock_hz_;
        zcfg.mode = 0;

        zcfg.aset.tseg1 = static_cast<uint8_t>(arbitration_.tseg1);
        zcfg.aset.tseg2 = static_cast<uint8_t>(arbitration_.tseg2);
        zcfg.aset.sjw = static_cast<uint8_t>(arbitration_.sjw);
        zcfg.aset.smp = 0;
        zcfg.aset.brp = static_cast<uint16_t>(arbitration_.brp);

        zcfg.dset.tseg1 = static_cast<uint8_t>(data_phase_.tseg1);
        zcfg.dset.tseg2 = static_cast<uint8_t>(data_phase_.tseg2);
        zcfg.dset.sjw = static_cast<uint8_t>(data_phase_.sjw);
        zcfg.dset.smp = 0;
        zcfg.dset.brp = static_cast<uint16_t>(data_phase_.brp);

        if (!VCI_InitCAN(device_type_, device_index_, channel_index, &zcfg))
            return Status::Io;

        uint32_t on = termination_enabled_ ? 1 : 0;
        if (!VCI_SetReference(device_type_, device_index_, channel_index, CMD_CAN_TRES, &on))
        {
            /* Termination setting may be unsupported on some driver versions; non-fatal. */
        }

        return Status::Ok;
    }

    Status HandInterface::start_can(uint32_t channel)
    {
        Status st = open_can();
        if (st != Status::Ok)
            return st;
        if (!VCI_StartCAN(device_type_, device_index_, channel))
            return Status::Io;
        return Status::Ok;
    }

    Status HandInterface::close_can()
    {
        if (!is_open_)
            return Status::Can;
        (void)VCI_CloseDevice(device_type_, device_index_);
        is_open_ = false;
        return Status::Ok;
    }

    /* ================================================================== *
     *  TX / RX
     * ================================================================== */
    Status HandInterface::transmit_fd(uint32_t channel, const uint8_t *data, uint32_t len, uint32_t id)
    {
        Status st = open_can();
        if (st != Status::Ok)
            return st;

        ZCAN_FD_MSG m;
        std::memset(&m, 0, sizeof(m));
        m.hdr.inf.fmt = 1; /* FD */
        m.hdr.inf.brs = 1; /* bit-rate switch */
        m.hdr.id = id;
        m.hdr.chn = static_cast<uint8_t>(channel);
        m.hdr.len = static_cast<uint8_t>(len);
        if (len > 0 && data)
            std::memcpy(m.dat, data, len);

        uint32_t sent = VCI_TransmitFD(device_type_, device_index_, channel, &m, 1);
        return (sent == 1) ? Status::Ok : Status::Io;
    }

    Status HandInterface::receive_fd(uint32_t channel, uint32_t wait_ms, uint32_t &got)
    {
        Status st = open_can();
        if (st != Status::Ok)
            return st;

        ZCAN_FD_MSG frames[kMaxRx];
        std::memset(frames, 0, sizeof(frames));

        got = VCI_ReceiveFD(device_type_, device_index_, channel, frames, kMaxRx, wait_ms);
        for (uint32_t i = 0; i < got; i++)
        {
            rx_msgs_[i].id = frames[i].hdr.id;
            rx_msgs_[i].timestamp = to_millis(frames[i].hdr.ts);
            rx_msgs_[i].size = frames[i].hdr.len;
            std::memcpy(rx_msgs_[i].data.data(), frames[i].dat, rx_msgs_[i].size);
        }
        return Status::Ok;
    }

    Status HandInterface::send_sdo_cmd(uint16_t index, uint8_t subindex)
    {
        uint8_t payload[8] = {0};
        payload[0] = 0x40;
        payload[1] = static_cast<uint8_t>(index & 0xff);
        payload[2] = static_cast<uint8_t>((index >> 8) & 0xff);
        payload[3] = subindex;

        uint32_t id = make_can_id(static_cast<uint32_t>(CanDirection::SdoR));
        return transmit_fd(channel_index_, payload, 8, id);
    }

    /* ================================================================== *
     *  Protocol decode
     * ================================================================== */
    void HandInterface::decode_runtime_telemetry(RealtimeData *out, size_t cap, const uint8_t *p, uint8_t n)
    {
        if (!out || !p || n < 8)
            return;

        int count = n / 8;
        for (int i = 0; i < count && static_cast<size_t>(i) < cap; i++)
        {
            const uint8_t *b = p + 8 * i;
            uint8_t dev_id = b[0];
            if (dev_id == 0)
                continue;
            out[i].dev_id = dev_id;
            // modesOfOperation (joint-drive mode, protocol 2.3/6.2/6.7):
            //   high nibble = control-mode display (0x0/0x2/0x3/0x6 -> ZeroTorque/HallPos/JointPos/MitTorque),
            //   low nibble  = status combination (GUI reads bit0 as the per-joint enable LED).
            // Keep the full byte so the status bits remain available; consumers that need the
            // control mode should mask with (b[1] & 0xF0).
            out[i].control_mode = static_cast<ControlMode>(b[1]);
            out[i].curr_pos = rd_i16(&b[2]) * 0.01f;
            out[i].curr_vel = rd_i16(&b[4]) * 0.01f;
            out[i].curr_torque = rd_i16(&b[6]);
        }
    }

    void HandInterface::decode_sensor_telemetry(BusInfo &bus,
                                                JointSensor *joints, size_t joint_num,
                                                TipSensor *tips,
                                                const uint8_t *p, uint8_t n)
    {
        if (!p)
            return;

        int need = static_cast<int>(10 + joint_num * 2 + joint_num * 2 + 5 * 2 + 5 * 2);
        if (n < need)
            return;

        uint16_t type = rd_u16(&p[0]);
        if (type != 0)
            return;

        bus.bus_voltage = rd_u16(&p[2]) * 0.01f;
        bus.bus_current = rd_u16(&p[4]);
        bus.temp1_cC = p[6];
        bus.temp2_cC = p[7];
        bus.fault_code = rd_u16(&p[8]);

        size_t jpos_off = 10;                 // joint positions
        size_t mpos_off = 10 + joint_num * 2; // motor positions
        size_t tip_off = 10 + joint_num * 4;  // tip tactile / proximity

        if (joints)
        {
            for (size_t i = 0; i < joint_num; i++)
            {
                joints[i].dev_id = i + 1;
                joints[i].joint_position = rd_i16(&p[jpos_off + i * 2]) * 0.01f;
                joints[i].motor_position = rd_i16(&p[mpos_off + i * 2]) * 0.01f;
            }
        }

        if (tips)
        {
            for (size_t i = 0; i < 5; i++)
            {
                tips[i].device = module_str(static_cast<Module>(i + 1));
                tips[i].tip_tactile = rd_i16(&p[tip_off + i * 2]);
                tips[i].tip_proximity_sense = rd_u16(&p[tip_off + 10 + i * 2]);
            }
        }
    }

    void HandInterface::decode_sdo_message(uint16_t &index, uint8_t &subindex, uint32_t &data, const uint8_t *p)
    {
        if (!p)
            return;
        uint8_t type = p[0];
        index = rd_u16(&p[1]);
        subindex = p[3];
        switch (type)
        {
        case 0x4F:
            data = p[4];
            break;
        case 0x4B:
            data = rd_u16(&p[4]);
            break;
        case 0x47:
            data = static_cast<uint32_t>(p[4] | (static_cast<uint16_t>(p[5]) << 8) | (static_cast<uint16_t>(p[6]) << 16));
            break;
        case 0x43:
            data = static_cast<uint32_t>(rd_i32(&p[4]));
            break;
        default:
            data = 0;
            break;
        }
    }

    /* ================================================================== *
     *  Public API
     * ================================================================== */
    Status HandInterface::start_bus(const CanDeviceSettings &cfg)
    {
        device_type_ = cfg.device_type;
        device_index_ = cfg.device_index;
        channel_index_ = cfg.channel_index;
        core_clock_hz_ = cfg.core_clock_hz;
        termination_enabled_ = cfg.termination_enabled;
        arbitration_ = cfg.bit_timing.arbitration;
        data_phase_ = cfg.bit_timing.data_phase;
        hand_id_ = -1;
        hand_side_ = -1;
        devid_ = 0;
        ts_prev_ = 0;
        ts_prev_valid_ = false;
        ts_high_ = 0;
        init_ = false;
        error_msg_.clear();

        Status st = open_can();
        if (st != Status::Ok)
        {
            error_msg_ = "Open Can device failed";
            return st;
        }

        st = configure_can(channel_index_);
        if (st != Status::Ok)
        {
            error_msg_ = "Can device initialize failed";
            return st;
        }

        st = start_can(channel_index_);
        if (st != Status::Ok)
        {
            error_msg_ = "Can device start failed";
            return st;
        }

        time_t start = time(nullptr);
        const int timeout_sec = 5;
        while (!init_)
        {
            time_t now = time(nullptr);
            if (difftime(now, start) >= timeout_sec)
            {
                error_msg_ = "Initialize failed: Receive timeout";
                return Status::Can;
            }
            std::vector<RealtimeData> realtime;
            std::vector<JointSensor> joints;
            std::vector<TipSensor> tips;
            BusInfo bus;
            (void)receive_step(realtime, joints, tips, bus, 0);
        }

        error_msg_.clear();
        return Status::Ok;
    }

    Status HandInterface::close_bus()
    {
        if (!is_open_)
            return Status::Can;
        Status st = close_can();
        if (st == Status::Ok)
        {
            init_ = false;
        }
        else
        {
            error_msg_ = "Can device Close failed";
        }
        return st;
    }

    Status HandInterface::send_command(const std::vector<Command> &cmds)
    {
        if (cmds.empty())
        {
            error_msg_ = "Number of cmds must larger than 0";
            return Status::InvalidArg;
        }

        constexpr size_t kPerFrame = 8;
        size_t total = cmds.size();
        size_t msg_count = (total / kPerFrame) + (total % kPerFrame != 0 ? 1 : 0);

        for (size_t i = 0; i < msg_count; i++)
        {
            size_t chunk = (i == msg_count - 1) ? (total - kPerFrame * i) : kPerFrame;

            uint8_t payload[64] = {0};
            for (size_t j = 0; j < chunk; j++)
            {
                const Command &cmd = cmds[i * kPerFrame + j];
                int16_t joint_angle = static_cast<int16_t>(cmd.cmd_joint_angle * 100.0f);
                int16_t vel_ref = static_cast<int16_t>(cmd.joint_vel_ref * 100.0f);

                uint8_t *b = payload + j * 8;
                b[0] = static_cast<uint8_t>(cmd.dev_id & 0xFF);
                b[1] = static_cast<uint8_t>(static_cast<int>(cmd.control_mode) & 0xFF);
                b[2] = static_cast<uint8_t>(joint_angle & 0xFF);
                b[3] = static_cast<uint8_t>((joint_angle >> 8) & 0xFF);
                b[4] = static_cast<uint8_t>(vel_ref & 0xFF);
                b[5] = static_cast<uint8_t>((vel_ref >> 8) & 0xFF);
                b[6] = static_cast<uint8_t>(cmd.motor_torque_ref & 0xFF);
                b[7] = static_cast<uint8_t>((cmd.motor_torque_ref >> 8) & 0xFF);
            }

            uint32_t id = make_can_id(static_cast<uint32_t>(CanDirection::Rpdo1));
            Status st = transmit_fd(channel_index_, payload, 64, id);
            if (st != Status::Ok)
            {
                error_msg_ = "Failed to transmit data";
                return st;
            }
        }

        error_msg_.clear();
        return Status::Ok;
    }

    Status HandInterface::receive_step(std::vector<RealtimeData> &realtime,
                                       std::vector<JointSensor> &joints,
                                       std::vector<TipSensor> &tips,
                                       BusInfo &bus,
                                       size_t joint_num,
                                       uint32_t *got_out)
    {
        uint32_t got = 0;
        Status st = receive_fd(channel_index_, kReceiveWaitMs, got);
        if (st != Status::Ok)
        {
            error_msg_ = "Canfd frame receive failed";
            return st;
        }
        if (got_out)
            *got_out = got;

        if (!init_)
        {
            if (hand_id_ != -1)
                init_ = true;
            else if (got != 0)
            {
                // CAN ID = (cmd << 7) | devID ; devID = handID(0x60) | slaveID(0x1F)
                devid_ = rx_msgs_[0].id & 0x7fu;
                hand_id_ = static_cast<int>(devid_ & 0x1fu);
                const uint32_t hand_field = devid_ & 0x60u; // 0x40 = left, 0x20 = right
                if (hand_field == 0x20u)
                    hand_side_ = static_cast<int>(HandSide::Left);
                else if (hand_field == 0x40u)
                    hand_side_ = static_cast<int>(HandSide::Right);
                else
                    hand_side_ = -1; // unknown, may still arrive via SDO 0x1018:4
            }
        }
        else if (joint_num == 0)
        {
            error_msg_ = "number of joint must > 0";
            return Status::InvalidArg;
        }

        // Decode directly into caller buffers (reused across calls).
        // Compare the whole device field (handID | slaveID), not just the slave id.
        const uint32_t hand = devid_ ? devid_ : (static_cast<uint32_t>(hand_id_) & 0x1fu);
        for (uint32_t i = 0; i < got; i++)
        {
            const RxMessage &msg = rx_msgs_[i];
            if ((msg.id & 0x7fu) != hand)
                continue;

            uint32_t dir = msg.id >> 7;

            if (dir == static_cast<uint32_t>(CanDirection::Tpdo1) && init_)
            {
                decode_runtime_telemetry(realtime.data(), realtime.size(), msg.data.data(), msg.size);
                for (auto &r : realtime)
                    r.timestamp_ms = msg.timestamp;
            }
            else if (dir == static_cast<uint32_t>(CanDirection::Tpdo2) && init_)
            {
                decode_sensor_telemetry(bus, joints.data(), joint_num, tips.data(), msg.data.data(), msg.size);
                for (auto &j : joints)
                    j.timestamp_ms = msg.timestamp;
                for (auto &t : tips)
                    t.timestamp_ms = msg.timestamp;
                bus.timestamp_ms = msg.timestamp;
            }
            else if (dir == static_cast<uint32_t>(CanDirection::SdoT))
            {
                uint16_t index;
                uint8_t subindex;
                uint32_t data;
                decode_sdo_message(index, subindex, data, msg.data.data());
                // 0x1018:4 左右手: 0 = left, 1 = right (protocol 八)
                if (index == static_cast<uint16_t>(SdoIndex::HandSide) && subindex == 4)
                    hand_side_ = (data == 1) ? static_cast<int>(HandSide::Right)
                                             : static_cast<int>(HandSide::Left);
            }
        }

        error_msg_.clear();
        return Status::Ok;
    }

    std::string HandInterface::last_error() const
    {
        return error_msg_;
    }

    std::string HandInterface::hand_side() const
    {
        if (hand_side_ == static_cast<int>(HandSide::Left))
            return "left";
        if (hand_side_ == static_cast<int>(HandSide::Right))
            return "right";
        return "unknown";
    }

    int HandInterface::hand_id() const
    {
        return hand_id_;
    }

    uint32_t HandInterface::dev_id() const
    {
        return devid_;
    }

} // namespace grip
