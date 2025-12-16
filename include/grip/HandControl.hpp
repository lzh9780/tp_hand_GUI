#pragma once
#include "ModuleRt.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace grip
{

    class HandControl
    {
    public:
        // Create after config is loaded. Layout is module-local.
        explicit HandControl(const ModuleState &module_state);

        const ModuleLayout &layout() const { return layout_; }

        // Hall controller needs per-motor zero (counts). If you don't have it, leave unset -> 0.
        void setMotorZeroLinearityPos(int motor_id, int32_t zero_counts);

        // Run one step for this module
        // control_mode comes from ModuleState::control_mode (module-level mode)
        void step(const ModuleInputSnapshot &in,
                  ModuleOutputSnapshot &out,
                  int control_mode);

    private:
        static constexpr double kPi = 3.14159265358979323846;
        static inline double deg2rad(double deg) { return deg * kPi / 180.0; }
        static inline double rad2deg(double rad) { return rad * 180.0 / kPi; }

        template <typename T>
        static inline T clamp(T v, T lo, T hi) { return (v < lo) ? lo : (v > hi) ? hi
                                                                                 : v; }

        // ---------------- Original algorithm states ----------------
        struct CascadedState
        {
            double velocity_ref = 0.0;
            bool do_outer_flag = false;
            double prev_vel_error = 0.0;
            double vel_error_int = 0.0;
            double prev_pos_error = 0.0;
            double pos_error_int = 0.0;
            int control_loop_counter = 0;
        };

        struct HallLPF
        {
            std::vector<double> previous_target_theta;
        };
        struct HallMotorPIDState
        {
            double prev_pos_error = 0.0;
            double pos_error_int = 0.0;
        };

        // Wrist persistent states (direct translation behavior)
        struct WristLPF
        {
            std::vector<double> pitch_hist;
            std::vector<double> roll_hist;
            std::vector<double> yaw_hist;
        };
        struct WristMotorPID
        {
            double prev_pos_error = 0.0;
            double pos_error_int = 0.0;
        };

    private:
        // ---------------- Original algorithm helpers ----------------
        static double selectCurrentTheta(double mcp, double pip, double dip);
        double tendonLengthMm(double theta_rad) const;

        void cascadedFingerStep_(CascadedState &st,
                                 double target_theta,
                                 double theta_mcp, double theta_pip, double theta_dip,
                                 double theta_mcp_speed, double theta_pip_speed, double theta_dip_speed,
                                 double &out_cur_cmd_mA);

        bool computeStageFlagAndNormalize_(double &theta_target_rad, int &stage_flag) const;

        void hallFingerStep_(int motor_id,
                             HallLPF &lpf,
                             HallMotorPIDState &mp,
                             double target_theta,
                             int32_t current_counts,
                             double &out_vel_cmd_rpm);

        std::optional<std::pair<double, double>> BT_solution_(double phi, double theta, double L = 70.726) const;

        bool wristControlStep_(double pitch_Target_Theta,
                               double roll_Target_Theta,
                               double yaw_Target_Theta,
                               int32_t left_enc_counts,
                               int32_t right_enc_counts,
                               double &out_left_vel,
                               double &out_right_vel);

    private:
        ModuleLayout layout_;

        // Per-DOF states
        std::vector<CascadedState> cascaded_state_;
        std::vector<HallLPF> hall_lpf_;
        std::vector<HallMotorPIDState> hall_pid_;

        std::unordered_map<int, int32_t> motor_zero_linearity_pos_counts_;

        // Wrist state
        WristLPF wrist_lpf_;
        WristMotorPID wrist_left_pid_;
        WristMotorPID wrist_right_pid_;

        // Cached module-local indices for wrist DOFs/motors (if present in THIS module)
        bool has_wrist_ = false;
        bool has_pitch_ = false, has_yaw_ = false, has_roll_ = false;
        uint32_t pitch_di_ = 0, yaw_di_ = 0, roll_di_ = 0;

        bool has_wrist_motors_ = false;
        uint32_t left_mi_ = 0, right_mi_ = 0;

        // ---------------- Original constants (unchanged) ----------------
        double initialized_theta_ = 0.74918;
        double joint_to_fix_dist_mm_ = 9.410;
        double joint_to_port_dist_mm_ = 9.413;

        double linear_screw_pitch_1_mm_per_rev_ = 1.0;
        int64_t encoder_counts_per_rev_1_ = (1LL << 18);

        // Wrist
        double linear_screw_pitch_2_mm_per_rev_ = 3.5;
        int64_t encoder_counts_per_rev_2_ = (1LL << 18);

        int num_joints_on_tendon_ = 1;

        // Cascaded gains
        double kp_pos_1_ = 150, kp_pos_2_ = 100, kp_pos_3_ = 50, kp_pos_4_ = 20;
        double ki_pos_1_ = 1.3328, ki_pos_2_ = 1.0, ki_pos_3_ = 0.8, ki_pos_4_ = 0.5;
        double kd_pos_1_ = 10, kd_pos_2_ = 15, kd_pos_3_ = 24, kd_pos_4_ = 45;

        double kp_vel_ = 20, ki_vel_ = 0.1, kd_vel_ = 0.05;

        double max_velocity_rpm_ = 3000;
        double min_velocity_rpm_ = -3000;
        double max_current_mA_ = 3000;
        double min_current_mA_ = -3000;

        // Hall
        double kp_pos_counts_ = 0.005;
        double ki_pos_counts_ = 0.00005;
        double kd_pos_counts_ = 0.0001;

        double hall_max_velocity_rpm_ = 1600;
        double hall_min_velocity_rpm_ = -1600;

        // Wrist PID gains (kept)
        double wrist_pitch_Kp_ = 0.005;
        double wrist_pitch_Ki_ = 0.00005;
        double wrist_pitch_Kd_ = 0.0001;

        double wrist_max_velocity_rpm_ = 1600;
        double wrist_min_velocity_rpm_ = -1600;
    };

} // namespace grip
