#include "fccu_state.h"
#include "fccu_config.h"
#include <stddef.h>

void fccu_state_init(fccu_state_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    ctx->state = STATE_INIT;
    ctx->last_can_rx_ms = 0U;
}

void fccu_state_notify_can_rx(fccu_state_ctx_t *ctx, uint32_t now_ms) {
    if (ctx == NULL) {
        return;
    }
    ctx->last_can_rx_ms = now_ms;
}

void fccu_state_set_enable(fccu_state_ctx_t *ctx, uint8_t cmd_enable, uint32_t now_ms) {
    if (ctx == NULL) {
        return;
    }
    ctx->last_can_rx_ms = now_ms;

    if (cmd_enable == 1U && (ctx->state == STATE_INIT || ctx->state == STATE_STOPPED)) {
        ctx->state = STATE_STARTING;
    } else if (cmd_enable == 0U) {
        ctx->state = STATE_STOPPED;
    }
}

fccu_state_t fccu_state_step(fccu_state_ctx_t *ctx, uint32_t now_ms) {
    if (ctx == NULL) {
        return STATE_STOPPED;
    }

    if (ctx->state == STATE_STARTING) {
        ctx->state = STATE_RUNNING;
        ctx->last_can_rx_ms = now_ms;
    }

    return ctx->state;
}

bool fccu_state_check_failsafe(fccu_state_ctx_t *ctx, uint32_t now_ms) {
    if (ctx == NULL || ctx->state != STATE_RUNNING) {
        return false;
    }

    if ((now_ms - ctx->last_can_rx_ms) >= FCCU_CAN_TIMEOUT_MS) {
        ctx->state = STATE_STOPPED;
        return true;
    }
    return false;
}