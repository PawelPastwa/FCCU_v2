// fc - dodac tryb kolejny do purge (uzalezniony od napiecia fc ktore przyjdzie na canie z mcu), i wtedy purguje sie
// termistor na fc, pomiedzy wentylatorami a ogniwem,  (FC module na plytce podpis, po tej samej st4ronie co can)
// calkowite zuzycie do 3 miejsc po przecinku, oddzielna ramka,
// chwilowe zuzycie 100 hz albo 1000hz (odzielna ramka), zwiekszyc dokladnosc i wysylanie tak szybko jak robiony pomiar.
// calkowite zuzycie nie pojawia sie w savvy canie
// wentylatory maja zapamietywac ustawienia w pamieci flash czyli jezeli raz ustawimy na 30% wentylator albo nastawy p i d, i ze jest wlaczony to od razu po ponownym uruchomieniu pojazdu ma byc wlaczany 30% i nastawy ustawione, tak samo purge interwal itp. itd

// PCAN_USBBUS1

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
#include "bsp/fccu_storage.h"

#include "comm/fccu_can.h"

LOG_MODULE_REGISTER(fccu_main, LOG_LEVEL_INF);

K_MSGQ_DEFINE(fccu_event_q, sizeof(fccu_event_t), 16, 4);

static fccu_state_ctx_t    state_ctx;
static fccu_valves_ctx_t   valves_ctx;
static fccu_fan_ctx_t      fan_ctx;
static fccu_flow_ctx_t     flow_ctx;
static fccu_sensors_data_t sensors_data;

static uint8_t get_fan_temp_c(void) {
    int16_t temp_x100 =
#if FCCU_USE_FC_TEMP
            sensors_data.fc_temp_x100;
#else
    sensors_data.ambient_temp_x100;
#endif
    return (temp_x100 > 0) ? (uint8_t)(temp_x100 / 100) : 0U;
}

static void save_settings_to_flash(void) {
    fccu_persisted_cfg_t cfg = {
            .fan_mode                   = fan_ctx.mode,
            .fan_manual_on              = fan_ctx.manual_state,
            .fan_manual_duty            = fan_ctx.manual_duty,
            .fan_p_gain                 = fan_ctx.p_gain,
            .fan_i_gain                 = fan_ctx.i_gain,
            .fan_d_gain                 = fan_ctx.d_gain,
            .purge_mode                 = valves_ctx.purge_mode,
            .purge_interval_s           = valves_ctx.purge_interval_s,
            .purge_duration_ms          = valves_ctx.purge_duration_ms,
            .purge_voltage_threshold_mv = valves_ctx.purge_voltage_threshold_mv,
            .purge_voltage_cooldown_s   = valves_ctx.purge_voltage_cooldown_s
    };
    (void)fccu_storage_save(&cfg);
}

static void load_settings_from_flash(void) {
    fccu_persisted_cfg_t cfg;
    if (!fccu_storage_load(&cfg)) {
        return;
    }

    fccu_fan_set_settings(&fan_ctx, cfg.fan_mode, cfg.fan_p_gain, cfg.fan_i_gain, cfg.fan_d_gain);
    fccu_fan_set_manual_duty(&fan_ctx, cfg.fan_manual_on, cfg.fan_manual_duty);
    fccu_fan_resume(&fan_ctx);

    fccu_valves_set_settings(&valves_ctx,
                             cfg.purge_interval_s,
                             cfg.purge_duration_ms,
                             true,
                             cfg.purge_mode,
                             cfg.purge_voltage_threshold_mv,
                             cfg.purge_voltage_cooldown_s);

    LOG_INF("Restored Flash cfg: fan(mode=%u on=%u duty=%u P=%u I=%u D=%u) purge(mode=%u int=%us dur=%ums thr=%umV cd=%us)",
            fan_ctx.mode, fan_ctx.manual_state, fan_ctx.manual_duty,
            fan_ctx.p_gain, fan_ctx.i_gain, fan_ctx.d_gain,
            valves_ctx.purge_mode, valves_ctx.purge_interval_s, valves_ctx.purge_duration_ms,
            valves_ctx.purge_voltage_threshold_mv, valves_ctx.purge_voltage_cooldown_s);
}

