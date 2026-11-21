#ifndef INZYNIERKA_FCCU_SCALING_H
#define INZYNIERKA_FCCU_SCALING_H

#include <stdint.h>
#include "fccu_config.h"

static inline int32_t scale_div_10k_15k_mv(int32_t adc_mv) {
    return (adc_mv * FCCU_DIV_10K_15K_NUM) / FCCU_DIV_10K_15K_DEN;
}

/* Zwraca napięcie stosu w [V * 100] (skala DBC 0.01 V) */
static inline int16_t calc_fc_voltage_x100(int32_t adc_mv) {
    int32_t vin_mv = (adc_mv * FCCU_DIV_FC_V_NUM) / FCCU_DIV_FC_V_DEN;
    return (int16_t)(vin_mv / 10); /* mV / 10 == V * 100 */
}

/* Zwraca ciśnienie w [bar * 100] (skala DBC 0.01 bar) */
static inline uint16_t calc_pressure_x100(int32_t adc_mv, int32_t mv_min, int32_t mv_max, int32_t max_bar_x100) {
    int32_t vin_mv = scale_div_10k_15k_mv(adc_mv);
    if (vin_mv <= mv_min) {
        return 0U;
    }
    if (vin_mv >= mv_max) {
        return (uint16_t)max_bar_x100;
    }
    return (uint16_t)(((vin_mv - mv_min) * max_bar_x100) / (mv_max - mv_min));
}

/* Zwraca prąd ogniwa w [A * 100] (skala DBC 0.01 A) */
static inline int16_t calc_fc_current_x100(int32_t adc_mv) {
    int32_t vin_mv = scale_div_10k_15k_mv(adc_mv);
    return (int16_t)(((vin_mv - FCCU_FC_C_ZERO_MV) * 100) / FCCU_FC_C_SENS_MV_PER_A);
}

#endif //INZYNIERKA_FCCU_SCALING_H
