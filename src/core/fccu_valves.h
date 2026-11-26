#ifndef INZYNIERKA_FCCU_VALVES_H
#define INZYNIERKA_FCCU_VALVES_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    PURGE_MODE_TIME    = 0,
    PURGE_MODE_VOLTAGE = 1,
    PURGE_MODE_BOTH    = 2
} fccu_purge_mode_t;

typedef struct {
    uint8_t  main_valve;
    uint8_t  purge_valve;
    uint8_t  purge_mode;
    uint16_t purge_interval_s;
    uint16_t purge_duration_ms;
    uint16_t purge_voltage_threshold_mv;
    uint8_t  purge_voltage_cooldown_s;
    uint32_t last_purge_time_ms;
} fccu_valves_ctx_t;

void fccu_valves_init(fccu_valves_ctx_t *ctx);
void fccu_valves_reset_outputs(fccu_valves_ctx_t *ctx);
void fccu_valves_sync_purge_timer(fccu_valves_ctx_t *ctx, uint32_t now_ms);

void fccu_valves_set_main(fccu_valves_ctx_t *ctx, uint8_t state);
void fccu_valves_toggle_main(fccu_valves_ctx_t *ctx);

bool fccu_valves_set_settings(fccu_valves_ctx_t *ctx,
                              uint16_t interval_s,
                              uint16_t duration_ms,
                              bool has_voltage_cfg,
                              uint8_t mode,
                              uint16_t voltage_threshold_mv,
                              uint8_t voltage_cooldown_s);

bool fccu_valves_start_purge(fccu_valves_ctx_t *ctx, bool is_running, uint32_t now_ms);
void fccu_valves_close_purge(fccu_valves_ctx_t *ctx);
bool fccu_valves_check_auto_purge(fccu_valves_ctx_t *ctx,
                                  bool is_running,
                                  uint32_t now_ms,
                                  bool fc_voltage_valid,
                                  int32_t fc_voltage_mv);

#endif //INZYNIERKA_FCCU_VALVES_H