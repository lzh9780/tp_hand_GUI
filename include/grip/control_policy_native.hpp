#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <deque>
#include <cstdint>
#include <cmath>
#include "settings.hpp"

namespace grip {

struct PidState { double kp{0.005}, ki{0.00005}, kd{0.0001}; };

struct JointRuntime {
    int32_t linear_zero_counts{500};
    std::vector<double> ff_mA;
    std::deque<double> recent_targets; // <=5
    int64_t integral_err{0};
    int32_t prev_err{0};
};

class NativePolicy {
public:
    explicit NativePolicy(const NativePolicySettings& cfg);

    // returns current setpoint in mA for given joint key
    int32_t command_mA(std::string_view joint_key, double target_rad, int32_t encoder_counts);

private:
    static double tendon_length(double theta, double a, double b);
    static int32_t clamp_i32(int32_t v, int32_t lo, int32_t hi);
    static double lerp_table(double x, const std::vector<double>& xp, const std::vector<double>& fp);

private:
    PidState pid_;
    int32_t limit_min_mA_{-1000}, limit_max_mA_{1000};
    double  init_theta_rad_{0.74918};
    double  fix_mm_{9.410}, port_mm_{9.413};
    int     coupling_{1};
    double  pitch_mm_per_rev_{1.0};
    int     counts_per_rev_{262144};

    std::vector<double> ff_x_deg_{0,10,20,30,40,50,60,70,80,90,100,110,120,130,140};
    std::unordered_map<std::string, JointRuntime> table_;
};

} // namespace grip
