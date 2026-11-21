//
// Created by pawel on 19/11/2026.
//

#ifndef INZYNIERKA_FCCU_FLOW_H
#define INZYNIERKA_FCCU_FLOW_H

#include <stdint.h>

typedef struct {
    uint16_t current_flow_x100;        /* Skala 0.01 ln/min */
    uint32_t total_volume_consumed_ln; /* Skala 0.01 ln */
} fccu_flow_ctx_t;

void     fccu_flow_init(fccu_flow_ctx_t *ctx);
void     fccu_flow_reset_volume(fccu_flow_ctx_t *ctx);
uint16_t fccu_flow_calc_x100(uint32_t current_hz);
void     fccu_flow_update_1s(fccu_flow_ctx_t *ctx, uint32_t pulses_per_sec);

#endif //INZYNIERKA_FCCU_FLOW_H
