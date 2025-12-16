#include "grip/HandControl.hpp"

#include <algorithm>
#include <cmath>
#include <thread>
#include <chrono>

namespace grip
{

    static void push_pop(std::vector<double> &v, double x, size_t maxn)
    {
        v.push_back(x);
        if (v.size() > maxn)
            v.erase(v.begin());
    }
    static double avg_last_n(const std::vector<double> &v, size_t n)
    {
        if (v.empty())
            return 0.0;
        n = std::max<size_t>(1, std::min(n, v.size()));
        double s = 0.0;
        for (size_t i = v.size() - n; i < v.size(); ++i)
            s += v[i];
        return s / static_cast<double>(n);
    }

    HandControl::HandControl(const ModuleState &module_state)
        : layout_(build_module_layout(module_state))
    {
        const size_t Nd = layout_.dof_names.size();
        cascaded_state_.resize(Nd);
        hall_lpf_.resize(Nd);
        hall_pid_.resize(Nd);

        // Detect wrist DOFs in THIS module
        auto itP = layout_.dof_index.find("wrist_pitch");
        auto itY = layout_.dof_index.find("wrist_yaw");
        auto itR = layout_.dof_index.find("wrist_roll");

        has_pitch_ = (itP != layout_.dof_index.end());
        has_yaw_ = (itY != layout_.dof_index.end());
        has_roll_ = (itR != layout_.dof_index.end());

        if (has_pitch_)
            pitch_di_ = itP->second;
        if (has_yaw_)
            yaw_di_ = itY->second;
        if (has_roll_)
            roll_di_ = itR->second;

        has_wrist_ = has_pitch_ || has_yaw_ || has_roll_;

        // Resolve wrist motors (prefer pitch binding two motors; else yaw binding)
        auto get_two_motors = [&](uint32_t di, uint32_t &miL, uint32_t &miR) -> bool
        {
            if (di >= layout_.bindings.size())
                return false;
            const auto &b = layout_.bindings[di];
            if (b.motor_is.size() < 2)
                return false;
            miL = b.motor_is[0];
            miR = b.motor_is[1];
            return true;
        };

        if (has_pitch_ && get_two_motors(pitch_di_, left_mi_, right_mi_))
        {
            has_wrist_motors_ = true;
        }
        else if (has_yaw_ && get_two_motors(yaw_di_, left_mi_, right_mi_))
        {
            has_wrist_motors_ = true;
        }

        wrist_lpf_.pitch_hist.reserve(80);
        wrist_lpf_.roll_hist.reserve(100);
        wrist_lpf_.yaw_hist.reserve(42);
    }

    void HandControl::setMotorZeroLinearityPos(int motor_id, int32_t zero_counts)
    {
        motor_zero_linearity_pos_counts_[motor_id] = zero_counts;
    }

    // ---------------- Original algorithm helpers (unchanged) ----------------

    double HandControl::selectCurrentTheta(double mcp, double pip, double dip)
    {
        if (mcp < deg2rad(75.0) && pip <= deg2rad(0.5))
            return mcp;
        else if (mcp > deg2rad(65.0) && pip >= deg2rad(0.6) && dip <= deg2rad(0.5))
            return pip;
        else if (mcp > deg2rad(65.0) && pip > deg2rad(65.0) && dip >= deg2rad(0.5))
            return dip;
        else
            return (mcp + pip + dip) / 3.0;
    }

    double HandControl::tendonLengthMm(double theta_rad) const
    {
        const double a = joint_to_fix_dist_mm_;
        const double b = joint_to_port_dist_mm_;
        return std::sqrt(a * a + b * b + 2.0 * a * b * std::cos(theta_rad));
    }

