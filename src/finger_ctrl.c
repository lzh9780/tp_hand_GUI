#define _GNU_SOURCE
#include "grip/finger_ctrl.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

/* ---------- helpers ---------- */
static inline double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline double rad2deg(double r) { return r * (180.0 / M_PI); }

/* L'(theta)/pitch = (fix*port*sin(theta))/sqrt(fix^2+port^2+2*fix*port*cos(theta)) / pitch */
static double tendon_factor_over_pitch(double th, double fix, double port, double pitch)
{
    const double denom = sqrt(fix * fix + port * port + 2.0 * fix * port * cos(th));
    if (denom <= 0.0 || pitch <= 0.0)
        return 0.0;
    return (fix * port * sin(th)) / (denom * pitch);
}

/* 3-stage switching to pick which segment angle represents current_theta (same as Python) */
static double select_stage_theta(double mcp, double pip, double dip)
{
    const double d75 = 75.0 * (M_PI / 180.0);
    const double d65 = 65.0 * (M_PI / 180.0);
    const double eps = 0.5 * (M_PI / 180.0);
    if (mcp < d75 && pip <= eps)
        return mcp; /* stage 1 */
    if (mcp > d65 && pip >= 0.6 * (M_PI / 180.0) && dip <= eps)
        return pip; /* stage 2 */
    if (mcp > d65 && pip > d65 && dip >= eps)
        return dip;                 /* stage 3 */
    return (mcp + pip + dip) / 3.0; /* fallback */
}

/* ---------- defaults ---------- */
static void set_defaults(fc_ctx_t *c)
{
    memset(c, 0, sizeof(*c));

    c->cfg.init_theta_rad = 0.74918;
    c->cfg.tendon_fix_mm = 9.410;
    c->cfg.tendon_port_mm = 9.413;
    c->cfg.screw_pitch_mm_per_rev = 1.0;

    c->cfg.kp_pos = 80.0;
    c->cfg.kp_pos_1 = 150.0;
    c->cfg.kp_pos_2 = 120.0; // 100
    c->cfg.kp_pos_3 = 80.0;  // 50
    c->cfg.kp_pos_4 = 50.0;  // 20

    c->cfg.ki_pos = 1.3328;
    c->cfg.ki_pos_1 = 1.3328;
    c->cfg.ki_pos_2 = 1.0;
    c->cfg.ki_pos_3 = 0.8;
    c->cfg.ki_pos_4 = 0.5;

    c->cfg.kd_pos = 5.3315;
    c->cfg.kd_pos_1 = 10;
    c->cfg.kd_pos_2 = 15;
    c->cfg.kd_pos_3 = 24;
    c->cfg.kd_pos_4 = 45;

    c->cfg.kp_vel = 20.0;
    c->cfg.ki_vel = 0.1;
    c->cfg.kd_vel = 0.05;

    c->cfg.vel_ref_min = -3000.0;
    c->cfg.vel_ref_max = 3000.0;
    c->cfg.curr_min_mA = -5000;
    c->cfg.curr_max_mA = 5000;

    /* example motor id mapping 1..5 */
    c->joint[FC_THUMB_CMC_PULL].motor_id = 1;
    c->joint[FC_INDEX_MCP].motor_id = 2;
    c->joint[FC_MIDDLE_MCP].motor_id = 3;
    c->joint[FC_RING_MCP].motor_id = 4;
    c->joint[FC_PINKY_MCP].motor_id = 5;

    for (int i = 0; i < FC_JOINT_COUNT; ++i)
        c->joint[i].outer_period = 10;
}

void fc_init(fc_ctx_t *ctx) { set_defaults(ctx); }
void fc_set_motor(fc_ctx_t *ctx, fc_joint_t j, int motor_id)
{
    if (!ctx || j < 0 || j >= FC_JOINT_COUNT)
        return;
    ctx->joint[j].motor_id = motor_id;
}
fc_config_t *fc_cfg(fc_ctx_t *ctx) { return ctx ? &ctx->cfg : 0; }

