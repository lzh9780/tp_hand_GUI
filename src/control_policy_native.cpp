#include "grip/control_policy_native.hpp"
#include <algorithm>
#include <iostream>

namespace grip
{

    NativePolicy::NativePolicy(const NativePolicySettings &cfg)
    {
        pid_.kp = cfg.pid.kp;
        pid_.ki = cfg.pid.ki;
        pid_.kd = cfg.pid.kd;
        limit_min_mA_ = cfg.min_current_mA;
        limit_max_mA_ = cfg.max_current_mA;
        init_theta_rad_ = cfg.initial_theta_rad;
        fix_mm_ = cfg.tendon_fix_mm;
        port_mm_ = cfg.tendon_port_mm;
        coupling_ = cfg.coupling_ratio;
        pitch_mm_per_rev_ = cfg.pitch_mm_per_rev;
        counts_per_rev_ = cfg.counts_per_rev;
        for (auto &kv : cfg.joint_tables)
        {
            JointRuntime jr;
            jr.linear_zero_counts = kv.second.linear_zero_counts;
            jr.ff_mA = kv.second.feedforward_mA;
            table_.emplace(kv.first, std::move(jr));
        }
    }

    double NativePolicy::tendon_length(double th, double a, double b) { return std::sqrt(a * a + b * b + 2 * a * b * std::cos(th)); }
    int32_t NativePolicy::clamp_i32(int32_t v, int32_t lo, int32_t hi) { return std::max(lo, std::min(hi, v)); }
    double NativePolicy::lerp_table(double x, const std::vector<double> &xp, const std::vector<double> &fp)
    {
        if (x <= xp.front())
            return fp.front();
        if (x >= xp.back())
            return fp.back();
        auto it = std::upper_bound(xp.begin(), xp.end(), x);
        size_t i1 = size_t(it - xp.begin()), i0 = i1 - 1;
        double t = (x - xp[i0]) / (xp[i1] - xp[i0]);
        return fp[i0] + t * (fp[i1] - fp[i0]);
    }

    int32_t NativePolicy::command_mA(std::string_view key, double target_rad, int32_t enc)
    {
        auto it = table_.find(std::string(key));
        if (it == table_.end())
            return 0;
        auto &jr = it->second;

        // 5-sample MA on target
        jr.recent_targets.push_front(target_rad);
        if (jr.recent_targets.size() > 80)
            jr.recent_targets.pop_back();
        double filt = 0.0;
        for (double v : jr.recent_targets)
            filt += v;
        filt /= jr.recent_targets.size();

        // piecewise stages: [0,70) and [70,140]
        int deg = int(filt * 180.0 / M_PI);
        int seg = 0;
        double seg_ang = filt;
        if (deg >= 0 && deg < 70)
            seg = 1;
        else if (deg >= 70 && deg <= 140)
        {
            seg = 2;
            seg_ang -= 70.0 * (M_PI / 180.0);
        }
        else
            return 0;

        double abs_th = seg_ang + init_theta_rad_;
        double dT = tendon_length(init_theta_rad_, fix_mm_, port_mm_) - tendon_length(abs_th, fix_mm_, port_mm_);
        dT *= coupling_;
        if (seg == 2)
            dT += 7.118368851943895; // stage offset from original logic

        int32_t tgt_counts = int32_t((dT / pitch_mm_per_rev_) * counts_per_rev_) + jr.linear_zero_counts;

        int32_t e = tgt_counts - enc;
        jr.integral_err += e;
        int32_t d = e - jr.prev_err;
        jr.prev_err = e;

        const int64_t Imax = 100000000, Imin = -100000000;
        if (jr.integral_err > Imax)
            jr.integral_err = Imax;
        if (jr.integral_err < Imin)
            jr.integral_err = Imin;

        // Stopping the motor if the target position is reached
        if (abs(e) < 600000 && abs(d) > 5000) { //
            e = 0;
            jr.integral_err = 0;
            d = 0;
        }

        double torque = pid_.kp * double(e) + pid_.kd * double(d) + pid_.ki * double(jr.integral_err);
        double current_mA = torque / 10.0;

        // std::cout << "algorithm speed: " << d << "\n";

        // Optional FF (off by default to match prior behavior)
        if (!jr.ff_mA.empty() && abs_th >= 0.74918)
        {
            double ff = lerp_table(double(deg), ff_x_deg_, jr.ff_mA);
            (void)ff; /* current_mA += ff; */
        }

        return clamp_i32(int32_t(std::llround(current_mA)), limit_min_mA_, limit_max_mA_);
    }

} // namespace grip