    void HandControl::cascadedFingerStep_(
        CascadedState &st,
        double target_theta,
        double theta_mcp, double theta_pip, double theta_dip,
        double theta_mcp_speed, double theta_pip_speed, double theta_dip_speed,
        double &out_cur_cmd_mA)
    {
        const double theta_sum = theta_mcp + theta_pip + theta_dip;
        const double speed_sum = theta_mcp_speed + theta_pip_speed + theta_dip_speed;

        const double current_theta = selectCurrentTheta(theta_mcp, theta_pip, theta_dip);
        const double theta = initialized_theta_ + current_theta;

        const double a = joint_to_fix_dist_mm_;
        const double b = joint_to_port_dist_mm_;
        const double denom = std::sqrt(a * a + b * b + 2.0 * a * b * std::cos(theta));
        const double tendon_linear_speed = speed_sum * ((a * b * std::sin(theta)) / denom);
        const double motor_speed_rps = tendon_linear_speed / linear_screw_pitch_1_mm_per_rev_;
        const double motor_speed_rpm = motor_speed_rps * 60.0;

        st.control_loop_counter += 1;
        if (st.control_loop_counter >= 10)
        {
            st.control_loop_counter = 0;
            st.do_outer_flag = true;
        }

        if (st.do_outer_flag)
        {
            st.do_outer_flag = false;

            const double pos_error = target_theta - theta_sum;
            const double pos_error_deg = rad2deg(pos_error);

            st.pos_error_int += pos_error;
            const double deriv = pos_error - st.prev_pos_error;
            st.prev_pos_error = pos_error;

            const double max_int = deg2rad(120.0);
            const double min_int = -deg2rad(120.0);
            st.pos_error_int = clamp(st.pos_error_int, min_int, max_int);

            double Kp, Ki, Kd;
            if (std::abs(pos_error_deg) > 70.0)
            {
                Kp = kp_pos_1_;
                Ki = ki_pos_1_;
                Kd = kd_pos_1_;
            }
            else if (std::abs(pos_error_deg) > 30.0)
            {
                Kp = kp_pos_2_;
                Ki = ki_pos_2_;
                Kd = kd_pos_2_;
            }
            else if (std::abs(pos_error_deg) > 10.0)
            {
                Kp = kp_pos_3_;
                Ki = ki_pos_3_;
                Kd = kd_pos_3_;
            }
            else
            {
                Kp = kp_pos_4_;
                Ki = ki_pos_4_;
                Kd = kd_pos_4_;
            }

            if ((std::abs(pos_error_deg) < 80.0) && (std::abs(deriv) > 0.3))
            {
                st.velocity_ref = Kp * pos_error + Kd * 1.0;
                st.pos_error_int = 0.0;
            }
            else if ((std::abs(pos_error_deg) < 60.0) && (std::abs(deriv) > 0.27))
            {
                st.velocity_ref = Kp * pos_error + Kd * 1.2;
                st.pos_error_int = 0.0;
            }
            else if ((std::abs(pos_error_deg) < 40.0) && (std::abs(deriv) > 0.22))
            {
                st.velocity_ref = Kp * pos_error + Kd * 1.6;
                st.pos_error_int = 0.0;
            }
            else if ((std::abs(pos_error_deg) < 20.0) && (std::abs(deriv) > 0.15))
            {
                st.velocity_ref = Kp * pos_error + Kd * 2.5;
                st.pos_error_int = 0.0;
            }
            else
            {
                st.velocity_ref = Kp * pos_error + Kd * deriv + Ki * st.pos_error_int;
            }

            st.velocity_ref = clamp(st.velocity_ref, min_velocity_rpm_, max_velocity_rpm_);
        }

        double vel_error = st.velocity_ref - motor_speed_rpm;
        st.vel_error_int += vel_error;
        const double dvel = vel_error - st.prev_vel_error;
        st.prev_vel_error = vel_error;

        st.vel_error_int = clamp(st.vel_error_int, min_current_mA_ * 0.1, max_current_mA_ * 0.1);

        if (static_cast<int>(rad2deg(target_theta)) == static_cast<int>(rad2deg(theta_sum)))
        {
            vel_error = 0.0;
            st.vel_error_int = 0.0;
        }

        const double torque_cmd = kp_vel_ * vel_error + kd_vel_ * dvel + ki_vel_ * st.vel_error_int;
        out_cur_cmd_mA = clamp(torque_cmd, min_current_mA_, max_current_mA_);
    }

