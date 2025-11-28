#include "grip/wrist_control.hpp"
#include <cmath>
#include <numeric>
#include <algorithm>
#include <iostream>

namespace grip
{

  static inline double deg(double rad) { return rad * (180.0 / M_PI); }
  static inline double rad(double deg_) { return deg_ * (M_PI / 180.0); }
  static inline double clamp(double v, double lo, double hi)
  {
    return (v < lo) ? lo : (v > hi ? hi : v);
  }

  Controller::Controller()
  {
    buf_pitch_.reserve(kPitchBuf);
    buf_roll_.reserve(kRollBuf);
    buf_yaw_.reserve(kYawBuf);

    double fs = 100.0; // 采样频率
    double fc = 18.0; // 截止频率
    double fc_2 = 20.0; // 截止频率

    alpha_pitch = (2 * M_PI * fc) / (2 * M_PI * fc + fs);
    alpha_yaw = (2 * M_PI * fc_2) / (2 * M_PI * fc_2 + fs);
  }

  double Controller::filter_pitch(double pitch_rad)
  {
    // push back newest
    buf_pitch_.push_back(pitch_rad);
    if ((int)buf_pitch_.size() > kPitchBuf)
      buf_pitch_.erase(buf_pitch_.begin());

    // used stack size = |Δθ_deg|
    double last = buf_pitch_.front();
    double ddeg = fabs(deg(pitch_rad - last));
    int used = std::clamp((int)std::round(ddeg), 0, kPitchBuf);
    used = (int)std::round(used * 0.5); // for speeding
    if (used == 0)
      used = 1;
    // average last 'used' samples
    const int n = (int)buf_pitch_.size();
    const int start = std::max(0, n - used);
    double sum = 0.0;
    for (int i = start; i < n; ++i)
      sum += buf_pitch_[i];
    return sum / (n - start);
  }

  double Controller::filter_roll(double roll_rad)
  {
    buf_roll_.push_back(roll_rad);
    if ((int)buf_roll_.size() > kRollBuf)
      buf_roll_.erase(buf_roll_.begin());
    double last = buf_roll_.front();
    double ddeg = fabs(deg(roll_rad - last));
    int used = std::clamp((int)std::round(ddeg), 0, kRollBuf);
    used = (int)std::round(used * 0.5); // for speeding
    if (used == 0)
      used = 1;
    const int n = (int)buf_roll_.size();
    const int start = std::max(0, n - used);
    double sum = 0.0;
    for (int i = start; i < n; ++i)
      sum += buf_roll_[i];
    return sum / (n - start);
  }

  double Controller::filter_yaw(double yaw_rad)
  {
    buf_yaw_.push_back(yaw_rad);
    if ((int)buf_yaw_.size() > kYawBuf)
      buf_yaw_.erase(buf_yaw_.begin());
    double last = buf_yaw_.front();
    double ddeg = fabs(deg(yaw_rad - last));
    int used = std::clamp((int)std::round(ddeg), 0, kYawBuf);
    used = (int)std::round(used * 0.5); // for speeding
    if (used == 0)
      used = 1;
    const int n = (int)buf_yaw_.size();
    const int start = std::max(0, n - used);
    double sum = 0.0;
    for (int i = start; i < n; ++i)
      sum += buf_yaw_[i];
    return sum / (n - start);
  }

  double Controller::filter_pitch_2(double pitch_rad)
  {
    double p = prev_pitch + alpha_pitch * (pitch_rad - prev_pitch);
    prev_pitch = p;
    return p;
  }

  double Controller::filter_roll_2(double roll_rad)
  {
    double r = prev_roll + alpha_pitch * (roll_rad - prev_roll);
    prev_roll = r;
    return r;
  }

  double Controller::filter_yaw_2(double yaw_rad)
  {
    
    double y = prev_yaw + alpha_yaw * (yaw_rad - prev_yaw);
    prev_yaw = y;
    return y;
  }

