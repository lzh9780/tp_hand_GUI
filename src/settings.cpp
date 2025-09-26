#include "grip/settings.hpp"
#include <yaml-cpp/yaml.h>
#include <set>
#include <sstream>
#include <cmath>

namespace grip {

static Timing read_timing(const YAML::Node& n){
    return Timing{
        n["tseg1"].as<int>(),
        n["tseg2"].as<int>(),
        n["sjw"].as<int>(),
        n["brp"].as<int>()
    };
}

AppSettings load_settings(const std::string& yaml_path) {
    YAML::Node y = YAML::LoadFile(yaml_path);
    AppSettings s;

    if (y["runtime_policy"]) s.runtime_policy = y["runtime_policy"].as<std::string>();

    if (auto n = y["can_device"]) {
        s.can_device.device_type = n["device_type"].as<uint32_t>(s.can_device.device_type);
        s.can_device.device_index = n["device_index"].as<uint32_t>(s.can_device.device_index);
        s.can_device.channel_index = n["channel_index"].as<uint32_t>(s.can_device.channel_index);
        s.can_device.core_clock_hz = n["core_clock_hz"].as<uint32_t>(s.can_device.core_clock_hz);
        s.can_device.termination_enabled = n["termination_enabled"].as<bool>(s.can_device.termination_enabled);
        if (auto t = n["bit_timing"]) {
            s.can_device.bit_timing.arbitration = read_timing(t["arbitration"]);
            s.can_device.bit_timing.data_phase  = read_timing(t["data_phase"]);
        }
    }

    if (auto n = y["joints"]) {
        for (auto& it : n) {
            JointBinding jb;
            jb.device = it["device"].as<std::string>();
            jb.key    = it["key"].as<std::string>();
            s.joints.push_back(std::move(jb));
        }
    }

    if (auto n = y["hold_targets"]) {
        s.hold_targets.others_rad = n["others_rad"].as<double>(s.hold_targets.others_rad);
        s.hold_targets.thumb_rad  = n["thumb_rad"].as<double>(s.hold_targets.thumb_rad);
    }

    if (auto n = y["calibration"]) {
        s.calibration.current_limit_mA   = n["current_limit_mA"].as<int32_t>(s.calibration.current_limit_mA);
        s.calibration.backdrive_speed    = n["backdrive_speed"].as<int32_t>(s.calibration.backdrive_speed);
        s.calibration.encoder_zero_offset= n["encoder_zero_offset"].as<int32_t>(s.calibration.encoder_zero_offset);
    }

    if (auto n = y["native_policy"]) {
        if (auto p = n["pid"]) {
            s.native_policy.pid.kp = p["kp"].as<double>(s.native_policy.pid.kp);
            s.native_policy.pid.ki = p["ki"].as<double>(s.native_policy.pid.ki);
            s.native_policy.pid.kd = p["kd"].as<double>(s.native_policy.pid.kd);
        }
        if (auto lim = n["current_limits_mA"]) {
            s.native_policy.min_current_mA = lim["min"].as<int32_t>(s.native_policy.min_current_mA);
            s.native_policy.max_current_mA = lim["max"].as<int32_t>(s.native_policy.max_current_mA);
        }
        s.native_policy.initial_theta_rad = n["initial_theta_rad"].as<double>(s.native_policy.initial_theta_rad);
        if (auto g = n["geometry"]) {
            s.native_policy.tendon_fix_mm  = g["tendon_fix_mm"].as<double>(s.native_policy.tendon_fix_mm);
            s.native_policy.tendon_port_mm = g["tendon_port_mm"].as<double>(s.native_policy.tendon_port_mm);
            s.native_policy.coupling_ratio = g["coupling_ratio"].as<int>(s.native_policy.coupling_ratio);
        }
        if (auto sdrv = n["screw_drive"]) {
            s.native_policy.pitch_mm_per_rev = sdrv["pitch_mm_per_rev"].as<double>(s.native_policy.pitch_mm_per_rev);
            s.native_policy.counts_per_rev   = sdrv["counts_per_rev"].as<int>(s.native_policy.counts_per_rev);
        }
        if (auto jt = n["joint_tables"]) {
            for (auto it = jt.begin(); it != jt.end(); ++it) {
                NativeJointConfig jc;
                jc.linear_zero_counts = it->second["linear_zero_counts"].as<int32_t>(jc.linear_zero_counts);
                if (auto ff = it->second["feedforward_mA"]) {
                    for (auto v : ff) jc.feedforward_mA.push_back(v.as<double>());
                }
                s.native_policy.joint_tables[it->first.as<std::string>()] = std::move(jc);
            }
        }
    }

    if (auto n = y["python_policy"]) {
        s.python_policy.script_dir = n["script_dir"].as<std::string>(s.python_policy.script_dir);
        s.python_policy.module     = n["module"].as<std::string>(s.python_policy.module);
        s.python_policy.class_name = n["class"].as<std::string>(s.python_policy.class_name);
        s.python_policy.use_headless_matplotlib = n["use_headless_matplotlib"].as<bool>(s.python_policy.use_headless_matplotlib);
    }

    validate_settings(s, yaml_path);
    return s;
}

static bool finite(double v){ return std::isfinite(v); }
static bool valid_device(const std::string& s) {
    static const std::set<std::string> k{"THUMB","INDEX_FINGER","MIDDLE_FINGER","RING_FINGER","PINKY_FINGER"};
    return k.count(s) > 0;
}

void validate_settings(const AppSettings& s, const std::string& path) {
    std::vector<std::string> errs;

    auto add = [&](const std::string& m){ errs.push_back(path + " — " + m); };

    if (s.runtime_policy != "native" && s.runtime_policy != "python")
        add("runtime_policy must be 'native' or 'python'");

    if (s.joints.empty())
        add("joints list cannot be empty");
    else {
        std::set<std::string> dn, jk;
        for (size_t i = 0; i < s.joints.size(); ++i) {
            const auto& e = s.joints[i];
            if (!valid_device(e.device)) add("joints[" + std::to_string(i) + "].device invalid: " + e.device);
            if (e.key.empty()) add("joints[" + std::to_string(i) + "].key empty");
            if (!dn.insert(e.device).second) add("duplicate device name: " + e.device);
            if (!jk.insert(e.key).second) add("duplicate joint key: " + e.key);
        }
    }

    auto check_t = [&](const char* w, const Timing& t){
        if (t.tseg1<=0||t.tseg2<=0||t.sjw<=0||t.brp<=0)
            add(std::string("can_device.bit_timing.")+w+": fields must be positive");
        if (t.tseg1>31 || t.tseg2>15 || t.sjw>7 || t.brp>1024)
            add(std::string("can_device.bit_timing.")+w+": out of typical range");
    };
    check_t("arbitration", s.can_device.bit_timing.arbitration);
    check_t("data_phase",  s.can_device.bit_timing.data_phase);
    if (s.can_device.core_clock_hz < 1'000'000 || s.can_device.core_clock_hz > 200'000'000)
        add("can_device.core_clock_hz out of range");

    if (!finite(s.hold_targets.others_rad) || !finite(s.hold_targets.thumb_rad))
        add("hold_targets must be finite");
    if (std::abs(s.calibration.backdrive_speed) > 5000)
        add("calibration.backdrive_speed magnitude too large");
    if (s.calibration.current_limit_mA <= 0 || s.calibration.current_limit_mA > 10000)
        add("calibration.current_limit_mA out of (0,10000]");

    if (s.runtime_policy == "native") {
        if (s.native_policy.min_current_mA > s.native_policy.max_current_mA)
            add("native_policy.current_limits_mA: min > max");
        if (s.native_policy.counts_per_rev <= 0 || s.native_policy.pitch_mm_per_rev <= 0)
            add("native_policy.screw_drive counts/pitch must be > 0");
        if (s.native_policy.tendon_fix_mm <= 0 || s.native_policy.tendon_port_mm <= 0)
            add("native_policy.geometry distances must be > 0");

        for (const auto& jb : s.joints) {
            if (!s.native_policy.joint_tables.count(jb.key))
                add("native_policy.joint_tables missing entry for joint key: " + jb.key);
        }
        for (const auto& kv : s.native_policy.joint_tables) {
            const auto& ff = kv.second.feedforward_mA;
            if (!ff.empty() && ff.size() != 15)
                add("native_policy.joint_tables."+kv.first+".feedforward_mA must have 15 values (0..140° step 10)");
            for (double v : ff) if (!finite(v)) { add("native_policy.joint_tables."+kv.first+".feedforward_mA contains non-finite"); break; }
        }
    }

    if (s.runtime_policy == "python") {
        if (s.python_policy.module.empty()) add("python_policy.module cannot be empty");
        if (s.python_policy.class_name.empty()) add("python_policy.class cannot be empty");
    }

    if (!errs.empty()) {
        std::ostringstream oss; oss << "Invalid configuration:\n";
        for (auto& e : errs) oss << "  - " << e << "\n";
        throw SettingsError(oss.str());
    }
}

} // namespace grip
