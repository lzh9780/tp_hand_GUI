#pragma once
#include <cstdint>
#include <unordered_map>
#include <string>
#include <vector>

namespace grip {

enum class JointDevice : uint32_t {
    // PINKY_FINGER  = 0x03,
    // MIDDLE_FINGER = 0x04,
    // THUMB         = 0x05,
    // RING_FINGER   = 0x06,
    // INDEX_FINGER  = 0x07,
    CENTER_BOARD  = 0x1E,
    BROADCAST     = 0x1F
};

enum class HandSide  : uint32_t { LEFT = 0x00, RIGHT = 0x20 };
enum class Direction : uint32_t { FORWARD = 0x400 };

enum class MsgKind : uint32_t {
    DEVICE_VERSION  = 0x00,
    SET_MOTOR_PARAM = 0x40,
    MOTOR_TELEMETRY = 0x80,
    SENSOR_TELEMETRY= 0xC0,
    MOTOR_COMMAND   = 0x100,
    BOARDSTATE   = 0x180
};

enum class MotorMode : uint8_t { TORQUE_ZERO=0x00, TORQUE_STEP=0x01, SPEED=0x02, HALL_GUARD=0x03, CALIBRATE=0xFE };
enum class ParamField : uint8_t { MAX_CURRENT=0x01, MAX_SPEED=0x02, CAN_MODE=0x03, ENCODER_POS=0x04, JOINT_SENSOR_RESET=0x07 };
enum class FaultCode : uint16_t { MOTOR_STALL=0x80 };

struct Telemetry {
    uint8_t control_mode = 0;
    int16_t motor_position = 0;
    int16_t joint_position = 0;
    int16_t iq_mA = 0;
    int16_t joint_speed = 0;
    int16_t tip_tactile = 0;
    int16_t motor_speed = 0;
    uint8_t temperature = 0;
    uint8_t fault = 0;
};

struct JointAngles {
    float thumb_mcp = 0;
    float thumb_ip = 0;
    float index_mcp = 0;
    float index_pip = 0;
    float index_dip = 0;
    float middle_mcp = 0;
    float middle_pip = 0;
    float middle_dip = 0;
    float ring_mcp = 0;
    float ring_pip = 0;
    float ring_dip = 0;
    float pinky_mcp = 0;
    float pinky_pip = 0;
    float pinky_dip = 0;
};

struct JointState
{
    float position = 0.0f;
    float velocity = 0.0f;
    float effort   = 0.0f;
};

struct DofState
{
    std::string name;
    float target{0.0f};
    std::vector<std::string> joints; // expected: [mcp,pip,dip] or [mcp] etc
    std::vector<int> motors;         // expected: [motor_id] (1 motor per dof)
};

struct ModuleState
{
    std::vector<DofState> dofs;
    std::unordered_map<std::string, JointState> joints; // name -> state
    std::unordered_map<int, Telemetry> motors;          // motor_id -> telemetry
    int control_mode = 0;
};

using HandStateMap = std::unordered_map<std::string, ModuleState>;
using StateMap = std::unordered_map<int, Telemetry>;

// Motor command separate from Telemetry (feedback)
struct MotorCommand
{
    float position_cmd = 0.0f; // optional
    float velocity_cmd = 0.0f; // rpm or your unit
    float current_cmd  = 0.0f; // mA or your unit
};

struct CmdStruct
{
    uint8_t joint_id = 0;
    uint8_t control_mode = 0;
    uint16_t cmd_joint_angle = 0;
    uint16_t joint_vel_ref = 0;
    uint16_t motor_current_ref = 0;
};

} // namespace grip
