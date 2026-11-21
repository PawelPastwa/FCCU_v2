//
// Created by pawel on 19/11/2026.
//

#ifndef INZYNIERKA_FCCU_ACTUATORS_H
#define INZYNIERKA_FCCU_ACTUATORS_H

#include <stdint.h>

int  fccu_actuators_init(void);
void fccu_actuators_set_main_valve(uint8_t state);
void fccu_actuators_set_purge_valve(uint8_t state);
void fccu_actuators_schedule_purge_close(uint16_t duration_ms);
void fccu_actuators_cancel_purge_close(void);
void fccu_actuators_set_fan_duty(uint8_t duty_percent);
void fccu_actuators_emergency_stop(void);

#endif //INZYNIERKA_FCCU_ACTUATORS_H
