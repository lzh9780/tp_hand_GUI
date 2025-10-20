#pragma once
#include <cstdint>

namespace grip {

enum class JointDevice : uint32_t {
    PINKY_FINGER  = 0x03,
    MIDDLE_FINGER = 0x04,
    THUMB         = 0x05,
    RING_FINGER   = 0x06,
    INDEX_FINGER  = 0x07,
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
    ERROR_MESSAGE   = 0x180
};

enum class MotorMode : uint8_t { TORQUE_ZERO=0x00, TORQUE_STEP=0x01, SPEED=0x02, HALL_GUARD=0x03, CALIBRATE=0xFE };
enum class ParamField : uint8_t { MAX_CURRENT=0x01, MAX_SPEED=0x02, CAN_MODE=0x03, ENCODER_POS=0x04 };
enum class FaultCode : uint16_t { MOTOR_STALL=0x80 };

struct Telemetry {
    int16_t  speed = 0;
    int16_t  iq_mA = 0;
    int32_t  encoder_counts = 0;
    uint16_t bus_mV = 0;
    int8_t   motor_degC = 0;
    uint16_t fault = 0;
    uint8_t  state = 0;
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

} // namespace grip
