#pragma once
#include <cstdint>
#include <unordered_map>
#include <string>
#include <vector>

#include "interface.h"

using JointTels = grip_joint_sensor_t;
using TipTels = grip_tip_sensor_t;
using BusData = grip_bus_info_t;
using RealtimeData = grip_realtime_data_t;
using CmdStruct = grip_command_t;

struct DevState
{
    float position = 0.0f;
    float velocity = 0.0f;
    float effort = 0.0f;
};

struct JointGroup
{
    std::unordered_map<std::string, int> control_mode; /* joint_name -> control_mode */
    std::unordered_map<std::string, DevState> joints;  /* joint_name -> joint_state (no data for wrist) */
    std::unordered_map<std::string, DevState> motors;  /* joint_name -> motor_state */
    TipTels tip_sensor;                                /* module_name -> tip_state (no data for wrist) */
    int32_t timestamp;
};

using HandStateMap = std::unordered_map<std::string, JointGroup>;
