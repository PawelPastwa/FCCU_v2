#include "fccu_flow.h"
#include "fccu_config.h"
#include <stddef.h>

void fccu_flow_init(fccu_flow_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    ctx->current_flow_x100 = 0U;
    ctx->total_volume_consumed_ln = 0U;
}

void fccu_flow_reset_volume(fccu_flow_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    ctx->total_volume_consumed_ln = 0U;
}

uint16_t fccu_flow_calc_x100(uint32_t current_hz) {
    uint32_t flow = (current_hz * FCCU_FLOW_SCALE_X100) / FCCU_FLOW_K_FACTOR;
    return (flow > UINT16_MAX) ? UINT16_MAX : (uint16_t)flow;
}

void fccu_flow_update_1s(fccu_flow_ctx_t *ctx, uint32_t pulses_per_sec) {
    if (ctx == NULL) {
        return;
    }
    ctx->current_flow_x100 = fccu_flow_calc_x100(pulses_per_sec);
    ctx->total_volume_consumed_ln += (ctx->current_flow_x100 / 60U);
}