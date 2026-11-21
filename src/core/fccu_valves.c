#include "fccu_valves.h"
#include "fccu_config.h"
#include <string.h>

void fccu_valves_init(fccu_valves_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->purge_duration_ms = FCCU_PURGE_DURATION_DEFAULT_MS;
}

void fccu_valves_reset_outputs(fccu_valves_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    ctx->main_valve = 0U;
    ctx->purge_valve = 0U;
}

void fccu_valves_sync_purge_timer(fccu_valves_ctx_t *ctx, uint32_t now_ms) {
    if (ctx == NULL) {
        return;
    }
    ctx->last_purge_time_ms = now_ms;
}

void fccu_valves_set_main(fccu_valves_ctx_t *ctx, uint8_t state) {
    if (ctx == NULL) {
        return;
    }
    ctx->main_valve = (state != 0U) ? 1U : 0U;
}

void fccu_valves_toggle_main(fccu_valves_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    ctx->main_valve = (ctx->main_valve == 0U) ? 1U : 0U;
}

bool fccu_valves_set_settings(fccu_valves_ctx_t *ctx, uint16_t interval_s, uint16_t duration_ms) {
    if (ctx == NULL) {
        return false;
    }
    ctx->purge_interval_s = interval_s;

    if (duration_ms >= FCCU_PURGE_DURATION_MIN_MS && duration_ms <= FCCU_PURGE_DURATION_MAX_MS) {
        ctx->purge_duration_ms = duration_ms;
        return true;
    }
    return false;
}

bool fccu_valves_start_purge(fccu_valves_ctx_t *ctx, bool is_running, uint32_t now_ms) {
    if (ctx == NULL || !is_running || ctx->purge_valve != 0U) {
        return false;
    }
    ctx->purge_valve = 1U;
    ctx->last_purge_time_ms = now_ms;
    return true;
}

void fccu_valves_close_purge(fccu_valves_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    ctx->purge_valve = 0U;
}

bool fccu_valves_check_auto_purge(fccu_valves_ctx_t *ctx, bool is_running, uint32_t now_ms) {
    if (ctx == NULL || !is_running || ctx->purge_interval_s == 0U) {
        return false;
    }

    if ((now_ms - ctx->last_purge_time_ms) >= ((uint32_t)ctx->purge_interval_s * 1000U)) {
        return fccu_valves_start_purge(ctx, is_running, now_ms);
    }
    return false;
}