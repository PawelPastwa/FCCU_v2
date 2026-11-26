#ifndef INZYNIERKA_FCCU_STATE_H
#define INZYNIERKA_FCCU_STATE_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    STATE_INIT     = 0,
    STATE_STARTING = 1,
    STATE_RUNNING  = 2,
    STATE_STOPPED  = 3
} fccu_state_t;

typedef struct {
    fccu_state_t state;
    uint32_t     last_can_rx_ms;
} fccu_state_ctx_t;

void         fccu_state_init(fccu_state_ctx_t *ctx);
void         fccu_state_notify_can_rx(fccu_state_ctx_t *ctx, uint32_t now_ms);
void         fccu_state_set_enable(fccu_state_ctx_t *ctx, uint8_t cmd_enable, uint32_t now_ms);
fccu_state_t fccu_state_step(fccu_state_ctx_t *ctx, uint32_t now_ms);
bool         fccu_state_check_failsafe(fccu_state_ctx_t *ctx, uint32_t now_ms);

#endif //INZYNIERKA_FCCU_STATE_H