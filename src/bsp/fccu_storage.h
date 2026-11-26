#ifndef INZYNIERKA_FCCU_STORAGE_H
#define INZYNIERKA_FCCU_STORAGE_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;

    /* Ustawienia wentylatora */
    uint8_t  fan_mode;
    uint8_t  fan_manual_on;
    uint8_t  fan_manual_duty;
    uint8_t  purge_mode;
    uint16_t fan_p_gain;
    uint16_t fan_i_gain;
    uint16_t fan_d_gain;

    /* Ustawienia purge */
    uint16_t purge_interval_s;
    uint16_t purge_duration_ms;
    uint16_t purge_voltage_threshold_mv;
    uint8_t  purge_voltage_cooldown_s;
    uint8_t  pad[3];
} fccu_persisted_cfg_t;

int  fccu_storage_init(void);
bool fccu_storage_load(fccu_persisted_cfg_t *out);
int  fccu_storage_save(const fccu_persisted_cfg_t *cfg);

#endif //INZYNIERKA_FCCU_STORAGE_H