/* ---------- control step (translation of finger_control) ---------- */
void fc_step(fc_ctx_t *ctx,
             fc_joint_t joint,
             double target_theta_rad,
             double theta_mcp, double theta_pip, double theta_dip,
             double d_mcp, double d_pip, double d_dip,
             int *out_motor_id, int32_t *out_current_mA)
{
    // printf("Calculating\n");
    // printf("%f, %f, %f, %f, %f, %f, %f\n", target_theta_rad, theta_mcp, theta_pip, theta_dip, d_mcp, d_pip, d_dip);
    fc_joint_state_t *J = &ctx->joint[joint];
    const fc_config_t *C = &ctx->cfg;

    /* Inputs */
    const double theta_sum = theta_mcp + theta_pip + theta_dip;
    const double theta_sum_deg = rad2deg(theta_sum);
    const double theta_dot_sum = d_mcp + d_pip + d_dip;
    const double target_deg = rad2deg(target_theta_rad);

    /* Stage selection + tendon kinematics */
    const double stage_theta = select_stage_theta(theta_mcp, theta_pip, theta_dip);
    const double theta_abs = C->init_theta_rad + stage_theta;
    const double motor_speed_proxy = theta_dot_sum * tendon_factor_over_pitch(
                                                         theta_abs, C->tendon_fix_mm, C->tendon_port_mm, C->screw_pitch_mm_per_rev);
    /* ^ equals (current_theta_speed_sum * factor_alpha) / pitch in Python,
       units used as the "RPM proxy" for the inner loop.  */

    /* First motor of this joint (same as Python picking the first id) */
    const int motor_id = J->motor_id;

    /* ---- OUTER LOOP each 10 calls ---- */
    if (++J->loop_counter >= J->outer_period)
    {
        J->loop_counter = 0;

        double e_pos = target_theta_rad - theta_sum; /* radians on sum */
        const double e_deg = rad2deg(e_pos);
        double d_pos = e_pos - J->pos_prev; /* discrete deriv */
        J->pos_prev = e_pos;

        /* Integral anti-windup (±120 deg in rad) */
        J->pos_int += e_pos;
        {
            const double Imax = 120.0 * (M_PI / 180.0);
            if (J->pos_int > Imax)
                J->pos_int = Imax;
            if (J->pos_int < -Imax)
                J->pos_int = -Imax;
        }

        

        /* Dynamic Kp selection (exact bands from script) */
        double Kp_pos;
        double Ki_pos;
        double Kd_pos;
        const double ae = fabs(e_deg);
        Kp_pos = C->kp_pos;
        Ki_pos = C->ki_pos;
        if (ae > 70.0){
            Kp_pos = C->kp_pos_1;
            Ki_pos = C->ki_pos_1;
            Kd_pos = C->kd_pos_1;
        }
        else if (ae > 30.0 && ae <= 70){
            Kp_pos = C->kp_pos_2;
            Ki_pos = C->ki_pos_2;
            Kd_pos = C->kd_pos_2;
        }
        else if (ae > 10.0 && ae <= 30){
            Kp_pos = C->kp_pos_3;
            Ki_pos = C->ki_pos_3;
            Kd_pos = C->kd_pos_3;
        }
        else{
            Kp_pos = C->kp_pos_4;
            Ki_pos = C->ki_pos_4;
            Kd_pos = C->kd_pos_4;
        }

        
        double vref; 
        /* Step Groove Brake */
        //printf("d_pos = %f\n", d_pos);
        if (fabs(e_deg) < 80.0 && fabs(d_pos) > 0.3)
        {
            vref = Kp_pos * e_pos + Kd_pos * d_pos * 1.0; //* (d_pos > 0 ? 0.3 : -0.3);
            // printf("Brake $$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n");
            J->pos_int = 0.0;
        }

        else if (fabs(e_deg) < 60.0 && fabs(d_pos) > 0.27)
        {
            vref = Kp_pos * e_pos + Kd_pos * d_pos * 1.2; //* (d_pos > 0 ? 0.27 : -0.27);
            // printf("Brake ##################################################################################\n");
            J->pos_int = 0.0;
        }

        else if (fabs(e_deg) < 40.0 && fabs(d_pos) > 0.22)
        {
            vref = Kp_pos * e_pos + Kd_pos * d_pos * 1.6; //* (d_pos > 0 ? 0.22 : -0.22);
            // printf("Brake ??????????????????????????????????????????????????????????????????????????????????\n");
            J->pos_int = 0.0;
        }
        
        else if (fabs(e_deg) < 20.0 && fabs(d_pos) > 0.15)
        {
            vref = Kp_pos * e_pos + Kd_pos * d_pos * 2.5; //* (d_pos > 0 ? 0.15 : -0.15);
            // printf("Brake !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
            J->pos_int = 0.0;
        }
        else {
            /* Velocity_ref = Kp*e + Kd*de + Ki*I (clamped) */
            vref = Kp_pos * e_pos + Kd_pos * d_pos + Ki_pos * J->pos_int;
        }
        J->vel_ref = clampd(vref, C->vel_ref_min, C->vel_ref_max);
    }

    /* ---- INNER LOOP every call ---- */
    double e_vel = J->vel_ref - motor_speed_proxy;
    double d_vel = e_vel - J->vel_prev;
    J->vel_prev = e_vel;

    /* Anti-windup on vel integral: ±10% of current range (per script) */
    J->vel_int += e_vel;
    {
        const double Imax = (double)C->curr_max_mA * 0.1;
        const double Imin = (double)C->curr_min_mA * 0.1;
        if (J->vel_int > Imax)
            J->vel_int = Imax;
        if (J->vel_int < Imin)
            J->vel_int = Imin;
    }

    /* Instantaneous equilibrium: int(target_deg)==int(sum_deg) → zero integrator & error */
    double kp_vel = C->kp_vel;
    if ((int)target_deg == (int)theta_sum_deg)
    {
        //kp_vel = 0;
        e_vel = 0.0;
        J->vel_int = 0.0; // !!!!
        /* Python also zeroes prev_vel_error; not needed for stability here */
    }

    const double torque = kp_vel * e_vel + C->kd_vel * d_vel + C->ki_vel * J->vel_int;
    int32_t current_mA = (int32_t)llround(torque);
    current_mA = clampi(current_mA, C->curr_min_mA, C->curr_max_mA);

    if (out_motor_id)
        *out_motor_id = motor_id;
    if (out_current_mA)
        *out_current_mA = current_mA;
    
    // printf("%d, %d\n", motor_id, current_mA);
}
