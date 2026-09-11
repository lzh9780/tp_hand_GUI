#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <array>

#include "settings.hpp"

namespace grip
{

/* Raw received CAN-FD message (internal representation). */
struct RxMessage
{
    uint32_t id = 0;
    uint32_t timestamp = 0;
    std::array<uint8_t, 64> data{};
    uint8_t size = 0;
};

/* ------------------------------------------------------------------ *
 *  Errors / status
 * ------------------------------------------------------------------ */
enum class Status
{
    Ok = 0,
    InvalidArg = 1,
    Io = 2,
    Parse = 3,
    NoMem = 4,
    Internal = 5,
    Can = 6,
};

const char *status_str(Status s);

/* ------------------------------------------------------------------ *
 *  Protocol definitions
 * ------------------------------------------------------------------ */
enum class Module
{
    Wrist = 0x00,
    Thumb = 0x01,
    Index = 0x02,
    Middle = 0x03,
    Ring = 0x04,
    Pinky = 0x05,
};

const char *module_str(Module d);

enum class HandSide
{
    Left = 0,
    Right = 1,
};

enum class ControlMode
{
    ZeroTorque = 0x00,
    // Current = 0x10,
    HallPos = 0x20,
    JointPos = 0x30,
    // Impedance = 0x40,
    // CascadedPid = 0x50,
    MitTorque = 0x60,
    // Hybrid = 0x70,
    // ForcePosHybrid = 0x80,
    // Emergency = 0x90,
    // Homing = 0xA0,
};

const char *control_mode_str(ControlMode c);
ControlMode control_mode_from_string(const std::string &mode_name);

/* Fault codes (bit positions in the sensor telemetry error word). */
std::vector<std::string> fault_code_str(uint16_t code);

/* ------------------------------------------------------------------ *
 *  CAN direction / SDO index constants
 * ------------------------------------------------------------------ */
enum class CanDirection : uint32_t
{
    Nmt = 0x00,
    Sync = 0x01,
    Tpdo1 = 0x03, // runtime data
    Rpdo1 = 0x04, // joint commands
    Tpdo2 = 0x05, // sensor data
    Rpdo2 = 0x06,
    Tpdo3 = 0x07,
    Rpdo3 = 0x08,
    Tpdo4 = 0x09,
    Rpdo4 = 0x0A,
    SdoT = 0x0B,
    SdoR = 0x0C,
};

enum class SdoIndex : uint16_t
{
    WorkMode = 0x1007,
    HardwareVersion = 0x1009,
    SoftwareVersion = 0x100A,
    HandSide = 0x1018,
};

/* ------------------------------------------------------------------ *
 *  Data structures (realtime / command / sensor)
 * ------------------------------------------------------------------ */
struct RealtimeData
{
    size_t dev_id = 0;
    ControlMode control_mode = ControlMode::ZeroTorque;
    float curr_pos = 0.0f;   /* hall-counts (hall mode) / degree (joint mode) */
    float curr_vel = 0.0f;
    int32_t curr_torque = 0; /* motor current, mA (raw feedback, not normalised) */
    uint32_t timestamp_ms = 0;
};

struct JointSensor
{
    size_t dev_id = 0;
    float motor_position = 0.0f; /* mm */
    float joint_position = 0.0f; /* degree */
    uint32_t timestamp_ms = 0;
};

struct TipSensor
{
    std::string device;
    int16_t tip_tactile = 0;
    int16_t tip_proximity_sense = 0;
    uint32_t timestamp_ms = 0;
};

struct BusInfo
{
    uint16_t bus_current = 0;
    float bus_voltage = 0.0f;
    uint8_t temp1_cC = 0;
    uint8_t temp2_cC = 0;
    uint16_t fault_code = 0;
    uint32_t timestamp_ms = 0;
};

struct Command
{
    size_t dev_id = 0;
    ControlMode control_mode = ControlMode::ZeroTorque;
    float cmd_joint_angle = 0.0f;    /* degree (joint mode) / hall-counts (hall mode) */
    float joint_vel_ref = 0.0f;      /* degree/s */
    int32_t motor_torque_ref = 0;    /* mA in current */
};

/* ------------------------------------------------------------------ *
 *  HandInterface — C++ communication interface
 *  Replaces the legacy C "interface.c / interface.h" + libtphandapi.so.
 * ------------------------------------------------------------------ */
class HandInterface
{
public:
    HandInterface() = default;
    ~HandInterface();