  // Geometric solver from the script: returns Left/Right motor target (mm)
  std::optional<std::pair<double, double>>
  Controller::BT_solution(double phi, double theta) const
  {
    // domain checks: theta ∈ [-21, 21], phi ∈ [-30, 50] (degrees)
    if (theta < rad(-21.1) || theta > rad(21.1))
      return std::nullopt;
    if (phi < rad(-30.1) || phi > rad(50.1))
      return std::nullopt;

    const double st = std::sin(theta), ct = std::cos(theta);
    const double sp = std::sin(phi), cp = std::cos(phi);
    const double yphi = 22 * cp - 31.5 * sp;
    const double zphi = 22 * sp + 31.5 * cp;

    // B
    const double Bx = 18.5 * st - 14.5 * ct + st * zphi;
    const double By = yphi;
    const double Bz = 18.5 * ct + 14.5 * st + ct * zphi;

    // B'
    const double Bpx = 18.5 * st + 14.5 * ct + st * zphi;
    const double Bpy = yphi;
    const double Bpz = 18.5 * ct - 14.5 * st + ct * zphi;

    // Sphere intersections (T at x=-21, y=29.687) and (T' at x=+21, y=29.687)
    const double L = 70.726; // link length from script
    {
      const double dx = -21 - Bx;
      const double dy = 29.687 - By;
      const double disc = L * L - dx * dx - dy * dy;
      if (disc < 0)
        return std::nullopt;
    }
    {
      const double dxp = 21 - Bpx;
      const double dyp = 29.687 - Bpy;
      const double discp = L * L - dxp * dxp - dyp * dyp;
      if (discp < 0)
        return std::nullopt;
    }

    const double Right = Bz - std::sqrt(L * L - (-21 - Bx) * (-21 - Bx) - (29.687 - By) * (29.687 - By));
    const double Left = Bpz - std::sqrt(L * L - (21 - Bpx) * (21 - Bpx) - (29.687 - Bpy) * (29.687 - Bpy));

    // offset (script adds 37.511)
    double R = Right + 37.511;
    double Lf = Left + 37.511;

    // clamp range 0..35 mm as in script
    if (R < 0 || R > 35 || Lf < 0 || Lf > 35)
      return std::nullopt;
    return std::make_pair(Lf, R);
  }

  std::optional<std::array<Result, 2>>
  Controller::step(const IO &in)
  {
    // 1) filter targets
    const double phi_f = filter_pitch_2(-in.pitch);
    const double roll_f = filter_roll_2(in.roll);
    const double theta_f = filter_yaw_2(-in.yaw);
    (void)roll_f; // roll path kept as hook (script’s roll block is a stub)

    std::cout << "pitch" << ": " << deg(phi_f) << "; ";
    std::cout << "yaw" << ": " << deg(theta_f) << "\n";

    // 2) geometry → desired tendon displacements in mm
    auto LRmm = BT_solution(phi_f, theta_f);
    double L_mm = prev_cmd_L;
    double R_mm = prev_cmd_R;
    if (LRmm)
    {
      // inputs is correct:
      L_mm = LRmm->first;
      R_mm = LRmm->second;

      prev_cmd_L = L_mm;
      prev_cmd_R = R_mm;
    }

    // 3) convert to encoder counts
    const int32_t L_counts = mm_to_counts(L_mm);
    const int32_t R_counts = mm_to_counts(R_mm);

    // 4) position errors (counts)
    int32_t eL = L_counts - in.left_counts;
    int32_t eR = R_counts - in.right_counts;
    std::cout << "Left position error" << ": " << eL << "; ";
    std::cout << "Right position error" << ": " << eR << "\n";

    // 5) PID per motor (position PID → velocity/current proxy)
    //    matches script: pos_error_int clamp ±1e8; derivative uses prev
    // left
    left_pid_.e_int += static_cast<double>(eL);
    left_pid_.e_int = clamp(left_pid_.e_int, cfg_.Imin, cfg_.Imax);
    double dL = static_cast<double>(eL) - left_pid_.e_prev;
    left_pid_.e_prev = static_cast<double>(eL);

    // Instantaneous equilibrium
    if (fabs(eL) <= 10000)
    {
      eL = 0;
      left_pid_.e_int = 0;
    }

    double cmdL = cfg_.Kp_pitch * eL + cfg_.Ki_pitch * left_pid_.e_int + cfg_.Kd_pitch * dL;
    cmdL = cmdL * 0.3;

    // right
    right_pid_.e_int += static_cast<double>(eR);
    right_pid_.e_int = clamp(right_pid_.e_int, cfg_.Imin, cfg_.Imax);
    double dR = static_cast<double>(eR) - right_pid_.e_prev;
    right_pid_.e_prev = static_cast<double>(eR);

    // Instantaneous equilibrium
    if (fabs(eR) <= 10000)
    {
      eR = 0;
      right_pid_.e_int = 0;
      dR = 0; // !!! Only for bad motors
    }

    double cmdR = cfg_.Kp_pitch * eR + cfg_.Ki_pitch * right_pid_.e_int + cfg_.Kd_pitch * dR;
    cmdR = cmdR * 0.3;

    // 6) clamp outputs (script uses velocity limits)
    int32_t outL = static_cast<int32_t>(std::lround(cmdL));
    int32_t outR = static_cast<int32_t>(std::lround(cmdR));
    outL = std::clamp(outL, cfg_.vel_min, cfg_.vel_max);
    outR = std::clamp(outR, cfg_.vel_min, cfg_.vel_max);

    std::cout << "LeftMotor" << ": " << outL << "; ";
    std::cout << "RightMotor" << ": " << outR << "\n\n";

    return std::array<Result, 2>{Result{kLeftMotorId, outL}, Result{kRightMotorId, outR}};
  }

} // namespace wrist