static void trigger_purge(uint32_t now_ms) {
    bool is_running = (state_ctx.state == STATE_RUNNING);
    if (fccu_valves_start_purge(&valves_ctx, is_running, now_ms)) {
        /* Nie zerujemy objętości przy purge — zliczamy sumaryczne zużycie od włączenia zasilania */
        fccu_actuators_set_purge_valve(1U);
        fccu_actuators_schedule_purge_close(valves_ctx.purge_duration_ms);
        fccu_can_send_state((uint8_t)state_ctx.state, valves_ctx.main_valve,
                            valves_ctx.purge_valve, fan_ctx.state, fan_ctx.duty);
    }
}

static void handle_event(const fccu_event_t *evt) {
    switch (evt->type) {
        case FCCU_EVT_CAN_SYSTEM_ENABLE:
            fccu_state_set_enable(&state_ctx, evt->data.u8_val, evt->timestamp_ms);
            break;

        case FCCU_EVT_CAN_MAIN_VALVE: {
            fccu_state_notify_can_rx(&state_ctx, evt->timestamp_ms);
            uint8_t prev_main = valves_ctx.main_valve;
            fccu_valves_set_main(&valves_ctx, evt->data.u8_val);
            if (prev_main == 0U && valves_ctx.main_valve == 1U) {
                fccu_valves_sync_purge_timer(&valves_ctx, evt->timestamp_ms);
            }
            if (state_ctx.state == STATE_RUNNING) {
                fccu_actuators_set_main_valve(valves_ctx.main_valve);
                fccu_can_send_state((uint8_t)state_ctx.state, valves_ctx.main_valve,
                                    valves_ctx.purge_valve, fan_ctx.state, fan_ctx.duty);
            }
            break;
        }

        case FCCU_EVT_CAN_MANUAL_PURGE:
            fccu_state_notify_can_rx(&state_ctx, evt->timestamp_ms);
            trigger_purge(evt->timestamp_ms);
            break;

        case FCCU_EVT_CAN_FAN_CMD:
            fccu_state_notify_can_rx(&state_ctx, evt->timestamp_ms);
            fccu_fan_set_manual_duty(&fan_ctx, evt->data.fan_cmd.on, evt->data.fan_cmd.duty);
            save_settings_to_flash();
            if (state_ctx.state == STATE_RUNNING) {
                fccu_actuators_set_fan_duty(fan_ctx.duty);
                fccu_can_send_state((uint8_t)state_ctx.state, valves_ctx.main_valve,
                                    valves_ctx.purge_valve, fan_ctx.state, fan_ctx.duty);
            }
            break;

        case FCCU_EVT_CAN_FAN_SETTINGS:
            fccu_state_notify_can_rx(&state_ctx, evt->timestamp_ms);
            fccu_fan_set_settings(&fan_ctx,
                                  evt->data.fan_settings.mode,
                                  evt->data.fan_settings.p_gain,
                                  evt->data.fan_settings.i_gain,
                                  evt->data.fan_settings.d_gain);
            save_settings_to_flash();
            if (state_ctx.state == STATE_RUNNING) {
                fccu_actuators_set_fan_duty(fan_ctx.duty);
                fccu_can_send_state((uint8_t)state_ctx.state, valves_ctx.main_valve,
                                    valves_ctx.purge_valve, fan_ctx.state, fan_ctx.duty);
            }
            break;

        case FCCU_EVT_CAN_VALVE_SETTINGS:
            fccu_state_notify_can_rx(&state_ctx, evt->timestamp_ms);
            fccu_valves_set_settings(&valves_ctx,
                                     evt->data.valve_settings.interval_s,
                                     evt->data.valve_settings.duration_ms,
                                     (evt->data.valve_settings.has_voltage_cfg != 0U),
                                     evt->data.valve_settings.mode,
                                     evt->data.valve_settings.voltage_threshold_mv,
                                     evt->data.valve_settings.voltage_cooldown_s);
            save_settings_to_flash();
            break;

        case FCCU_EVT_BTN_SINGLE_CLICK:
            if (state_ctx.state == STATE_RUNNING) {
                uint8_t prev_main = valves_ctx.main_valve;
                fccu_valves_toggle_main(&valves_ctx);
                if (prev_main == 0U && valves_ctx.main_valve == 1U) {
                    fccu_valves_sync_purge_timer(&valves_ctx, evt->timestamp_ms);
                }
                fccu_actuators_set_main_valve(valves_ctx.main_valve);
                fccu_can_send_state((uint8_t)state_ctx.state, valves_ctx.main_valve,
                                    valves_ctx.purge_valve, fan_ctx.state, fan_ctx.duty);
            }
            break;

        case FCCU_EVT_BTN_DOUBLE_CLICK:
            trigger_purge(evt->timestamp_ms);
            break;

        case FCCU_EVT_PURGE_TIMEOUT:
            fccu_valves_close_purge(&valves_ctx);
            fccu_actuators_set_purge_valve(0U);
            fccu_can_send_state((uint8_t)state_ctx.state, valves_ctx.main_valve,
                                valves_ctx.purge_valve, fan_ctx.state, fan_ctx.duty);
            break;
    }
}

