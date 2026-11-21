//
// Created by pawel on 19/11/2026.
//

#ifndef INZYNIERKA_FCCU_VALVES_H
#define INZYNIERKA_FCCU_VALVES_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t  main_valve;
    uint8_t  purge_valve;
    uint16_t purge_interval_s;
    uint16_t purge_duration_ms;
    uint32_t last_purge_time_ms;
} fccu_valves_ctx_t;

void fccu_valves_init(fccu_valves_ctx_t *ctx);
void fccu_valves_reset_outputs(fccu_valves_ctx_t *ctx);
void fccu_valves_sync_purge_timer(fccu_valves_ctx_t *ctx, uint32_t now_ms);

void fccu_valves_set_main(fccu_valves_ctx_t *ctx, uint8_t state);
void fccu_valves_toggle_main(fccu_valves_ctx_t *ctx);

bool fccu_valves_set_settings(fccu_valves_ctx_t *ctx, uint16_t interval_s, uint16_t duration_ms);
bool fccu_valves_start_purge(fccu_valves_ctx_t *ctx, bool is_running, uint32_t now_ms);
void fccu_valves_close_purge(fccu_valves_ctx_t *ctx);
bool fccu_valves_check_auto_purge(fccu_valves_ctx_t *ctx, bool is_running, uint32_t now_ms);

#endif //INZYNIERKA_FCCU_VALVES_H
