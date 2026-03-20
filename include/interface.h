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
        GRIP_WRIST_JOINT_PITCH = 0x01,
        GRIP_WRIST_JOINT_YAW = 0x02,
        GRIP_FINGER_JOINT_PINKY = 0x03,
        GRIP_FINGER_JOINT_MIDDLE = 0x04,
        GRIP_FINGER_JOINT_THUMB = 0x05,
        GRIP_FINGER_JOINT_RING = 0x06,
        GRIP_FINGER_JOINT_INDEX = 0x07,
        // GRIP_DEVICE_CENTER_BOARD = 0x1E,
        // GRIP_DEVICE_BROADCAST = 0x1F
    } grip_device_t;
    GRIP_C_API const char *grip_device_str(grip_device_t d);

    typedef enum
    {
        GRIP_HAND_LEFT = 0x20,
        GRIP_HAND_RIGHT = 0x40
    } grip_hand_side_t;

    typedef enum
    {
        GRIP_PDO_RECEIVE = 0x180,
        GRIP_PDO_SEND = 0x200
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

    GRIP_C_API char **grip_fault_code_str(uint8_t code);

    typedef struct
    {
        grip_device_t dev_id;
        grip_control_mode_t control_mode;
        int32_t motor_position; /* hall-counts */
        float joint_position;   /* degree */
        int32_t motor_torque;   /* normalization (value 0-1000) */
        float joint_speed;      /* degree/s */
        float tip_tactile;      /* N */
        float motor_speed;      /* hall-counts/s */
        int32_t temp_cC;        /* centi-C */
        uint8_t fault_code;
        uint32_t timestamp_ms;
    } grip_telemetry_t;

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

    /* ---------- Control interface (minimal C port) ---------- */
    /* This C port focuses on: start bus, submit targets, periodic TX, RX telemetry decode, snapshot. */
    GRIP_C_API grip_can_device_settings_t *grip_device_setting_create(uint32_t device_type,
                                                                      uint32_t device_index,
                                                                      uint32_t channel_index);
    GRIP_C_API grip_control_t *grip_control_create(int hand_id, int is_left);
    GRIP_C_API grip_status_t grip_control_start_bus(grip_control_t *ctl, grip_can_device_settings_t *cfg);
    GRIP_C_API grip_status_t grip_control_close_bus(grip_control_t *ctl);

    GRIP_C_API grip_status_t grip_send_command(grip_control_t *ctl, grip_command_t *cmd, size_t len);
    GRIP_C_API grip_status_t grip_receive_step(grip_control_t *ctl, grip_telemetry_t *tels, size_t len);

    const char *grip_get_last_error(grip_control_t *ctl);

#ifdef __cplusplus
}
#endif
