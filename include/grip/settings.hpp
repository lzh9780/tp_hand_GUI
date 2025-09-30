#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <stdexcept>

namespace grip {

struct Timing { int tseg1{}, tseg2{}, sjw{}, brp{}; };
struct TimingPair { Timing arbitration{}, data_phase{}; };

struct CanDeviceSettings {
    uint32_t device_type{33}, device_index{0}, channel_index{0}, core_clock_hz{60000000};
    bool termination_enabled{true};
    TimingPair bit_timing{};
};

struct CalibrationSettings {
    int32_t current_limit_mA{1500}, backdrive_speed{-200}, encoder_zero_offset{-10000};
};

struct TargetsConfig { double others_rad{1.5707963}, thumb_rad{2.4434610}; };

struct PidGains { double kp{0.005}, ki{0.00005}, kd{0.0001}; };

struct NativeJointConfig {
    int32_t linear_zero_counts{500};
    std::vector<double> feedforward_mA; // size 15 (0..140 step 10°)
};

struct NativePolicySettings {
    PidGains pid{};
    int32_t min_current_mA{-1000}, max_current_mA{1000};
    double initial_theta_rad{0.74918};
    double tendon_fix_mm{9.410}, tendon_port_mm{9.413};
    int    coupling_ratio{1};
    double pitch_mm_per_rev{1.0};
    int    counts_per_rev{262144};
    std::unordered_map<std::string, NativeJointConfig> joint_tables;
};

struct AppSettings {
    CanDeviceSettings can_device{};
    std::vector<std::string> joints;
    TargetsConfig targets{};
    CalibrationSettings calibration{};
    NativePolicySettings native_policy{};
};

struct SettingsError : public std::runtime_error { using std::runtime_error::runtime_error; };

AppSettings load_settings(const std::string& yaml_path);
void validate_settings(const AppSettings& s, const std::string& yaml_path);

} // namespace grip
