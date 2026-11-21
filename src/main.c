#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "fccu_config.h"
#include "fccu_events.h"

#include "core/fccu_state.h"
#include "core/fccu_valves.h"
#include "core/fccu_fan.h"
#include "core/fccu_flow.h"

#include "bsp/fccu_actuators.h"
#include "bsp/fccu_button.h"
#include "bsp/fccu_sensors.h"
#include "bsp/fccu_watchdog.h"
#include "bsp/system_status.h"

#include "comm/fccu_can.h"

LOG_MODULE_REGISTER(fccu_main, LOG_LEVEL_INF);

K_MSGQ_DEFINE(fccu_event_q, sizeof(fccu_event_t), 16, 4);

static fccu_state_ctx_t    state_ctx;
static fccu_valves_ctx_t   valves_ctx;
static fccu_fan_ctx_t      fan_ctx;
static fccu_flow_ctx_t     flow_ctx;
static fccu_sensors_data_t sensors_data;

static void trigger_purge(uint32_t now_ms) {
    bool is_running = (state_ctx.state == STATE_RUNNING);
    if (fccu_valves_start_purge(&valves_ctx, is_running, now_ms)) {
        fccu_flow_reset_volume(&flow_ctx);
        fccu_actuators_set_purge_valve(1U);
        fccu_actuators_schedule_purge_close(valves_ctx.purge_duration_ms);
    }
}

static void handle_event(const fccu_event_t *evt) {
    switch (evt->type) {
        case FCCU_EVT_CAN_SYSTEM_ENABLE:
            fccu_state_set_enable(&state_ctx, evt->data.u8_val, evt->timestamp_ms);
            break;

        case FCCU_EVT_CAN_MAIN_VALVE:
            fccu_state_notify_can_rx(&state_ctx, evt->timestamp_ms);
            fccu_valves_set_main(&valves_ctx, evt->data.u8_val);
            break;

        case FCCU_EVT_CAN_MANUAL_PURGE:
            fccu_state_notify_can_rx(&state_ctx, evt->timestamp_ms);
            trigger_purge(evt->timestamp_ms);
            break;

        case FCCU_EVT_CAN_FAN_CMD:
            fccu_state_notify_can_rx(&state_ctx, evt->timestamp_ms);
            fccu_fan_set_manual(&fan_ctx, evt->data.u8_val);
            break;

        case FCCU_EVT_CAN_FAN_SETTINGS:
            fccu_state_notify_can_rx(&state_ctx, evt->timestamp_ms);
            fccu_fan_set_settings(&fan_ctx,
                                  evt->data.fan_settings.mode,
                                  evt->data.fan_settings.p_gain,
                                  evt->data.fan_settings.i_gain,
                                  evt->data.fan_settings.d_gain);
            break;

        case FCCU_EVT_CAN_VALVE_SETTINGS:
            fccu_state_notify_can_rx(&state_ctx, evt->timestamp_ms);
            fccu_valves_set_settings(&valves_ctx,
                                     evt->data.valve_settings.interval_s,
                                     evt->data.valve_settings.duration_ms);
            break;

        case FCCU_EVT_BTN_SINGLE_CLICK:
            if (state_ctx.state == STATE_RUNNING) {
                fccu_valves_toggle_main(&valves_ctx);
            }
            break;

        case FCCU_EVT_BTN_DOUBLE_CLICK:
            trigger_purge(evt->timestamp_ms);
            break;

        case FCCU_EVT_PURGE_TIMEOUT:
            fccu_valves_close_purge(&valves_ctx);
            fccu_actuators_set_purge_valve(0U);
            break;
    }
}

