#include "grip/settings.hpp"
#include <yaml-cpp/yaml.h>
#include <set>
#include <sstream>
#include <cmath>
#include <iostream>

namespace grip
{

    static bool valid_device(const std::string &s)
    {
        static const std::set<std::string> k{"THUMB", "INDEX_FINGER", "MIDDLE_FINGER", "RING_FINGER", "PINKY_FINGER"};
        return k.count(s) > 0;
    }
    static auto rt(const YAML::Node &n) { return Timing{n["tseg1"].as<int>(), n["tseg2"].as<int>(), n["sjw"].as<int>(), n["brp"].as<int>()}; }

    AppSettings load_app_settings(const std::string &yaml_path)
    {
        YAML::Node y = YAML::LoadFile(yaml_path);
        AppSettings s;

        if (auto n = y["can_device"])
        {
            s.can_device.device_type = n["device_type"].as<uint32_t>(s.can_device.device_type);
            s.can_device.device_index = n["device_index"].as<uint32_t>(s.can_device.device_index);
            s.can_device.channel_index = n["channel_index"].as<uint32_t>(s.can_device.channel_index);
            s.can_device.core_clock_hz = n["core_clock_hz"].as<uint32_t>(s.can_device.core_clock_hz);
            s.can_device.termination_enabled = n["termination_enabled"].as<bool>(s.can_device.termination_enabled);
            if (auto t = n["bit_timing"])
            {
                s.can_device.bit_timing.arbitration = rt(t["arbitration"]);
                s.can_device.bit_timing.data_phase = rt(t["data_phase"]);
            }
        }
        if (auto n = y["models"])
            for (const auto &it : n)
                s.models.push_back(it.as<std::string>());
        validate_settings(s, yaml_path);
        return s;
    }

    DeviceSettings load_device_settings(const std::string& yaml_path)
    {
        YAML::Node y = YAML::LoadFile(yaml_path);
        DeviceSettings device;

        if (auto n = y["type"]) {
            device.device_type = n.as<std::string>() == "hand" ? 0 : (n.as<std::string>() == "linear_actuator" ? 1 : 2);
        }

        if (auto hardware = y["hardware_version"]) 
            device.hardware_version = hardware.as<int32_t>(device.hardware_version);

        if (auto software = y["software_version"]) 
            device.software_version = software.as<int32_t>(device.software_version);
        
        return device;
    }


    HandSettings load_hand_settings(const std::string& yaml_path)
    {
        YAML::Node y = YAML::LoadFile(yaml_path);
        HandSettings hand;

        if (auto n = y["hand_struct"]) {
            
            for (auto m = n.begin(); m != n.end(); ++m)
                {
                    ModuleStruct ms;
                    int counter = 0;
                    for (const auto name : m->second["joints"]) {
                        JointInfo j;
                        j.name = name.as<std::string>();
                        j.route.first = m->second["route"][counter]["min"].as<float_t>(j.route.first);
                        j.route.second = m->second["route"][counter]["max"].as<float_t>(j.route.second);
                        ms.joints.push_back(j);
                        counter += 1;
                    }
                    for (const auto motor : m->second["motors"]) {
                        // MotorInfo mi;
                        // mi.id = motor["id"].as<int32_t>(mi.id);
                        // for (const auto p : motor["param"]) {
                        //     mi.joint_map.push_back(p.as<int32_t>());
                        // }
                        // std::cout << mi.id << '\n';
                        ms.motors.push_back(motor["id"].as<int32_t>());
                        
                    }
                    hand.structs[m->first.as<std::string>()] = std::move(ms);
                }
        } 

        if (auto p = y["control_policy"]) {
            for (auto const policy : p) {
                hand.control_policies.push_back(policy.as<std::string>());
            }
        }

        if (auto p = y["wrist_control_policy"]) {
            for (auto const policy : p) {
                hand.wrist_control.push_back(policy.as<std::string>());
            }
        }

        if (auto n = y["native_policy"])
        {
            if (auto p = n["pid"])
            {
                hand.native_policy.pid.kp = p["kp"].as<double>(hand.native_policy.pid.kp);
                hand.native_policy.pid.ki = p["ki"].as<double>(hand.native_policy.pid.ki);
                hand.native_policy.pid.kd = p["kd"].as<double>(hand.native_policy.pid.kd);
            }
            if (auto lim = n["current_limits_mA"])
            {
                hand.native_policy.min_current_mA = lim["min"].as<int32_t>(hand.native_policy.min_current_mA);
                hand.native_policy.max_current_mA = lim["max"].as<int32_t>(hand.native_policy.max_current_mA);
            }
            hand.native_policy.initial_theta_rad = n["initial_theta_rad"].as<double>(hand.native_policy.initial_theta_rad);
            if (auto g = n["geometry"])
            {
                hand.native_policy.tendon_fix_mm = g["tendon_fix_mm"].as<double>(hand.native_policy.tendon_fix_mm);
                hand.native_policy.tendon_port_mm = g["tendon_port_mm"].as<double>(hand.native_policy.tendon_port_mm);
                hand.native_policy.coupling_ratio = g["coupling_ratio"].as<int>(hand.native_policy.coupling_ratio);
            }
            if (auto sd = n["screw_drive"])
            {
                hand.native_policy.pitch_mm_per_rev = sd["pitch_mm_per_rev"].as<double>(hand.native_policy.pitch_mm_per_rev);
                hand.native_policy.counts_per_rev = sd["counts_per_rev"].as<int>(hand.native_policy.counts_per_rev);
            }
            if (auto jt = n["joint_tables"])
            {
                for (auto it = jt.begin(); it != jt.end(); ++it)
                {
                    NativeJointConfig jc;
                    jc.linear_zero_counts = it->second["linear_zero_counts"].as<int32_t>(jc.linear_zero_counts);
                    if (auto ff = it->second["feedforward_mA"])
                        for (auto v : ff)
                            jc.feedforward_mA.push_back(v.as<double>());
                    hand.native_policy.joint_tables[it->first.as<std::string>()] = std::move(jc);
                }
            }
        }

        return hand;
    }

