#include "fccu_fan.h"
#include "fccu_config.h"
#include <string.h>

void fccu_fan_init(fccu_fan_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->mode = FAN_MODE_MANUAL;
}

void fccu_fan_stop(fccu_fan_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    ctx->state = 0U;
    ctx->duty = FCCU_FAN_DUTY_MIN_PCT;
    ctx->pid_integral = 0;
    ctx->pid_prev_error = 0;
}

void fccu_fan_set_settings(fccu_fan_ctx_t *ctx, uint8_t mode, uint16_t p, uint16_t i, uint16_t d) {
    if (ctx == NULL) {
        return;
    }
    if (mode <= FAN_MODE_PID) {
        if (ctx->mode != mode) {
            ctx->pid_integral = 0;
            ctx->pid_prev_error = 0;
        }
        ctx->mode = mode;
    }
    ctx->p_gain = p;
    ctx->i_gain = i;
    ctx->d_gain = d;
}

void fccu_fan_set_manual(fccu_fan_ctx_t *ctx, uint8_t fan_on) {
    if (ctx == NULL) {
        return;
    }
    ctx->state = (fan_on != 0U) ? 1U : 0U;
    if (ctx->mode == FAN_MODE_MANUAL) {
        ctx->duty = (ctx->state != 0U) ? FCCU_FAN_DUTY_MAX_PCT : FCCU_FAN_DUTY_MIN_PCT;
    }
}

uint8_t fccu_fan_clamp_duty(uint8_t duty_percent) {
    return (duty_percent > FCCU_FAN_DUTY_MAX_PCT) ? FCCU_FAN_DUTY_MAX_PCT : duty_percent;
}

void fccu_fan_step(fccu_fan_ctx_t *ctx, bool is_running, uint8_t current_temp_c) {
    if (ctx == NULL || !is_running) {
        return;
    }

    switch (ctx->mode) {
        case FAN_MODE_MANUAL:
            break;

        case FAN_MODE_LINEAR:
            if (current_temp_c >= FCCU_FAN_LINEAR_ON_TEMP_C) {
                ctx->state = 1U;
                ctx->duty = FCCU_FAN_DUTY_MAX_PCT;
            } else if (current_temp_c <= FCCU_FAN_LINEAR_OFF_TEMP_C) {
                ctx->state = 0U;
                ctx->duty = FCCU_FAN_DUTY_MIN_PCT;
            }
            break;

        case FAN_MODE_PID: {
            int32_t error = (int32_t)current_temp_c - FCCU_FAN_PID_SETPOINT_C;
            ctx->pid_integral += error;

            int32_t p_term = (int32_t)ctx->p_gain * error;
            int32_t i_term = (int32_t)ctx->i_gain * ctx->pid_integral;
            int32_t d_term = (int32_t)ctx->d_gain * (error - ctx->pid_prev_error);

            ctx->pid_prev_error = error;
            int32_t output = p_term + i_term + d_term;

            if (output >= (int32_t)FCCU_FAN_DUTY_MAX_PCT) {
                output = (int32_t)FCCU_FAN_DUTY_MAX_PCT;
                if (error > 0) {
                    ctx->pid_integral -= error;
                }
            } else if (error <= 0 || output <= (int32_t)FCCU_FAN_DUTY_MIN_PCT) {
                output = (int32_t)FCCU_FAN_DUTY_MIN_PCT;
                ctx->pid_integral = 0;
            }

            ctx->duty = (uint8_t)output;
            ctx->state = (ctx->duty > 0U) ? 1U : 0U;
            break;
        }

        default:
            break;
    }
}