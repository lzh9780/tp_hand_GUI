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

struct PidGains { double kp{0.005}, ki{0.00005}, kd{0.0001}; };

struct NativeJointConfig {
    int32_t linear_zero_counts{500};
    std::vector<double> feedforward_mA; // size 15 (0..140 step 10°)
};

struct MotorInfo {
    int id;
    std::vector<int> joint_map;
};

struct ModuleStruct {
    std::vector<std::string> joints;
    std::vector<MotorInfo> motors;
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
    std::vector<std::string> models;
};

struct DeviceSettings {
    int device_type{0};
    int hardware_version{0};
    int software_version{0};
};

struct HandSettings {
    std::unordered_map<std::string, ModuleStruct> structs;
    NativePolicySettings native_policy{};
};

struct ActuatorSettings {
    int max_vel{0};
    int max_current{0};
    std::pair<float, float> route{0, 0};
    float psi_f{0.0};
    int n_pole_pairs{0};
    int device_id{0};
    NativePolicySettings native_policy{};
};

struct SettingsError : public std::runtime_error { using std::runtime_error::runtime_error; };

AppSettings load_app_settings(const std::string& yaml_path);
DeviceSettings load_device_settings(const std::string& yaml_path);
HandSettings load_hand_settings(const std::string& yaml_path);
ActuatorSettings load_actuator_settings(const std::string& yaml_path);
void validate_settings(const AppSettings& s, const std::string& yaml_path);
void validate_settings(const HandSettings& s, const std::string& path);

} // namespace grip
