#pragma once
#include "can_defs.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace grip
{
    // Module-local layout (built once after config is loaded)
    struct ModuleLayout
    {
        std::vector<std::string> dof_names;
        std::vector<std::string> joint_names;
        std::vector<int> motor_ids;

        std::unordered_map<std::string, uint32_t> dof_index;
        std::unordered_map<std::string, uint32_t> joint_index;
        std::unordered_map<int, uint32_t> motor_index;

        struct DofBinding
        {
            uint32_t dof_i = 0;
            std::vector<uint32_t> joint_is; // in order: [MCP,PIP,DIP] if provided
            std::vector<uint32_t> motor_is; // usually size 1 for finger, size 2 for wrist pitch
        };
        std::vector<DofBinding> bindings;
    };

    // Module-local runtime snapshots (fixed sizes once layout is built)
    struct ModuleInputSnapshot
    {
        std::vector<float> dof_target; // Ndof

        std::vector<float> joint_pos; // Njoint
        std::vector<float> joint_vel; // Njoint
        std::vector<float> joint_eff; // Njoint

        std::vector<int32_t> motor_counts; // Nmotor
        std::vector<int16_t> motor_speed;  // Nmotor
        std::vector<int16_t> motor_iq_mA;  // Nmotor
    };

    struct ModuleOutputSnapshot
    {
        std::vector<float> motor_pos_cmd; // Nmotor (optional)
        std::vector<float> motor_vel_cmd; // Nmotor
        std::vector<float> motor_cur_cmd; // Nmotor
    };

    // Build module-local layout from a single ModuleState
    ModuleLayout build_module_layout(const ModuleState &ms);

    // Allocate module-local snapshots
    ModuleInputSnapshot make_module_input(const ModuleLayout &L);
    ModuleOutputSnapshot make_module_output(const ModuleLayout &L);

    // Pack one module's current values into snapshot
    void pack_module_inputs(const ModuleLayout &L, const ModuleState &ms, ModuleInputSnapshot &in);

    // Apply module output snapshot to motor command map (motor_id -> MotorCommand)
    void apply_module_outputs(const ModuleLayout &L,
                              const ModuleOutputSnapshot &out,
                              std::unordered_map<int, MotorCommand> &motor_cmds);
} // namespace grip
