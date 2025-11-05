#ifndef FINGER_CTRL_H
#define FINGER_CTRL_H
#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

    /* Logical joints (map them as you like) */
    typedef enum
    {
        FC_THUMB_CMC_PULL = 0,
        FC_INDEX_MCP,
        FC_MIDDLE_MCP,
        FC_RING_MCP,
        FC_PINKY_MCP,
        FC_JOINT_COUNT
    } fc_joint_t;

    /* Static configuration (from Cascaded_PID_8.py’s constants) */
    typedef struct
    {
        /* geometry / kinematics */
        double init_theta_rad;         /* initialized_Theta */
        double tendon_fix_mm;          /* Joint_to_Tendon_Fix_Point_Distance */
        double tendon_port_mm;         /* Joint_to_Tendon_Port_Point_Distance */
        double screw_pitch_mm_per_rev; /* Linear_Screw_Pitch_1 */

        /* dynamic position gain bands (Kp_pos_1..4) */
        double kp_pos;
        double kp_pos_1; /* |e| > 70 deg */
        double kp_pos_2; /* 30 < |e| <= 70 */
        double kp_pos_3; /* 10 < |e| <= 30 */
        double kp_pos_4; /* otherwise     */
        double ki_pos;
        double ki_pos_1; /* |e| > 70 deg */
        double ki_pos_2; /* 30 < |e| <= 70 */
        double ki_pos_3; /* 10 < |e| <= 30 */
        double ki_pos_4; /* otherwise     */
        double kd_pos;
        double kd_pos_1; /* |e| > 70 deg */
        double kd_pos_2; /* 30 < |e| <= 70 */
        double kd_pos_3; /* 10 < |e| <= 30 */
        double kd_pos_4; /* otherwise     */

        /* inner velocity PID */
        double kp_vel;
        double ki_vel;
        double kd_vel;

        /* limits (Velocity_ref, Current_command) */
        double vel_ref_min;
        double vel_ref_max;
        int32_t curr_min_mA;
        int32_t curr_max_mA;
    } fc_config_t;

    /* Per-joint persistent state */
    typedef struct
    {
        int loop_counter; /* outer loop every 10 calls */
        double vel_ref;   /* Velocity_ref */
        int outer_period; /* default = 10 */

        /* outer PID (position on sum) */
        double pos_prev; /* prev_pos_error */
        double pos_int;  /* pos_error_int  */

        /* inner PID (velocity) */
        double vel_prev; /* prev_vel_error */
        double vel_int;  /* vel_error_int  */

        int motor_id; /* first motor bound to this joint */
    } fc_joint_state_t;

    /* Controller context */
    typedef struct
    {
        fc_config_t cfg;
        fc_joint_state_t joint[FC_JOINT_COUNT];
    } fc_ctx_t;

    /* ---- API ---- */
    void fc_init(fc_ctx_t *ctx);                                  /* defaults + example motor map (1..5) */
    void fc_set_motor(fc_ctx_t *ctx, fc_joint_t j, int motor_id); /* override a motor id */
    fc_config_t *fc_cfg(fc_ctx_t *ctx);                           /* tweak gains/limits */

    void fc_step(fc_ctx_t *ctx,
                 fc_joint_t joint,
                 double target_theta_rad,
                 double theta_mcp, double theta_pip, double theta_dip,
                 double d_mcp, double d_pip, double d_dip,
                 int *out_motor_id, int32_t *out_current_mA);

#ifdef __cplusplus
}
#endif
#endif /* FINGER_CTRL_H */
