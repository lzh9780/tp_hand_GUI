#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
#if defined(GRIP_C_BUILD_DLL)
#define GRIP_C_API __declspec(dllexport)
#else
#define GRIP_C_API __declspec(dllimport)
#endif
#else
#if defined(__GNUC__) && __GNUC__ >= 4
#define GRIP_C_API __attribute__((visibility("default")))
#else
#define GRIP_C_API
#endif
#endif

    /* ---------- Errors ---------- */
    typedef enum grip_status_e
    {
        GRIP_OK = 0,
        GRIP_E_INVALID_ARG = 1,
        GRIP_E_IO = 2,
        GRIP_E_PARSE = 3,
        GRIP_E_NO_MEM = 4,
        GRIP_E_INTERNAL = 5,
        GRIP_E_CAN = 6,
    } grip_status_t;

    GRIP_C_API const char *grip_status_str(grip_status_t s);

    /* ---------- Protocol / telemetry defs ---------- */
    typedef enum
    {
        GRIP_INVAILD_DEVICE = 0x00,
        GRIP_WRIST_JOINT_YAW = 0x01,
        GRIP_WRIST_JOINT_PITCH = 0x02,
        GRIP_FINGER_JOINT_THUMB_MCP = 0x03,
        GRIP_FINGER_JOINT_THUMB_ROT = 0x04,
        GRIP_FINGER_JOINT_INDEX = 0x05,
        GRIP_FINGER_JOINT_MIDDLE = 0x06,
        GRIP_FINGER_JOINT_RING = 0x07,
        GRIP_FINGER_JOINT_PINKY = 0x08,
    } grip_device_t;
    GRIP_C_API const char *grip_device_str(grip_device_t d);
    GRIP_C_API const char *grip_finger_str(grip_device_t d);

    typedef enum
    {
        GRIP_DEV_WRIST = 0x00,
        GRIP_DEV_THUMB = 0x01,
        GRIP_DEV_INDEX = 0x02,
        GRIP_DEV_MIDDLE = 0x03,
        GRIP_DEV_RING = 0x04,
        GRIP_DEV_PINKY = 0x05,
    } grip_module_t;
    GRIP_C_API const char *grip_module_str(grip_module_t d);

    typedef enum
    {
        GRIP_HAND_LEFT = 0x01,
        GRIP_HAND_RIGHT = 0x02
    } grip_hand_side_t;

    typedef enum
    {
        CANOPEN_CMD_NMT = 0x00, //
        CANFD_CMD_SYNC = 0x01,  //
        CANFD_CMD_TPDO1 = 0x03, // Runtime data
        CANFD_CMD_RPDO1 = 0x04, // Joint cmds
        CANFD_CMD_TPDO2 = 0x05, // Sensor data
        CANFD_CMD_RPDO2 = 0x06, //
        CANFD_CMD_TPDO3 = 0x07, //
        CANFD_CMD_RPDO3 = 0x08, //
        CANFD_CMD_TPDO4 = 0x09, //
        CANFD_CMD_RPDO4 = 0x0A, //
        CANFD_CMD_SDOT = 0x0B,  //
        CANFD_CMD_SDOR = 0x0C,  //
    } grip_direction_t;

    typedef enum
    {
        GRIP_CTRLMODE_ZERO_TORQUE = 0x00,
        GRIP_CTRLMODE_CURRENT = 0x10,
        GRIP_CTRLMODE_HALLPOS = 0x20,
        GRIP_CTRLMODE_JOINT_POS = 0x30,
        GRIP_CTRLMODE_IMPEDANCE = 0x40,
        GRIP_CTRLMODE_CASCADED_PID = 0x50,
        GRIP_CTRLMODE_MIT_TORQUE = 0x60,
        GRIP_CTRLMODE_HYBRID = 0x70,
        GRIP_CTRLMODE_FORCE_POS_HYBRID = 0x80,
        GRIP_CTRLMODE_EMERGENCY = 0x90,
        GRIP_CTRLMODE_HM = 0xA0,
    } grip_control_mode_t;
    GRIP_C_API const char *grip_control_mode_str(grip_control_mode_t c);
    GRIP_C_API const uint16_t grip_control_mode_id(const char *mode_name);

    GRIP_C_API char **grip_fault_code_str(uint8_t code);

    typedef struct
    {
        grip_device_t dev_id;
        grip_control_mode_t control_mode;
        int32_t motor_position; /* hall-counts */
        float joint_position;   /* degree */
        float curr_pos;      /* hall-counts/s */
        float curr_vel;      /* degree/s */
        int32_t curr_torque;   /* normalization (value 0-1000) */
        // float tip_tactile;      /* N */
        // int32_t temp_cC;        /* centi-C */
        // uint8_t fault_code;
        uint32_t timestamp_ms;
    } grip_joint_sensor_t;

    typedef struct
    {
        const char *device;
        int16_t tip_tactile;
        int16_t tip_proximity_sense;
        uint32_t timestamp_ms;
    } grip_tip_sensor_t;

    typedef struct
    {
        grip_device_t dev_id;
        grip_control_mode_t control_mode;
        float cmd_joint_angle;    /* degree(joint mode) hall-counts(hall mode) */
        float joint_vel_ref;      /* degree/s */
        int32_t motor_torque_ref; /* normalization (value 0-1000) */
    } grip_command_t;

    typedef struct
    {
        grip_status_t status;
        char msg[50];
    } grip_status_msg_t;

    /* ---------- Opaque handles ---------- */
    typedef struct grip_can_device_settings grip_can_device_settings_t;
    typedef struct grip_can_bus grip_can_bus_t;
    typedef struct grip_control grip_control_t;
    typedef struct grip_can_frame grip_can_frame_t;
    typedef struct grip_telemetry grip_telemetry_t;
    typedef struct grip_sensor_telemetry grip_sensor_telemetry_t;

    /* ---------- Control interface (minimal C port) ---------- */
    GRIP_C_API grip_can_device_settings_t *grip_device_setting_create(uint32_t device_type,
                                                                      uint32_t device_index,
                                                                      uint32_t channel_index);
    GRIP_C_API grip_control_t *grip_control_create(int hand_id, int is_left);
    GRIP_C_API grip_status_t grip_control_start_bus(grip_control_t *ctl, grip_can_device_settings_t *cfg);
    GRIP_C_API grip_status_t grip_control_close_bus(grip_control_t *ctl);

    GRIP_C_API grip_status_t grip_send_command(grip_control_t *ctl, grip_command_t *cmd, size_t len);
    GRIP_C_API grip_status_t grip_receive_step(grip_control_t *ctl, grip_joint_sensor_t *tels,
                                               grip_tip_sensor_t *tips, size_t joint_num);
    // GRIP_C_API grip_status_t grip_ota_update(grip_control_t *ctl, char* file_name);

    GRIP_C_API const char *grip_get_last_error(grip_control_t *ctl);

#ifdef __cplusplus
}
#endif