int main(void) {
    init_system_status();
    fccu_state_init(&state_ctx);
    fccu_valves_init(&valves_ctx);
    fccu_fan_init(&fan_ctx);
    fccu_flow_init(&flow_ctx);

    if (fccu_actuators_init() != 0 ||
        fccu_button_init() != 0 ||
        fccu_sensors_init() != 0 ||
        fccu_can_init() != 0 ||
        fccu_watchdog_init() != 0) {
        LOG_ERR("Hardware init failed! Entering safe halt.");
        fccu_actuators_emergency_stop();
        return -1;
    }

    LOG_INF("FCCU started successfully");

    uint32_t last_1s_tick_ms = k_uptime_get_32();
    fccu_event_t evt;

    while (1) {
        fccu_watchdog_feed();

        if (k_msgq_get(&fccu_event_q, &evt, K_MSEC(50)) == 0) {
            handle_event(&evt);
        }

        uint32_t now_ms = k_uptime_get_32();

        /* 1. Nadzór bezpieczeństwa i maszyna stanów */
        if (fccu_state_check_failsafe(&state_ctx, now_ms)) {
            LOG_WRN("CAN timeout! Entering FAIL-SAFE (STOPPED)");
        }

        fccu_state_t prev_state = state_ctx.state;
        fccu_state_t curr_state = fccu_state_step(&state_ctx, now_ms);

        if (prev_state == STATE_STARTING && curr_state == STATE_RUNNING) {
            fccu_valves_sync_purge_timer(&valves_ctx, now_ms);
        }

        /* 2. Wykonanie wyjść zależnie od stanu */
        if (curr_state == STATE_STOPPED) {
            fccu_valves_reset_outputs(&valves_ctx);
            fccu_fan_stop(&fan_ctx);
            fccu_actuators_emergency_stop();
        } else if (curr_state == STATE_RUNNING) {
            fccu_actuators_set_main_valve(valves_ctx.main_valve);

            if (fccu_valves_check_auto_purge(&valves_ctx, true, now_ms)) {
                fccu_flow_reset_volume(&flow_ctx);
                fccu_actuators_set_purge_valve(1U);
                fccu_actuators_schedule_purge_close(valves_ctx.purge_duration_ms);
            }

            uint8_t temp_c = (sensors_data.ambient_temp_x100 > 0)
                             ? (uint8_t)(sensors_data.ambient_temp_x100 / 100)
                             : 0U;
            fccu_fan_step(&fan_ctx, true, temp_c);
            fccu_actuators_set_fan_duty(fan_ctx.duty);
        }

        /* 3. Zadania okresowe co 1 s (czujniki + przepływ + telemetria CAN) */
        if ((now_ms - last_1s_tick_ms) >= 1000U) {
            last_1s_tick_ms = now_ms;

            fccu_sensors_read_all(&sensors_data);
            uint32_t pulses = fccu_sensors_consume_flow_pulses();
            fccu_flow_update_1s(&flow_ctx, pulses);

            fccu_telemetry_t tel = {
                    .state                 = curr_state,
                    .main_valve            = valves_ctx.main_valve,
                    .purge_valve           = valves_ctx.purge_valve,
                    .fan_state             = fan_ctx.state,
                    .fan_duty              = fan_ctx.duty,
                    .flow_x100             = flow_ctx.current_flow_x100,
                    .total_volume_ln       = flow_ctx.total_volume_consumed_ln,
                    .lp_pressure_x100      = (sensors_data.lp_pressure_mv > 0) ? (uint16_t)sensors_data.lp_pressure_mv : 0U,
                    .hp_pressure_x100      = (sensors_data.hp_pressure_mv > 0) ? (uint16_t)sensors_data.hp_pressure_mv : 0U,
                    .fc_voltage_x100       = (int16_t)sensors_data.fc_voltage_mv,
                    .fc_current_x100       = (int16_t)sensors_data.fc_current_mv,
                    .ambient_temp_x100     = sensors_data.ambient_temp_x100,
                    .ambient_humidity_x100 = sensors_data.ambient_humidity_x100,
                    .ambient_pressure_x100 = sensors_data.ambient_pressure_x100
            };
            fccu_can_send_telemetry(&tel);
        }
    }

    return 0;
}