#include "grip/control_policy_native.hpp"
#include <algorithm>

namespace grip {

NativePolicy::NativePolicy(const NativePolicySettings& cfg) {
    pid_.kp = cfg.pid.kp; pid_.ki = cfg.pid.ki; pid_.kd = cfg.pid.kd;
    limit_min_mA_ = cfg.min_current_mA; limit_max_mA_ = cfg.max_current_mA;
    init_theta_rad_ = cfg.initial_theta_rad;
    fix_mm_ = cfg.tendon_fix_mm; port_mm_ = cfg.tendon_port_mm;
    coupling_ = cfg.coupling_ratio; pitch_mm_per_rev_ = cfg.pitch_mm_per_rev;
    counts_per_rev_ = cfg.counts_per_rev;

    for (auto& kv : cfg.joint_tables) {
        JointRuntime jr;
        jr.linear_zero_counts = kv.second.linear_zero_counts;
        jr.ff_mA = kv.second.feedforward_mA;
        table_.emplace(kv.first, std::move(jr));
    }
}

double NativePolicy::tendon_length(double th, double a, double b) {
    return std::sqrt(a*a + b*b + 2*a*b*std::cos(th));
}
int32_t NativePolicy::clamp_i32(int32_t v, int32_t lo, int32_t hi) {
    return std::max(lo, std::min(hi, v));
}
double NativePolicy::lerp_table(double x, const std::vector<double>& xp, const std::vector<double>& fp) {
    if (x <= xp.front()) return fp.front();
    if (x >= xp.back())  return fp.back();
    auto it = std::upper_bound(xp.begin(), xp.end(), x);
    size_t i1 = size_t(it - xp.begin()), i0 = i1 - 1;
    double t = (x - xp[i0]) / (xp[i1] - xp[i0]);
    return fp[i0] + t * (fp[i1] - fp[i0]);
}

int32_t NativePolicy::compute(const std::string& key, double target_rad, int32_t current_counts) {
    auto it = table_.find(key);
    if (it == table_.end()) return 0;
    auto& jr = it->second;

    // 5-sample moving average on target
    jr.recent_targets.push_front(target_rad);
    if (jr.recent_targets.size() > 5) jr.recent_targets.pop_back();
    double filtered = 0.0; for (double v : jr.recent_targets) filtered += v;
    filtered /= jr.recent_targets.size();

    // piecewise angle logic: [0,70) and [70,140]
    int deg = int(filtered * 180.0 / M_PI);
    int segment = 0; double seg_angle = filtered;
    if (deg >= 0 && deg < 70) segment = 1;
    else if (deg >= 70 && deg <= 140) { segment = 2; seg_angle -= 70.0 * (M_PI/180.0); }
    else return 0;

    // forward kinematics-like mapping to tendon motion
    double abs_theta = seg_angle + init_theta_rad_;
    double dT = tendon_length(init_theta_rad_, fix_mm_, port_mm_) - tendon_length(abs_theta, fix_mm_, port_mm_);
    dT *= coupling_;
    if (segment == 2) dT += 7.118368851943895;   // stage offset from your python

    // convert mm -> counts
    int32_t tgt_counts = int32_t((dT / pitch_mm_per_rev_) * counts_per_rev_);
    tgt_counts += jr.linear_zero_counts;

    // PID in counts domain
    int32_t err = tgt_counts - current_counts;
    jr.integral_err += err;
    int32_t deriv = err - jr.prev_err;
    jr.prev_err = err;

    const int64_t Imax = 100000000, Imin = -100000000;
    if (jr.integral_err > Imax) jr.integral_err = Imax;
    if (jr.integral_err < Imin) jr.integral_err = Imin;

    double torque_units = pid_.kp * double(err) + pid_.kd * double(deriv) + pid_.ki * double(jr.integral_err);
    double current_mA = torque_units / 10.0;

    // Optional FF (off by default to match your script’s behavior)
    if (!jr.ff_mA.empty() && abs_theta >= 0.74918) {
        double ff = lerp_table((double)deg, ff_x_deg_, jr.ff_mA);
        // current_mA += ff; // enable if desired
    }

    return clamp_i32((int32_t)std::llround(current_mA), limit_min_mA_, limit_max_mA_);
}

} // namespace grip