    bool HandControl::computeStageFlagAndNormalize_(double &theta_target_rad, int &stage_flag) const
    {
        const int deg = static_cast<int>(rad2deg(theta_target_rad));
        if (deg >= 0 && deg < 70)
        {
            stage_flag = 1;
            return true;
        }
        if (deg >= 70 && deg <= 140)
        {
            theta_target_rad -= deg2rad(70.0);
            stage_flag = 2;
            return true;
        }
        return false;
    }

    void HandControl::hallFingerStep_(
        int motor_id,
        HallLPF &lpf,
        HallMotorPIDState &mp,
        double target_theta,
        int32_t current_counts,
        double &out_vel_cmd_rpm)
    {
        lpf.previous_target_theta.push_back(target_theta);
        if (lpf.previous_target_theta.size() > 5)
            lpf.previous_target_theta.erase(lpf.previous_target_theta.begin());
        double filtered = 0.0;
        for (double v : lpf.previous_target_theta)
            filtered += v;
        filtered /= static_cast<double>(lpf.previous_target_theta.size());

        double norm = filtered;
        int stage_flag = 0;
        if (!computeStageFlagAndNormalize_(norm, stage_flag))
        {
            out_vel_cmd_rpm = 0.0;
            return;
        }

        const double target_theta_total = norm + initialized_theta_;

        int32_t joint_zero_counts = 0;
        if (auto it = motor_zero_linearity_pos_counts_.find(motor_id); it != motor_zero_linearity_pos_counts_.end())
            joint_zero_counts = it->second;

        double delta_T = tendonLengthMm(initialized_theta_) - tendonLengthMm(target_theta_total);
        delta_T *= static_cast<double>(num_joints_on_tendon_);
        if (stage_flag == 2)
            delta_T += 7.118368851943895;

        int64_t target_counts = static_cast<int64_t>((delta_T / linear_screw_pitch_1_mm_per_rev_) *
                                                     static_cast<double>(encoder_counts_per_rev_1_));
        target_counts += joint_zero_counts;

        const double pos_error = static_cast<double>(target_counts - current_counts);
        mp.pos_error_int += pos_error;
        const double deriv = pos_error - mp.prev_pos_error;
        mp.prev_pos_error = pos_error;

        mp.pos_error_int = clamp(mp.pos_error_int, -1e8, 1e8);

        double pos_error_used = pos_error;
        double deriv_used = deriv;
        double int_used = mp.pos_error_int;

        if ((std::abs(pos_error_used) < 600000.0) && (std::abs(deriv_used) > 5000.0))
        {
            pos_error_used = 0.0;
            deriv_used = 0.0;
            int_used = 0.0;
            mp.pos_error_int = 0.0;
        }

        const double torque_cmd = kp_pos_counts_ * pos_error_used + kd_pos_counts_ * deriv_used + ki_pos_counts_ * int_used;
        out_vel_cmd_rpm = clamp(torque_cmd / 10.0, hall_min_velocity_rpm_, hall_max_velocity_rpm_);
    }

    std::optional<std::pair<double, double>> HandControl::BT_solution_(double phi, double theta, double L) const
    {
        if (theta < deg2rad(-21.0) || theta > deg2rad(21.0))
            return std::nullopt;
        if (phi < deg2rad(-30.0) || phi > deg2rad(50.0))
            return std::nullopt;

        const double st = std::sin(theta), ct = std::cos(theta);
        const double sp = std::sin(phi), cp = std::cos(phi);

        const double yphi = 22.0 * cp - 31.5 * sp;
        const double zphi = 22.0 * sp + 31.5 * cp;

        const double Bx = 18.5 * st - 14.5 * ct + st * zphi;
        const double By = yphi;
        const double Bz = 18.5 * ct + 14.5 * st + ct * zphi;

        const double Bpx = 18.5 * st + 14.5 * ct + st * zphi;
        const double Bpy = yphi;
        const double Bpz = 18.5 * ct - 14.5 * st + ct * zphi;

        const double dx = -21.0 - Bx;
        const double dy = 29.687 - By;
        const double disc = L * L - dx * dx - dy * dy;
        if (disc < 0.0)
            return std::nullopt;
        double Right = Bz - std::sqrt(disc);

        const double dxp = 21.0 - Bpx;
        const double dyp = 29.687 - Bpy;
        const double discp = L * L - dxp * dxp - dyp * dyp;
        if (discp < 0.0)
            return std::nullopt;
        double Left = Bpz - std::sqrt(discp);

        Right += 37.511;
        Left += 37.511;

        if (Right < 0.0 || Right > 35.0)
            return std::nullopt;
        if (Left < 0.0 || Left > 35.0)
            return std::nullopt;

        return std::make_pair(Left, Right);
    }

