#include "fccu_valves.h"
#include "fccu_config.h"
#include <string.h>

void fccu_valves_init(fccu_valves_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->purge_mode                 = PURGE_MODE_TIME;
    ctx->purge_duration_ms          = FCCU_PURGE_DURATION_DEFAULT_MS;
    ctx->purge_voltage_threshold_mv = FCCU_PURGE_VOLTAGE_DEFAULT_MV;
    ctx->purge_voltage_cooldown_s   = FCCU_PURGE_VOLTAGE_COOLDOWN_DEFAULT_S;
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

bool fccu_valves_set_settings(fccu_valves_ctx_t *ctx,
                              uint16_t interval_s,
                              uint16_t duration_ms,
                              bool has_voltage_cfg,
                              uint8_t mode,
                              uint16_t voltage_threshold_mv,
                              uint8_t voltage_cooldown_s) {
    if (ctx == NULL) {
        return false;
    }

    ctx->purge_interval_s = interval_s;

    if (has_voltage_cfg) {
        if (mode <= PURGE_MODE_BOTH) {
            ctx->purge_mode = mode;
        }
        ctx->purge_voltage_threshold_mv = voltage_threshold_mv;
        ctx->purge_voltage_cooldown_s   = (voltage_cooldown_s > 0U)
                                          ? voltage_cooldown_s
                                          : FCCU_PURGE_VOLTAGE_COOLDOWN_DEFAULT_S;
    }

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

bool fccu_valves_check_auto_purge(fccu_valves_ctx_t *ctx,
                                  bool is_running,
                                  uint32_t now_ms,
                                  bool fc_voltage_valid,
                                  int32_t fc_voltage_mv) {
    if (ctx == NULL || !is_running || ctx->main_valve == 0U || ctx->purge_valve != 0U) {
        return false;
    }

    uint32_t elapsed_ms = now_ms - ctx->last_purge_time_ms;

    /* 1. Tryb czasowy (TIME lub BOTH) */
    if ((ctx->purge_mode == PURGE_MODE_TIME || ctx->purge_mode == PURGE_MODE_BOTH) &&
        ctx->purge_interval_s > 0U) {
        if (elapsed_ms >= ((uint32_t)ctx->purge_interval_s * 1000U)) {
            return fccu_valves_start_purge(ctx, is_running, now_ms);
        }
    }

    /* 2. Tryb napięciowy z ramki MCU 0x104 (VOLTAGE lub BOTH) */
    if ((ctx->purge_mode == PURGE_MODE_VOLTAGE || ctx->purge_mode == PURGE_MODE_BOTH) &&
        fc_voltage_valid &&
        ctx->purge_voltage_threshold_mv > 0U) {
        uint32_t cooldown_ms = (uint32_t)((ctx->purge_voltage_cooldown_s > 0U)
                                          ? ctx->purge_voltage_cooldown_s
                                          : 1U) * 1000U;

        if (fc_voltage_mv >= FCCU_PURGE_VOLTAGE_MIN_VALID_MV &&
            fc_voltage_mv <= (int32_t)ctx->purge_voltage_threshold_mv &&
            elapsed_ms >= cooldown_ms) {
            return fccu_valves_start_purge(ctx, is_running, now_ms);
        }
    }

    return false;
}