#ifndef INZYNIERKA_FCCU_FAN_H
#define INZYNIERKA_FCCU_FAN_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    FAN_MODE_MANUAL = 0,
    FAN_MODE_LINEAR = 1,
    FAN_MODE_PID    = 2
} fccu_fan_mode_t;

typedef struct {
    uint8_t  mode;
    uint8_t  state;
    uint8_t  duty;
    uint8_t  manual_state;
    uint8_t  manual_duty;
    uint16_t p_gain;
    uint16_t i_gain;
    uint16_t d_gain;
    int32_t  pid_integral;
    int32_t  pid_prev_error;
} fccu_fan_ctx_t;

void    fccu_fan_init(fccu_fan_ctx_t *ctx);
void    fccu_fan_stop(fccu_fan_ctx_t *ctx);
void    fccu_fan_resume(fccu_fan_ctx_t *ctx);
void    fccu_fan_set_settings(fccu_fan_ctx_t *ctx, uint8_t mode, uint16_t p, uint16_t i, uint16_t d);
void    fccu_fan_set_manual(fccu_fan_ctx_t *ctx, uint8_t fan_on);
void    fccu_fan_set_manual_duty(fccu_fan_ctx_t *ctx, uint8_t fan_on, uint8_t duty_percent);
uint8_t fccu_fan_clamp_duty(uint8_t duty_percent);
void    fccu_fan_step(fccu_fan_ctx_t *ctx, bool is_running, uint8_t current_temp_c);

#endif //INZYNIERKA_FCCU_FAN_H