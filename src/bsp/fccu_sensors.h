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
    int16_t  ambient_temp_x100;     /* degC * 100 (np. 2540 = 25.40 C) */
    uint16_t ambient_humidity_x100; /* % * 100 */
    uint16_t ambient_pressure_x100; /* hPa * 100 */
} fccu_sensors_data_t;

int      fccu_sensors_init(void);
void     fccu_sensors_read_all(fccu_sensors_data_t *out);
uint32_t fccu_sensors_consume_flow_pulses(void);

#endif //INZYNIERKA_FCCU_SENSORS_H