    ActuatorSettings load_actuator_settings(const std::string& yaml_path)
    {
        YAML::Node y = YAML::LoadFile(yaml_path);
        ActuatorSettings act;

        if (auto r = y["route"]) {
            act.route.first=r["min"].as<float_t>(act.route.first);
            act.route.second=r["max"].as<float_t>(act.route.second);
        }

        if (auto mc = y["max_current"]) {
            act.max_current=mc.as<int32_t>(act.max_current);
        }

        if (auto mv = y["max_velocity"]) {
            act.max_vel=mv.as<int32_t>(act.max_vel);
        }

        if (auto p = y["psi_f"]) {
            act.psi_f=p.as<float_t>(act.psi_f);
        }

        if (auto p = y["n_pole_pairs"]) {
            act.n_pole_pairs=p.as<float_t>(act.n_pole_pairs);
        }

        if (auto p = y["device_id"]) {
            act.device_id=p.as<float_t>(act.device_id);
        }

        return act;
    }

    void validate_settings(const AppSettings &s, const std::string &path)
    {
        std::vector<std::string> errs;
        auto add = [&](std::string m)
        { errs.push_back(path + " — " + m); };
        if (s.models.empty())
            add("models list cannot be empty");
        // else
        // {
        //     std::set<std::string> uniq;
        //     for (size_t i = 0; i < s.models.size(); ++i)
        //     {
        //         if (!valid_device(s.models[i]))
        //             add("models[" + std::to_string(i) + "] invalid: " + s.models[i]);
        //         if (!uniq.insert(s.models[i]).second)
        //             add("duplicate joint name: " + s.models[i]);
        //     }
        // }
        if (!errs.empty())
        {
            std::ostringstream oss;
            oss << "Invalid configuration:\n";
            for (auto &e : errs)
                oss << "  - " << e << "\n";
            throw SettingsError(oss.str());
        }
    }

    void validate_settings(const HandSettings &s, const std::string &path)
    {
        std::vector<std::string> errs;
        auto add = [&](std::string m)
        { errs.push_back(path + " — " + m); };

        if (s.native_policy.min_current_mA > s.native_policy.max_current_mA)
            add("native_policy.current_limits_mA: min>max");
        if (s.native_policy.counts_per_rev <= 0 || s.native_policy.pitch_mm_per_rev <= 0)
            add("native_policy.screw_drive invalid");
        if (s.native_policy.tendon_fix_mm <= 0 || s.native_policy.tendon_port_mm <= 0)
            add("native_policy.geometry invalid");
        // require a table for each joint key you’ll use (use "_mcp" defaults)
        static const std::unordered_map<std::string, std::string> dev2key = {
            {"THUMB", "thumb_mcp"}, {"INDEX_FINGER", "index_finger_mcp"}, {"MIDDLE_FINGER", "middle_finger_mcp"}, {"RING_FINGER", "ring_finger_mcp"}, {"PINKY_FINGER", "pinky_mcp"}};
        // for (auto &n : s.models)
        // {
        //     auto it = dev2key.find(n);
        //     if (it != dev2key.end() && !s.native_policy.joint_tables.count(it->second))
        //         add("native_policy.joint_tables missing entry for " + it->second);
        // }

        if (!errs.empty())
        {
            std::ostringstream oss;
            oss << "Invalid configuration:\n";
            for (auto &e : errs)
                oss << "  - " << e << "\n";
            throw SettingsError(oss.str());
        }
    }

} // namespace grip
