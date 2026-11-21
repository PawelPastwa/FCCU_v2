//
// Created by pawel on 19/11/2026.
//

#ifndef INZYNIERKA_FCCU_EVENTS_H
#define INZYNIERKA_FCCU_EVENTS_H

#include <zephyr/kernel.h>
#include <stdint.h>

typedef enum {
    FCCU_EVT_CAN_SYSTEM_ENABLE,
    FCCU_EVT_CAN_MAIN_VALVE,
    FCCU_EVT_CAN_MANUAL_PURGE,
    FCCU_EVT_CAN_FAN_CMD,
    FCCU_EVT_CAN_FAN_SETTINGS,
    FCCU_EVT_CAN_VALVE_SETTINGS,
    FCCU_EVT_BTN_SINGLE_CLICK,
    FCCU_EVT_BTN_DOUBLE_CLICK,
    FCCU_EVT_PURGE_TIMEOUT
} fccu_event_type_t;

typedef struct {
    fccu_event_type_t type;
    uint32_t          timestamp_ms;
    union {
        uint8_t u8_val;
        struct {
            uint8_t  mode;
            uint16_t p_gain;
            uint16_t i_gain;
            uint16_t d_gain;
        } fan_settings;
        struct {
            uint16_t interval_s;
            uint16_t duration_ms;
        } valve_settings;
    } data;
} fccu_event_t;

/* Globalna kolejka zdarzeń zdefiniowana w main.c */
extern struct k_msgq fccu_event_q;

static inline int fccu_event_post(const fccu_event_t *evt) {
    /* K_NO_WAIT pozwala bezpiecznie wołać tę funkcję także z ISR */
    return k_msgq_put(&fccu_event_q, evt, K_NO_WAIT);
}

#endif //INZYNIERKA_FCCU_EVENTS_H
