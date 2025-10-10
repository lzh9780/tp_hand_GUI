#include "grip/settings.hpp"
#include <yaml-cpp/yaml.h>
#include <set>
#include <sstream>
#include <cmath>

namespace grip
{

    static bool valid_device(const std::string &s)
    {
        static const std::set<std::string> k{"THUMB", "INDEX_FINGER", "MIDDLE_FINGER", "RING_FINGER", "PINKY_FINGER"};
        return k.count(s) > 0;
    }
    static auto rt(const YAML::Node &n) { return Timing{n["tseg1"].as<int>(), n["tseg2"].as<int>(), n["sjw"].as<int>(), n["brp"].as<int>()}; }

    AppSettings load_settings(const std::string &yaml_path)
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
        if (auto n = y["joints"])
            for (const auto &it : n)
                s.joints.push_back(it.as<std::string>());
        if (auto n = y["calibration"])
        {
            s.calibration.current_limit_mA = n["current_limit_mA"].as<int32_t>(s.calibration.current_limit_mA);
            s.calibration.backdrive_speed = n["backdrive_speed"].as<int32_t>(s.calibration.backdrive_speed);
            s.calibration.encoder_zero_offset = n["encoder_zero_offset"].as<int32_t>(s.calibration.encoder_zero_offset);
        }
        if (auto n = y["native_policy"])
        {
            if (auto p = n["pid"])
            {
                s.native_policy.pid.kp = p["kp"].as<double>(s.native_policy.pid.kp);
                s.native_policy.pid.ki = p["ki"].as<double>(s.native_policy.pid.ki);
                s.native_policy.pid.kd = p["kd"].as<double>(s.native_policy.pid.kd);
            }
            if (auto lim = n["current_limits_mA"])
            {
                s.native_policy.min_current_mA = lim["min"].as<int32_t>(s.native_policy.min_current_mA);
                s.native_policy.max_current_mA = lim["max"].as<int32_t>(s.native_policy.max_current_mA);
            }
            s.native_policy.initial_theta_rad = n["initial_theta_rad"].as<double>(s.native_policy.initial_theta_rad);
            if (auto g = n["geometry"])
            {
                s.native_policy.tendon_fix_mm = g["tendon_fix_mm"].as<double>(s.native_policy.tendon_fix_mm);
                s.native_policy.tendon_port_mm = g["tendon_port_mm"].as<double>(s.native_policy.tendon_port_mm);
                s.native_policy.coupling_ratio = g["coupling_ratio"].as<int>(s.native_policy.coupling_ratio);
            }
            if (auto sd = n["screw_drive"])
            {
                s.native_policy.pitch_mm_per_rev = sd["pitch_mm_per_rev"].as<double>(s.native_policy.pitch_mm_per_rev);
                s.native_policy.counts_per_rev = sd["counts_per_rev"].as<int>(s.native_policy.counts_per_rev);
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
                    s.native_policy.joint_tables[it->first.as<std::string>()] = std::move(jc);
                }
            }
        }
        validate_settings(s, yaml_path);
        return s;
    }

    void validate_settings(const AppSettings &s, const std::string &path)
    {
        std::vector<std::string> errs;
        auto add = [&](std::string m)
        { errs.push_back(path + " — " + m); };
        if (s.joints.empty())
            add("joints list cannot be empty");
        else
        {
            std::set<std::string> uniq;
            for (size_t i = 0; i < s.joints.size(); ++i)
            {
                if (!valid_device(s.joints[i]))
                    add("joints[" + std::to_string(i) + "] invalid: " + s.joints[i]);
                if (!uniq.insert(s.joints[i]).second)
                    add("duplicate joint name: " + s.joints[i]);
            }
        }
        if (s.native_policy.min_current_mA > s.native_policy.max_current_mA)
            add("native_policy.current_limits_mA: min>max");
        if (s.native_policy.counts_per_rev <= 0 || s.native_policy.pitch_mm_per_rev <= 0)
            add("native_policy.screw_drive invalid");
        if (s.native_policy.tendon_fix_mm <= 0 || s.native_policy.tendon_port_mm <= 0)
            add("native_policy.geometry invalid");
        // require a table for each joint key you’ll use (use "_mcp" defaults)
        static const std::unordered_map<std::string, std::string> dev2key = {
            {"THUMB", "thumb_mcp"}, {"INDEX_FINGER", "index_finger_mcp"}, {"MIDDLE_FINGER", "middle_finger_mcp"}, {"RING_FINGER", "ring_finger_mcp"}, {"PINKY_FINGER", "pinky_mcp"}};
        for (auto &n : s.joints)
        {
            auto it = dev2key.find(n);
            if (it != dev2key.end() && !s.native_policy.joint_tables.count(it->second))
                add("native_policy.joint_tables missing entry for " + it->second);
        }
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
