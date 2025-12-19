#include "grip/ModuleRt.hpp"
#include <algorithm>
#include <iostream>

namespace grip
{
    ModuleLayout build_module_layout(const ModuleState &ms)
    {
        ModuleLayout L;

        auto add_dof = [&](const std::string &d)
        {
            if (L.dof_index.count(d))
                return;
            uint32_t idx = static_cast<uint32_t>(L.dof_names.size());
            L.dof_names.push_back(d);
            L.dof_index.emplace(d, idx);
        };
        auto add_joint = [&](const std::string &j)
        {
            if (L.joint_index.count(j))
                return;
            uint32_t idx = static_cast<uint32_t>(L.joint_names.size());
            L.joint_names.push_back(j);
            L.joint_index.emplace(j, idx);
        };
        auto add_motor = [&](int m)
        {
            if (L.motor_index.count(m))
                return;
            uint32_t idx = static_cast<uint32_t>(L.motor_ids.size());
            L.motor_ids.push_back(m);
            L.motor_index.emplace(m, idx);
        };

        // Topology primarily from dofs
        for (const auto &dof : ms.dofs)
        {
            add_dof(dof.name);
            for (const auto &j : dof.joints)
                add_joint(j);
            for (int m : dof.motors)
                add_motor(m);
        }
        // Ensure any extra keys in state maps are included
        for (const auto &[jname, _] : ms.joints)
            add_joint(jname);
        for (const auto &[mid, _] : ms.motors)
            add_motor(mid);

        // Bindings
        L.bindings.resize(L.dof_names.size());
        for (uint32_t di = 0; di < L.dof_names.size(); ++di)
        {
            L.bindings[di].dof_i = di;
        }

        for (const auto &dof : ms.dofs)
        {
            auto it = L.dof_index.find(dof.name);
            if (it == L.dof_index.end())
                continue;
            uint32_t di = it->second;

            auto &b = L.bindings[di];
            b.joint_is.clear();
            b.motor_is.clear();

            b.joint_is.reserve(dof.joints.size());
            b.motor_is.reserve(dof.motors.size());

            for (const auto &j : dof.joints)
            {
                auto jit = L.joint_index.find(j);
                if (jit != L.joint_index.end())
                    b.joint_is.push_back(jit->second);
            }
            for (int m : dof.motors)
            {
                auto mit = L.motor_index.find(m);
                if (mit != L.motor_index.end())
                    b.motor_is.push_back(mit->second);
            }
        }

        return L;
    }

    ModuleInputSnapshot make_module_input(const ModuleLayout &L)
    {
        ModuleInputSnapshot in;
        in.dof_target.resize(L.dof_names.size(), 0.f);

        in.joint_pos.resize(L.joint_names.size(), 0.f);
        in.joint_vel.resize(L.joint_names.size(), 0.f);
        in.joint_eff.resize(L.joint_names.size(), 0.f);

        in.motor_counts.resize(L.motor_ids.size(), 0);
        in.motor_speed.resize(L.motor_ids.size(), 0);
        in.motor_iq_mA.resize(L.motor_ids.size(), 0);
        return in;
    }

    ModuleOutputSnapshot make_module_output(const ModuleLayout &L)
    {
        ModuleOutputSnapshot out;
        out.motor_pos_cmd.resize(L.motor_ids.size(), 0.f);
        out.motor_vel_cmd.resize(L.motor_ids.size(), 0.f);
        out.motor_cur_cmd.resize(L.motor_ids.size(), 0.f);
        return out;
    }

    void pack_module_inputs(const ModuleLayout &L, const ModuleState &ms, ModuleInputSnapshot &in)
    {
        // DOF targets
        for (const auto &d : ms.dofs)
        {
            auto it = L.dof_index.find(d.name);
            if (it == L.dof_index.end())
                continue;
            uint32_t di = it->second;
            if (di < in.dof_target.size())
                in.dof_target[di] = d.target;
        }

        // joint states
        for (const auto &[jname, js] : ms.joints)
        {
            auto it = L.joint_index.find(jname);
            if (it == L.joint_index.end())
                continue;
            uint32_t ji = it->second;
            if (ji < in.joint_pos.size())
                in.joint_pos[ji] = js.position;
            if (ji < in.joint_vel.size())
                in.joint_vel[ji] = js.velocity;
            if (ji < in.joint_eff.size())
                in.joint_eff[ji] = js.effort;
        }

        // motor telemetry
        for (const auto &[mid, tel] : ms.motors)
        {
            auto it = L.motor_index.find(mid);
            if (it == L.motor_index.end())
                continue;
            uint32_t mi = it->second;
            if (mi < in.motor_counts.size())
                in.motor_counts[mi] = tel.encoder_counts;
            if (mi < in.motor_speed.size())
                in.motor_speed[mi] = tel.speed;
            if (mi < in.motor_iq_mA.size())
                in.motor_iq_mA[mi] = tel.iq_mA;
        }
    }

    void apply_module_outputs(const ModuleLayout &L,
                              const ModuleOutputSnapshot &out,
                              std::unordered_map<int, MotorCommand> &motor_cmds)
    {
        const size_t n = L.motor_ids.size();
        for (size_t mi = 0; mi < n; ++mi)
        {
            const int motor_id = L.motor_ids[mi];
            MotorCommand &cmd = motor_cmds[motor_id];
            if (mi < out.motor_pos_cmd.size())
                cmd.position_cmd = out.motor_pos_cmd[mi];
            if (mi < out.motor_vel_cmd.size())
                cmd.velocity_cmd = out.motor_vel_cmd[mi];
            if (mi < out.motor_cur_cmd.size())
                cmd.current_cmd = out.motor_cur_cmd[mi];
        }
    }
} // namespace grip
