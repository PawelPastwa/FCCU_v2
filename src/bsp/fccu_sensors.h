//
// Created by pawel on 19/11/2026.
//

#ifndef INZYNIERKA_FCCU_SENSORS_H
#define INZYNIERKA_FCCU_SENSORS_H

#include <stdint.h>

typedef struct {
    int32_t  lp_pressure_mv;
    int32_t  fc_voltage_mv;
    int32_t  sc_voltage_mv;
    int32_t  fc_current_mv;
    int32_t  hp_pressure_mv;
    int32_t  fc_temp_mv;            /* Surowe napięcie z ADC1_CH9 (GPIO10) [mV] */
    int16_t  fc_temp_x100;          /* Temperatura FC: degC * 100 (np. 3250 = 32.50 C) */
    int16_t  ambient_temp_x100;     /* degC * 100 (np. 2540 = 25.40 C) */
    uint16_t ambient_humidity_x100; /* % * 100 */
    uint16_t ambient_pressure_x100; /* hPa * 100 */
} fccu_sensors_data_t;

typedef struct {
    uint32_t effective_period_us;   /* Okres między impulsami [us], 0 = brak przepływu */
    uint64_t total_pulses;          /* Narastająca liczba impulsów od startu/resetu */
} fccu_flow_snapshot_t;

int      fccu_sensors_init(void);
void     fccu_sensors_read_all(fccu_sensors_data_t *out);
uint32_t fccu_sensors_consume_flow_pulses(void);
void     fccu_sensors_get_flow_snapshot(fccu_flow_snapshot_t *out);
void     fccu_sensors_reset_flow_pulses(void);

#endif //INZYNIERKA_FCCU_SENSORS_H