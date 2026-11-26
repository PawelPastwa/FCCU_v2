//
// Created by pawel on 19/11/2026.
//

#ifndef INZYNIERKA_FCCU_FLOW_H
#define INZYNIERKA_FCCU_FLOW_H

#include <stdint.h>

typedef struct {
    uint16_t current_flow_x100;        /* Skala 0.01 Ln/min (ramka 0x502) */
    uint32_t current_flow_x1000;       /* Skala 0.001 Ln/min (ramka 0x507) */
    uint32_t total_volume_x1000;       /* Skala 0.001 Ln = mLn (ramka 0x506) */
    uint32_t total_volume_consumed_ln; /* Kompatybilność wsteczna: skala 0.01 Ln */
    uint64_t total_pulses;
} fccu_flow_ctx_t;

void     fccu_flow_init(fccu_flow_ctx_t *ctx);
void     fccu_flow_reset_volume(fccu_flow_ctx_t *ctx);
uint16_t fccu_flow_calc_x100(uint32_t current_hz);
uint32_t fccu_flow_calc_x1000_from_period_us(uint32_t period_us);
uint32_t fccu_flow_calc_volume_x1000(uint64_t total_pulses);
void     fccu_flow_update_1s(fccu_flow_ctx_t *ctx, uint32_t pulses_per_sec);
void     fccu_flow_update_fast(fccu_flow_ctx_t *ctx, uint32_t effective_period_us, uint64_t total_pulses);

#endif //INZYNIERKA_FCCU_FLOW_H