    HandInterface(const HandInterface &) = delete;
    HandInterface &operator=(const HandInterface &) = delete;

    /* ---------- lifecycle ---------- */
    Status start_bus(const CanDeviceSettings &cfg);
    Status close_bus();

    /* ---------- command / receive ---------- */
    Status send_command(const std::vector<Command> &cmds);

    /* One receive tick. When `got` is non-null it receives the number of frames
       that were actually decoded in this call — callers must skip filtering /
       state updates when it is 0, otherwise the same stale sample is filtered
       over and over again (which cancels the smoothing). */
    Status receive_step(std::vector<RealtimeData> &realtime,
                        std::vector<JointSensor> &joints,
                        std::vector<TipSensor> &tips,
                        BusInfo &bus,
                        size_t joint_num,
                        uint32_t *got = nullptr);

    /* ---------- query ---------- */
    std::string last_error() const;
    std::string hand_side() const;   /* "left" / "right" / "unknown" */
    int hand_id() const;             /* slaveID (devID & 0x1F) */
    uint32_t dev_id() const;         /* full device field: devID & 0x7F (handID | slaveID) */
    bool initialized() const { return init_; }

private:
    /* CAN bus low-level helpers (ZLG VCI_*) */
    Status open_can();
    Status configure_can(uint32_t channel_index);
    Status start_can(uint32_t channel);
    Status close_can();
    Status transmit_fd(uint32_t channel, const uint8_t *data, uint32_t len, uint32_t id);
    Status receive_fd(uint32_t channel, uint32_t wait_ms, uint32_t &got);
    Status send_sdo_cmd(uint16_t index, uint8_t subindex);

    /* protocol decode — write directly into caller buffers (no temp allocation) */
    static void decode_runtime_telemetry(RealtimeData *out, size_t cap, const uint8_t *p, uint8_t n);
    static void decode_sensor_telemetry(BusInfo &bus,
                                        JointSensor *joints, size_t joint_num,
                                        TipSensor *tips,
                                        const uint8_t *p, uint8_t n);
    static void decode_sdo_message(uint16_t &index, uint8_t &subindex, uint32_t &data, const uint8_t *p);

    uint32_t make_can_id(uint32_t dir) const;

    /* Driver timestamps are 0.1 ms ticks on a wrapping 32-bit counter
       (ZCAN_MSG_HDR::ts). Convert them to monotonic milliseconds. */
    uint32_t to_millis(uint32_t ts_ticks);

    /* fixed-size RX buffers reused across calls (avoid per-frame heap alloc) */
    static constexpr size_t kMaxRx = 64;
    std::array<RxMessage, kMaxRx> rx_msgs_{};

    /* ---------- state ---------- */
    /* A non-blocking receive would busy-spin; wait a little for frames instead. */
    static constexpr uint32_t kReceiveWaitMs = 1;

    uint32_t device_type_ = 0;
    uint32_t device_index_ = 0;
    uint32_t channel_index_ = 0;
    uint32_t core_clock_hz_ = 60000000;
    bool termination_enabled_ = true;
    Timing arbitration_{14, 3, 3, 2};
    Timing data_phase_{1, 0, 0, 2};
    bool is_open_ = false;
    bool init_ = false;
    uint32_t devid_ = 0;   /* devID = handID(0x60) | slaveID(0x1F), learned from the first frame */
    int hand_id_ = -1;
    int hand_side_ = -1;   /* HandSide value: 0 = left, 1 = right, -1 = unknown */

    /* timestamp conversion state (0.1 ms ticks -> monotonic ms) */
    uint32_t ts_prev_ = 0;
    bool ts_prev_valid_ = false;
    uint64_t ts_high_ = 0;

    std::string error_msg_;
};

} // namespace grip
