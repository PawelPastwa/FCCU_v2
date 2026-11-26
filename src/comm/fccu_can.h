//
// Created by pawel on 19/11/2026.
//

#ifndef INZYNIERKA_FCCU_CAN_H
#define INZYNIERKA_FCCU_CAN_H

#include <stdint.h>
#include "fccu_state.h"
#include <stdbool.h>

typedef struct {
    fccu_state_t state;
    uint8_t      main_valve;
    uint8_t      purge_valve;
    uint8_t      fan_state;
    uint8_t      fan_duty;
    uint32_t     total_volume_x1000;
    uint16_t     lp_pressure_x100;
    uint16_t     hp_pressure_x100;
    int16_t      fc_voltage_x100;
    int16_t      fc_current_x100;
    int16_t      fc_temp_x100;
    int16_t      ambient_temp_x100;
    uint16_t     ambient_humidity_x100;
    uint16_t     ambient_pressure_x100;
} fccu_telemetry_t;

int  fccu_can_init(void);
void fccu_can_send_telemetry(const fccu_telemetry_t *tel);
void fccu_can_send_state(uint8_t state, uint8_t main_valve, uint8_t purge_valve, uint8_t fan_state, uint8_t fan_duty);
void fccu_can_send_flow_fast(uint32_t flow_x1000);
bool fccu_can_get_mcu_fc_voltage_mv(int32_t *out_mv, uint32_t now_ms);

#endif //INZYNIERKA_FCCU_CAN_H
