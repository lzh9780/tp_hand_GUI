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

struct DofInfo {
    std::string name;
    std::pair<float, float> route;
    std::vector<float> dof_joint_map;
};

struct ModuleStruct {
    std::vector<DofInfo> dofs;
    std::vector<std::string> joints;
    std::vector<int> motors;
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
    std::vector<std::string> control_policies;
    std::vector<std::string> wrist_control;
    std::string urdf_file;
};

struct ActuatorSettings {
    int max_vel{0};
    int max_current{0};
    std::pair<float, float> route{0, 0};
    float psi_f{0.0};
    int n_pole_pairs{0};
    int device_id{0};
};

struct SettingsError : public std::runtime_error { using std::runtime_error::runtime_error; };

AppSettings load_app_settings(const std::string& yaml_path);
DeviceSettings load_device_settings(const std::string& yaml_path);
HandSettings load_hand_settings(const std::string& yaml_path);
ActuatorSettings load_actuator_settings(const std::string& yaml_path);
void validate_settings(const AppSettings& s, const std::string& yaml_path);
void validate_settings(const HandSettings& s, const std::string& path);

} // namespace grip