    bool HandControl::wristControlStep_(double pitch_Target_Theta,
                                        double roll_Target_Theta,
                                        double yaw_Target_Theta,
                                        int32_t left_enc_counts,
                                        int32_t right_enc_counts,
                                        double &out_left_vel,
                                        double &out_right_vel)
    {
        const int pitch_stack_size = 80;
        const int roll_stack_size = 100;
        const int yaw_stack_size = 42;

        push_pop(wrist_lpf_.pitch_hist, pitch_Target_Theta, pitch_stack_size);
        push_pop(wrist_lpf_.roll_hist, roll_Target_Theta, roll_stack_size);
        push_pop(wrist_lpf_.yaw_hist, yaw_Target_Theta, yaw_stack_size);

        // Preserve Python behavior: diff against hist.back() after append -> usually 0 -> used=1
        auto used_stack = [&](double target, const std::vector<double> &hist, int maxn) -> int
        {
            double diff_deg = std::abs((target - hist.back()) * 180.0 / kPi);
            int used = static_cast<int>(diff_deg);
            if (used > maxn)
                used = maxn;
            if (used < 1)
                used = 1;
            return used;
        };

        const int pitch_used = used_stack(pitch_Target_Theta, wrist_lpf_.pitch_hist, pitch_stack_size);
        const int roll_used = used_stack(roll_Target_Theta, wrist_lpf_.roll_hist, roll_stack_size);
        const int yaw_used = used_stack(yaw_Target_Theta, wrist_lpf_.yaw_hist, yaw_stack_size);

        const double Filtered_pitch = avg_last_n(wrist_lpf_.pitch_hist, pitch_used);
        const double Filtered_roll = avg_last_n(wrist_lpf_.roll_hist, roll_used);
        const double Filtered_yaw = avg_last_n(wrist_lpf_.yaw_hist, yaw_used);

        (void)Filtered_roll; // python roll block does not output motor commands

        auto sol = BT_solution_(Filtered_pitch, Filtered_yaw);
        if (!sol.has_value())
        {
            out_left_vel = 0.0;
            out_right_vel = 0.0;
            return false;
        }

        const double Left_mm = sol->first;
        const double Right_mm = sol->second;

        const int Left_counts =
            static_cast<int>((Left_mm / linear_screw_pitch_2_mm_per_rev_) * static_cast<double>(encoder_counts_per_rev_2_));
        const int Right_counts =
            static_cast<int>((Right_mm / linear_screw_pitch_2_mm_per_rev_) * static_cast<double>(encoder_counts_per_rev_2_));

        const double errL = static_cast<double>(Left_counts - left_enc_counts);
        const double errR = static_cast<double>(Right_counts - right_enc_counts);

        wrist_left_pid_.pos_error_int += errL;
        wrist_right_pid_.pos_error_int += errR;
        wrist_left_pid_.pos_error_int = clamp(wrist_left_pid_.pos_error_int, -100000000.0, 100000000.0);
        wrist_right_pid_.pos_error_int = clamp(wrist_right_pid_.pos_error_int, -100000000.0, 100000000.0);

        const double dL = errL - wrist_left_pid_.prev_pos_error;
        const double dR = errR - wrist_right_pid_.prev_pos_error;
        wrist_left_pid_.prev_pos_error = errL;
        wrist_right_pid_.prev_pos_error = errR;

        double vL = wrist_pitch_Kp_ * errL + wrist_pitch_Ki_ * wrist_left_pid_.pos_error_int + wrist_pitch_Kd_ * dL;
        double vR = wrist_pitch_Kp_ * errR + wrist_pitch_Ki_ * wrist_right_pid_.pos_error_int + wrist_pitch_Kd_ * dR;

        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // preserve python sleep(0.1)

        out_left_vel = vL;
        out_right_vel = vR;
        return true;
    }

