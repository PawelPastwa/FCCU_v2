#include "fccu_flow.h"
#include "fccu_config.h"
#include <stddef.h>
#include <string.h>

void fccu_flow_init(fccu_flow_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
}

void fccu_flow_reset_volume(fccu_flow_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    ctx->total_volume_x1000 = 0U;
    ctx->total_volume_consumed_ln = 0U;
    ctx->total_pulses = 0ULL;
}

uint16_t fccu_flow_calc_x100(uint32_t current_hz) {
    uint32_t flow = (current_hz * FCCU_FLOW_SCALE_X100) / FCCU_FLOW_K_FACTOR;
    return (flow > UINT16_MAX) ? UINT16_MAX : (uint16_t)flow;
}

uint32_t fccu_flow_calc_x1000_from_period_us(uint32_t period_us) {
    if (period_us == 0U || FCCU_FLOW_K_FACTOR == 0U) {
        return 0U;
    }
    /* flow [Ln/min] = (1e6 / period_us) * 60 / K -> x1000 = 60e9 / (period_us * K) */
    uint64_t denom = (uint64_t)period_us * (uint64_t)FCCU_FLOW_K_FACTOR;
    uint64_t flow_x1000 = 60000000000ULL / denom;
    return (flow_x1000 > UINT32_MAX) ? UINT32_MAX : (uint32_t)flow_x1000;
}

uint32_t fccu_flow_calc_volume_x1000(uint64_t total_pulses) {
    if (FCCU_FLOW_K_FACTOR == 0U) {
        return 0U;
    }
    uint64_t vol_x1000 = (total_pulses * 1000ULL) / (uint64_t)FCCU_FLOW_K_FACTOR;
    return (vol_x1000 > UINT32_MAX) ? UINT32_MAX : (uint32_t)vol_x1000;
}

void fccu_flow_update_1s(fccu_flow_ctx_t *ctx, uint32_t pulses_per_sec) {
    if (ctx == NULL) {
        return;
    }
    ctx->total_pulses += pulses_per_sec;
    ctx->current_flow_x100 = fccu_flow_calc_x100(pulses_per_sec);
    ctx->current_flow_x1000 = (uint32_t)ctx->current_flow_x100 * 10U;
    ctx->total_volume_x1000 = fccu_flow_calc_volume_x1000(ctx->total_pulses);
    ctx->total_volume_consumed_ln = ctx->total_volume_x1000 / 10U;
}

void fccu_flow_update_fast(fccu_flow_ctx_t *ctx, uint32_t effective_period_us, uint64_t total_pulses) {
    if (ctx == NULL) {
        return;
    }
    ctx->total_pulses = total_pulses;
    ctx->current_flow_x1000 = fccu_flow_calc_x1000_from_period_us(effective_period_us);
    uint32_t flow_x100 = (ctx->current_flow_x1000 + 5U) / 10U;
    ctx->current_flow_x100 = (flow_x100 > UINT16_MAX) ? UINT16_MAX : (uint16_t)flow_x100;
    ctx->total_volume_x1000 = fccu_flow_calc_volume_x1000(total_pulses);
    ctx->total_volume_consumed_ln = ctx->total_volume_x1000 / 10U;
}