static void flow_fast_thread_fn(void *p1, void *p2, void *p3) {
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (1) {
        fccu_flow_snapshot_t snap;
        fccu_sensors_get_flow_snapshot(&snap);
        uint32_t flow_x1000 = fccu_flow_calc_x1000_from_period_us(snap.effective_period_us);
        fccu_can_send_flow_fast(flow_x1000);
        k_sleep(K_MSEC(FCCU_FLOW_FAST_PERIOD_MS));
    }
}

K_THREAD_DEFINE(flow_fast_tid, 1024, flow_fast_thread_fn, NULL, NULL, NULL, 6, 0, 100);

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

    if (fccu_storage_init() == 0) {
        load_settings_from_flash();
    }

    fccu_sensors_read_all(&sensors_data);
    uint32_t boot_ms = k_uptime_get_32();
    fccu_state_t boot_state = fccu_state_step(&state_ctx, boot_ms);

    if (boot_state == STATE_RUNNING) {
        fccu_valves_sync_purge_timer(&valves_ctx, boot_ms);
        fccu_fan_resume(&fan_ctx);
        fccu_fan_step(&fan_ctx, true, get_fan_temp_c());
        fccu_actuators_set_fan_duty(fan_ctx.duty);
    } else {
        fccu_actuators_emergency_stop();
    }

    fccu_can_send_state((uint8_t)boot_state, valves_ctx.main_valve,
                        valves_ctx.purge_valve, fan_ctx.state, fan_ctx.duty);

    LOG_INF("FCCU started in state=%u (fan duty=%u%%)", (unsigned int)boot_state, fan_ctx.duty);

    uint32_t last_1s_tick_ms = boot_ms;
    fccu_state_t prev_state = boot_state;
    fccu_event_t evt;

    while (1) {
        fccu_watchdog_feed();

        if (k_msgq_get(&fccu_event_q, &evt, K_MSEC(50)) == 0) {
            handle_event(&evt);
        }

        uint32_t now_ms = k_uptime_get_32();
        fccu_state_t curr_state = fccu_state_step(&state_ctx, now_ms);

        /* 1. Reakcja na zmianę stanu */
        if (prev_state != curr_state) {
            if (curr_state == STATE_RUNNING) {
                fccu_valves_sync_purge_timer(&valves_ctx, now_ms);
                fccu_fan_resume(&fan_ctx);
                fccu_fan_step(&fan_ctx, true, get_fan_temp_c());
                fccu_actuators_set_fan_duty(fan_ctx.duty);
            } else {
                fccu_valves_reset_outputs(&valves_ctx);
                fccu_fan_stop(&fan_ctx);
                fccu_actuators_emergency_stop();
            }
            fccu_can_send_state((uint8_t)curr_state, valves_ctx.main_valve,
                                valves_ctx.purge_valve, fan_ctx.state, fan_ctx.duty);
            prev_state = curr_state;
        }

        /* 2. Wykonanie wyjść zależnie od stanu */
        if (curr_state == STATE_STOPPED || curr_state == STATE_INIT) {
            fccu_valves_reset_outputs(&valves_ctx);
            fccu_fan_stop(&fan_ctx);
            fccu_actuators_emergency_stop();
        } else if (curr_state == STATE_RUNNING) {
            fccu_actuators_set_main_valve(valves_ctx.main_valve);

            int32_t mcu_fc_v_mv = 0;
            bool mcu_fc_v_valid = fccu_can_get_mcu_fc_voltage_mv(&mcu_fc_v_mv, now_ms);

            if (fccu_valves_check_auto_purge(&valves_ctx, true, now_ms, mcu_fc_v_valid, mcu_fc_v_mv)) {
                fccu_actuators_set_purge_valve(1U);
                fccu_actuators_schedule_purge_close(valves_ctx.purge_duration_ms);
                fccu_can_send_state((uint8_t)curr_state, valves_ctx.main_valve,
                                    valves_ctx.purge_valve, fan_ctx.state, fan_ctx.duty);
            }

            fccu_fan_step(&fan_ctx, true, get_fan_temp_c());
            fccu_actuators_set_fan_duty(fan_ctx.duty);
        }

        /* 3. Zadania okresowe co 1 s (czujniki + przepływ + telemetria CAN) */
        if ((now_ms - last_1s_tick_ms) >= 1000U) {
            last_1s_tick_ms = now_ms;

            fccu_sensors_read_all(&sensors_data);

#if FCCU_USE_FC_TEMP
            LOG_INF("FC temp: %d mV -> %d.%02d C",
                    sensors_data.fc_temp_mv,
                    sensors_data.fc_temp_x100 / 100,
                    (sensors_data.fc_temp_x100 < 0 ? -sensors_data.fc_temp_x100 : sensors_data.fc_temp_x100) % 100);
#endif

            fccu_flow_snapshot_t flow_snap;
            fccu_sensors_get_flow_snapshot(&flow_snap);
            fccu_flow_update_fast(&flow_ctx, flow_snap.effective_period_us, flow_snap.total_pulses);

            fccu_telemetry_t tel = {
                    .state                 = curr_state,
                    .main_valve            = valves_ctx.main_valve,
                    .purge_valve           = valves_ctx.purge_valve,
                    .fan_state             = fan_ctx.state,
                    .fan_duty              = fan_ctx.duty,
                    .total_volume_x1000    = flow_ctx.total_volume_x1000,
                    .lp_pressure_x100      = (sensors_data.lp_pressure_mv > 0) ? (uint16_t)sensors_data.lp_pressure_mv : 0U,
                    .hp_pressure_x100      = (sensors_data.hp_pressure_mv > 0) ? (uint16_t)sensors_data.hp_pressure_mv : 0U,
                    .fc_voltage_x100       = (int16_t)sensors_data.fc_voltage_mv,
                    .fc_current_x100       = (int16_t)sensors_data.fc_current_mv,
                    .fc_temp_x100          = sensors_data.fc_temp_x100,
                    .ambient_temp_x100     = sensors_data.ambient_temp_x100,
                    .ambient_humidity_x100 = sensors_data.ambient_humidity_x100,
                    .ambient_pressure_x100 = sensors_data.ambient_pressure_x100
            };
            fccu_can_send_telemetry(&tel);
        }
    }

    return 0;
}