    // ---------------- Module-level step ----------------

    void HandControl::step(const ModuleInputSnapshot &in,
                           ModuleOutputSnapshot &out,
                           int control_mode)
    {
        // Ensure output is correctly sized
        if (out.motor_vel_cmd.size() != layout_.motor_ids.size() ||
            out.motor_cur_cmd.size() != layout_.motor_ids.size() ||
            out.motor_pos_cmd.size() != layout_.motor_ids.size())
        {
            out = make_module_output(layout_);
        }

        std::fill(out.motor_pos_cmd.begin(), out.motor_pos_cmd.end(), 0.f);
        std::fill(out.motor_vel_cmd.begin(), out.motor_vel_cmd.end(), 0.f);
        std::fill(out.motor_cur_cmd.begin(), out.motor_cur_cmd.end(), 0.f);

        // Wrist (once) if this module includes wrist DOFs and 2 motors
        if (has_wrist_ && has_pitch_ && has_yaw_ && has_wrist_motors_)
        {
            const double pitch_t = (pitch_di_ < in.dof_target.size()) ? in.dof_target[pitch_di_] : 0.0;
            const double yaw_t = (yaw_di_ < in.dof_target.size()) ? in.dof_target[yaw_di_] : 0.0;
            const double roll_t = (has_roll_ && roll_di_ < in.dof_target.size()) ? in.dof_target[roll_di_] : 0.0;

            const int32_t left_enc = (left_mi_ < in.motor_counts.size()) ? in.motor_counts[left_mi_] : 0;
            const int32_t right_enc = (right_mi_ < in.motor_counts.size()) ? in.motor_counts[right_mi_] : 0;

            double vL = 0.0, vR = 0.0;
            wristControlStep_(pitch_t, roll_t, yaw_t, left_enc, right_enc, vL, vR);

            out.motor_vel_cmd[left_mi_] = static_cast<float>(vL);
            out.motor_vel_cmd[right_mi_] = static_cast<float>(vR);
        }

        // Finger-like DOFs: run per binding; skip wrist_* DOFs (handled above)
        for (const auto &b : layout_.bindings)
        {
            const uint32_t di = b.dof_i;
            const std::string &name = layout_.dof_names[di];

            if (name == "wrist_pitch" || name == "wrist_yaw" || name == "wrist_roll")
                continue;

            if (b.motor_is.empty())
                continue;
            const uint32_t mi0 = b.motor_is[0];
            const int motor_id = layout_.motor_ids[mi0];

            const double target = (di < in.dof_target.size()) ? static_cast<double>(in.dof_target[di]) : 0.0;

            // Map joints by order [0]=mcp, [1]=pip, [2]=dip (data fit only)
            double mcp = 0, pip = 0, dip = 0, dmcp = 0, dpip = 0, ddip = 0;
            if (b.joint_is.size() > 0)
            {
                mcp = in.joint_pos[b.joint_is[0]];
                dmcp = in.joint_vel[b.joint_is[0]];
            }
            if (b.joint_is.size() > 1)
            {
                pip = in.joint_pos[b.joint_is[1]];
                dpip = in.joint_vel[b.joint_is[1]];
            }
            if (b.joint_is.size() > 2)
            {
                dip = in.joint_pos[b.joint_is[2]];
                ddip = in.joint_vel[b.joint_is[2]];
            }

            if (control_mode == 1)
            {
                double cur = 0.0;
                cascadedFingerStep_(cascaded_state_[di], target, mcp, pip, dip, dmcp, dpip, ddip, cur);
                out.motor_cur_cmd[mi0] = static_cast<float>(cur);
            }
            else if (control_mode == 2)
            {
                const int32_t enc = (mi0 < in.motor_counts.size()) ? in.motor_counts[mi0] : 0;
                double vel = 0.0;
                hallFingerStep_(motor_id, hall_lpf_[di], hall_pid_[di], target, enc, vel);
                out.motor_vel_cmd[mi0] = static_cast<float>(vel);
            }
        }
    }

} // namespace grip
