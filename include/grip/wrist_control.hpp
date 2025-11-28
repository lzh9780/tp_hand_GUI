#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

/* Translation of the wrist control (pitch/roll/yaw) from Hall_Position_Control_with_Protection_12.py
 * - Filters pitch/roll/yaw targets just like the script.
 * - Computes left/right tendon target via BT_solution(phi, theta).
 * - Converts to encoder counts and runs a position PID to produce velocity/current commands (mA/RPM proxy).
 * - Returns 2 commands for motors {6,7} (pitch/yaw pair). Roll is left as a stub hook.
 *
 * Units:
 *   - Inputs pitch/roll/yaw are in radians.
 *   - Encoder counts are raw int32_t.
 *   - Linear pitch is mm/rev; Encoder counts per rev map mm↔counts.
 */

namespace grip
{

  struct MotorPIDState
  {
    double e_prev{0.0};
    double e_int{0.0};
  };

  struct Config
  {
    // --- gains (match the python defaults)
    double Kp_pitch{0.005}, Ki_pitch{0.00005}, Kd_pitch{0.0001};
    double Kp_roll{0.005}, Ki_roll{0.00005}, Kd_roll{0.0001};
    double Kp_yaw{0.005}, Ki_yaw{0.00005}, Kd_yaw{0.0001};

    // --- mechanical
    double screw_pitch_mm_per_rev{3.5}; // Linear_Screw_Pitch_2
    int encoder_cpr{1 << 18};           // Encoder_Counts_Per_Revolution_2

    // centers/zeros (kept here for completeness—your script stores them in dicts)
    int32_t left_center_counts{500000};
    int32_t right_center_counts{500000};

    // PID integral clamp (anti-windup)
    double Imax{300000}, Imin{-300000};

    // Output clamps (your script clamps “velocity”; keep a generous range)
    int32_t vel_min{-600}, vel_max{600}; // same as script’s Max/Min_Velocity
  };

  struct IO
  {
    // radians
    double pitch{0.0}, roll{0.0}, yaw{0.0};
    // raw encoders
    int32_t left_counts{0}, right_counts{0};
  };

  struct Result
  {
    // Motor id and command (velocity/current proxy). Script uses motors 6 & 7.
    int motor_id;
    int32_t command;
  };

  class Controller
  {
  public:
    Controller();

    // set/get config if you want to tune from GUI
    Config &cfg() { return cfg_; }
    const Config &cfg() const { return cfg_; }

    // main entry: compute 2 commands (left/right) for pitch+yaw
    // returns empty when input outside geometric domain
    std::optional<std::array<Result, 2>>
    step(const IO &in);

  private:
    // ----- helpers -----
    // BT_solution(phi, theta) in radians → (Left_mm, Right_mm) or nullopt if invalid
    std::optional<std::pair<double, double>> BT_solution(double phi, double theta) const;

    // filters
    double filter_pitch(double pitch_rad);
    double filter_roll(double roll_rad);
    double filter_yaw(double yaw_rad);

    // filters
    double filter_pitch_2(double pitch_rad);
    double filter_roll_2(double roll_rad);
    double filter_yaw_2(double yaw_rad);

    // convert mm <-> counts
    int32_t mm_to_counts(double mm) const
    {
      const double rev = mm / cfg_.screw_pitch_mm_per_rev;
      return static_cast<int32_t>(rev * cfg_.encoder_cpr);
    }

  private:
    Config cfg_;

    // rolling buffers (sizes per script)
    static constexpr int kPitchBuf = 80;
    static constexpr int kRollBuf = 100;
    static constexpr int kYawBuf = 42;

    std::vector<double> buf_pitch_; // recent pitch targets
    std::vector<double> buf_roll_;
    std::vector<double> buf_yaw_;

    double alpha_pitch;              // LPF 参数 α
    double alpha_yaw;              // LPF 参数 α
    double prev_pitch = 0.0;   // 低通状态
    double prev_roll  = 0.0;   // 低通状态
    double prev_yaw   = 0.0;   // 低通状态

    // PID states for left/right pitch/yaw motors (IDs 6/7)
    MotorPIDState left_pid_{}, right_pid_{};

    // motor IDs consistent with the script
    static constexpr int kLeftMotorId = 1;
    static constexpr int kRightMotorId = 2;

    double prev_cmd_L = 17.5;
    double prev_cmd_R = 17.5;
  };

} // namespace wrist
