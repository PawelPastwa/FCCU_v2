#include "comm/fccu_can.h"
#include "comm/dbc/hydrogreen_can_definitions.h"
#include "fccu_config.h"
#include "fccu_events.h"
#include "system_status.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/drivers/can.h>
#include <zephyr/logging/log.h>
#include <zephyr/irq.h>

LOG_MODULE_REGISTER(fccu_can, LOG_LEVEL_INF);

static const struct device *const can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));
static atomic_t can_tx_acked = ATOMIC_INIT(0);
static int32_t  mcu_fc_voltage_mv    = 0;
static uint32_t mcu_fc_voltage_ts_ms = 0U;
static bool     mcu_fc_voltage_seen  = false;

static void can_tx_cb(const struct device *dev, int error, void *user_data) {
    ARG_UNUSED(dev);
    ARG_UNUSED(user_data);

    if (error == 0) {
        atomic_set(&can_tx_acked, 1);
    }
}

static void can_rx_callback(const struct device *dev, struct can_frame *frame, void *user_data) {
    ARG_UNUSED(dev);
    ARG_UNUSED(user_data);

    fccu_event_t evt = {
            .timestamp_ms = k_uptime_get_32()
    };

    switch (frame->id) {
        case HYDROGREEN_CAN_DEFINITIONS_FCCU_SYSTEM_ENABLE_FRAME_ID: {
            struct hydrogreen_can_definitions_fccu_system_enable_t msg;
            if (hydrogreen_can_definitions_fccu_system_enable_unpack(&msg, frame->data, frame->dlc) == 0) {
                evt.type = FCCU_EVT_CAN_SYSTEM_ENABLE;
                evt.data.u8_val = msg.cmd_system_enable;
                (void)fccu_event_post(&evt);
            }
            break;
        }

        case HYDROGREEN_CAN_DEFINITIONS_FCCU_MAIN_VALVE_COMMAND_FRAME_ID: {
            struct hydrogreen_can_definitions_fccu_main_valve_command_t msg;
            if (hydrogreen_can_definitions_fccu_main_valve_command_unpack(&msg, frame->data, frame->dlc) == 0) {
                evt.type = FCCU_EVT_CAN_MAIN_VALVE;
                evt.data.u8_val = msg.cmd_main_valve;
                (void)fccu_event_post(&evt);
            }
            break;
        }

        case HYDROGREEN_CAN_DEFINITIONS_FCCU_MANUAL_PURGE_FRAME_ID: {
            struct hydrogreen_can_definitions_fccu_manual_purge_t msg;
            if (hydrogreen_can_definitions_fccu_manual_purge_unpack(&msg, frame->data, frame->dlc) == 0 &&
                msg.cmd_manual_purge == HYDROGREEN_CAN_DEFINITIONS_FCCU_MANUAL_PURGE_CMD_MANUAL_PURGE_TRIGGER_PURGE_CHOICE) {
                evt.type = FCCU_EVT_CAN_MANUAL_PURGE;
                (void)fccu_event_post(&evt);
            }
            break;
        }

        case HYDROGREEN_CAN_DEFINITIONS_FCCU_FAN_FRAME_ID: {
            struct hydrogreen_can_definitions_fccu_fan_t msg;
            if (hydrogreen_can_definitions_fccu_fan_unpack(&msg, frame->data, frame->dlc) == 0) {
                evt.type = FCCU_EVT_CAN_FAN_CMD;
                evt.data.fan_cmd.on = msg.cmd_fan_on;
                evt.data.fan_cmd.duty = msg.cmd_fan_duty;
                (void)fccu_event_post(&evt);
            } else if (frame->dlc == 1U) {
                /* Kompatybilność wsteczna ze starą 1-bajtową ramką 0x513 */
                evt.type = FCCU_EVT_CAN_FAN_CMD;
                evt.data.fan_cmd.on = (uint8_t)(frame->data[0] & 0x01U);
                evt.data.fan_cmd.duty = (evt.data.fan_cmd.on != 0U) ? FCCU_FAN_DUTY_MAX_PCT : FCCU_FAN_DUTY_MIN_PCT;
                (void)fccu_event_post(&evt);
            }
            break;
        }

        case HYDROGREEN_CAN_DEFINITIONS_FCCU_FAN_SETTINGS_FRAME_ID: {
            struct hydrogreen_can_definitions_fccu_fan_settings_t msg;
            if (hydrogreen_can_definitions_fccu_fan_settings_unpack(&msg, frame->data, frame->dlc) == 0) {
                evt.type = FCCU_EVT_CAN_FAN_SETTINGS;
                evt.data.fan_settings.mode   = msg.fan_mode;
                evt.data.fan_settings.p_gain = msg.p_gain;
                evt.data.fan_settings.i_gain = msg.i_gain;
                evt.data.fan_settings.d_gain = msg.d_gain;
                (void)fccu_event_post(&evt);
            }
            break;
        }

        case HYDROGREEN_CAN_DEFINITIONS_FCCU_VALVE_SETTINGS_FRAME_ID: {
            struct hydrogreen_can_definitions_fccu_valve_settings_t msg;
            if (hydrogreen_can_definitions_fccu_valve_settings_unpack(&msg, frame->data, frame->dlc) == 0) {
                evt.type = FCCU_EVT_CAN_VALVE_SETTINGS;
                evt.data.valve_settings.interval_s           = (msg.purge_interval_s > 0) ? (uint16_t)msg.purge_interval_s : 0U;
                evt.data.valve_settings.duration_ms          = msg.purge_duration_ms;
                evt.data.valve_settings.has_voltage_cfg      = 1U;
                evt.data.valve_settings.mode                 = msg.purge_mode;
                evt.data.valve_settings.voltage_threshold_mv = msg.purge_voltage_threshold_mv;
                evt.data.valve_settings.voltage_cooldown_s   = msg.purge_voltage_cooldown_s;
                (void)fccu_event_post(&evt);
            } else if (frame->dlc == 4U) {
                /* Kompatybilność wsteczna ze starą 4-bajtową ramką 0x521 */
                int16_t  interval_raw = (int16_t)((uint16_t)frame->data[0] | ((uint16_t)frame->data[1] << 8));
                uint16_t duration_raw = (uint16_t)((uint16_t)frame->data[2] | ((uint16_t)frame->data[3] << 8));

                evt.type = FCCU_EVT_CAN_VALVE_SETTINGS;
                evt.data.valve_settings.interval_s      = (interval_raw > 0) ? (uint16_t)interval_raw : 0U;
                evt.data.valve_settings.duration_ms     = duration_raw;
                evt.data.valve_settings.has_voltage_cfg = 0U;
                (void)fccu_event_post(&evt);
            }
            break;
        }

        case HYDROGREEN_CAN_DEFINITIONS_MCU_ANALOG_FUEL_CELL_FRAME_ID: {
            struct hydrogreen_can_definitions_mcu_analog_fuel_cell_t msg;
            if (hydrogreen_can_definitions_mcu_analog_fuel_cell_unpack(&msg, frame->data, frame->dlc) == 0) {
                unsigned int key = irq_lock();
                mcu_fc_voltage_mv    = (int32_t)msg.fuel_cell_output_voltage * 2; //(int32_t)msg.fuel_cell_output_voltage;
                mcu_fc_voltage_ts_ms = evt.timestamp_ms;
                mcu_fc_voltage_seen  = true;
                irq_unlock(key);
            }
            break;
        }

        default:
            break;
    }
}

int fccu_can_init(void) {
    if (!device_is_ready(can_dev)) {
        LOG_ERR("CAN device not ready");
        return -ENODEV;
    }

    static const uint32_t rx_ids[] = {
            HYDROGREEN_CAN_DEFINITIONS_FCCU_SYSTEM_ENABLE_FRAME_ID,
            HYDROGREEN_CAN_DEFINITIONS_FCCU_MAIN_VALVE_COMMAND_FRAME_ID,
            HYDROGREEN_CAN_DEFINITIONS_FCCU_MANUAL_PURGE_FRAME_ID,
            HYDROGREEN_CAN_DEFINITIONS_FCCU_FAN_FRAME_ID,
            HYDROGREEN_CAN_DEFINITIONS_FCCU_FAN_SETTINGS_FRAME_ID,
            HYDROGREEN_CAN_DEFINITIONS_FCCU_VALVE_SETTINGS_FRAME_ID,
            HYDROGREEN_CAN_DEFINITIONS_MCU_ANALOG_FUEL_CELL_FRAME_ID
    };

    for (size_t i = 0; i < ARRAY_SIZE(rx_ids); i++) {
        const struct can_filter filter = {
                .flags = CAN_FILTER_IDE,
                .id    = rx_ids[i],
                .mask  = CAN_EXT_ID_MASK
        };

        int ret = can_add_rx_filter(can_dev, can_rx_callback, NULL, &filter);
        if (ret < 0) {
            LOG_ERR("Failed to add CAN RX filter 0x%03x: %d", rx_ids[i], ret);
            return ret;
        }
    }

    int ret = can_start(can_dev);
    if (ret != 0) {
        LOG_ERR("Failed to start CAN: %d", ret);
        return ret;
    }

    return 0;
}

bool fccu_can_get_mcu_fc_voltage_mv(int32_t *out_mv, uint32_t now_ms) {
    if (out_mv == NULL) {
        return false;
    }

    unsigned int key = irq_lock();
    bool     seen = mcu_fc_voltage_seen;
    uint32_t ts   = mcu_fc_voltage_ts_ms;
    int32_t  mv   = mcu_fc_voltage_mv;
    irq_unlock(key);

    if (!seen || (now_ms - ts) > FCCU_MCU_VOLTAGE_TIMEOUT_MS) {
        return false;
    }

    *out_mv = mv;
    return true;
}

void fccu_can_send_state(uint8_t state, uint8_t main_valve, uint8_t purge_valve, uint8_t fan_state, uint8_t fan_duty) {
    if (!device_is_ready(can_dev)) {
        return;
    }

    struct hydrogreen_can_definitions_fccu_state_t state_msg = {
            .state       = state,
            .main_valve  = main_valve,
            .purge_valve = purge_valve,
            .fan_state   = fan_state,
            .fan_duty    = fan_duty
    };

    struct can_frame state_frame = {
            .id    = HYDROGREEN_CAN_DEFINITIONS_FCCU_STATE_FRAME_ID,
            .dlc   = HYDROGREEN_CAN_DEFINITIONS_FCCU_STATE_LENGTH,
            .flags = CAN_FRAME_IDE
    };

    if (hydrogreen_can_definitions_fccu_state_pack(state_frame.data, &state_msg, sizeof(state_frame.data)) > 0) {
        (void)can_send(can_dev, &state_frame, K_MSEC(10), can_tx_cb, NULL);
    }
}

void fccu_can_send_telemetry(const fccu_telemetry_t *tel) {
    if (tel == NULL) {
        return;
    }

    if (!device_is_ready(can_dev)) {
        set_system_status(STATUS_CAN_ERROR);
        return;
    }

    enum can_state state;
    struct can_bus_err_cnt err_cnt;
    if (can_get_state(can_dev, &state, &err_cnt) == 0) {
        if (state == CAN_STATE_BUS_OFF) {
            LOG_WRN("CAN BUS_OFF detected, attempting recovery...");
            set_system_status(STATUS_CAN_ERROR);
            (void)can_recover(can_dev, K_MSEC(100));
            return;
        }
        if (state == CAN_STATE_STOPPED) {
            set_system_status(STATUS_CAN_ERROR);
            return;
        }
    }

    int prev_acked = (int)atomic_set(&can_tx_acked, 0);
    int tx_err = -EIO;

    /* 1. Ramka FCCU_STATE (0x500, 2 bajty) */
    struct hydrogreen_can_definitions_fccu_state_t state_msg = {
            .state       = (uint8_t)tel->state,
            .main_valve  = tel->main_valve,
            .purge_valve = tel->purge_valve,
            .fan_state   = tel->fan_state,
            .fan_duty    = tel->fan_duty
    };

    struct can_frame state_frame = {
            .id    = HYDROGREEN_CAN_DEFINITIONS_FCCU_STATE_FRAME_ID,
            .dlc   = HYDROGREEN_CAN_DEFINITIONS_FCCU_STATE_LENGTH,
            .flags = CAN_FRAME_IDE
    };

    if (hydrogreen_can_definitions_fccu_state_pack(state_frame.data, &state_msg, sizeof(state_frame.data)) > 0) {
        tx_err = can_send(can_dev, &state_frame, K_MSEC(10), can_tx_cb, NULL);
    }

    /* 2. Ramka FCCU_HYDROGEN (0x502, 6 bajtów: LP, HP, Leakage) */
    struct hydrogreen_can_definitions_fccu_hydrogen_t h2_msg = {
            .h2_lp_pressure  = tel->lp_pressure_x100,
            .h2_hp_pressure  = tel->hp_pressure_x100,
            .leakage_voltage = 0
    };

    struct can_frame h2_frame = {
            .id    = HYDROGREEN_CAN_DEFINITIONS_FCCU_HYDROGEN_FRAME_ID,
            .dlc   = HYDROGREEN_CAN_DEFINITIONS_FCCU_HYDROGEN_LENGTH,
            .flags = CAN_FRAME_IDE
    };

    if (hydrogreen_can_definitions_fccu_hydrogen_pack(h2_frame.data, &h2_msg, sizeof(h2_frame.data)) > 0) {
        int ret = can_send(can_dev, &h2_frame, K_MSEC(10), can_tx_cb, NULL);
        if (ret != 0) {
            tx_err = ret;
        }
    }

    /* 3. Ramka FCCU_HYDROGEN_TOTAL (0x506, 4 bajty — skala 0.001 Ln) */
    struct hydrogreen_can_definitions_fccu_hydrogen_total_t h2_total_msg = {
            .h2_total_volume = tel->total_volume_x1000
    };

    struct can_frame h2_total_frame = {
            .id    = HYDROGREEN_CAN_DEFINITIONS_FCCU_HYDROGEN_TOTAL_FRAME_ID,
            .dlc   = HYDROGREEN_CAN_DEFINITIONS_FCCU_HYDROGEN_TOTAL_LENGTH,
            .flags = CAN_FRAME_IDE
    };

    if (hydrogreen_can_definitions_fccu_hydrogen_total_pack(h2_total_frame.data, &h2_total_msg, sizeof(h2_total_frame.data)) > 0) {
        int ret = can_send(can_dev, &h2_total_frame, K_MSEC(10), can_tx_cb, NULL);
        if (ret != 0) {
            tx_err = ret;
        }
    }

#if FCCU_USE_EXTRA_SENSORS
    /* 4. Ramka FCCU_POWER (0x503, 8 bajtów) */
    struct hydrogreen_can_definitions_fccu_power_t pwr_msg = {
            .fc_voltage   = tel->fc_voltage_x100,
            .fc_current   = tel->fc_current_x100,
            .dcdc_voltage = 0,
            .dcdc_current = 0
    };

    struct can_frame pwr_frame = {
            .id    = HYDROGREEN_CAN_DEFINITIONS_FCCU_POWER_FRAME_ID,
            .dlc   = HYDROGREEN_CAN_DEFINITIONS_FCCU_POWER_LENGTH,
            .flags = CAN_FRAME_IDE
    };

    if (hydrogreen_can_definitions_fccu_power_pack(pwr_frame.data, &pwr_msg, sizeof(pwr_frame.data)) > 0) {
        int ret = can_send(can_dev, &pwr_frame, K_MSEC(10), can_tx_cb, NULL);
        if (ret != 0) {
            tx_err = ret;
        }
    }
#endif

#if FCCU_USE_BME280 || FCCU_USE_FC_TEMP
    /* 5. Ramka FCCU_ENVIRONMENT (0x504, 8 bajtów) */
    struct hydrogreen_can_definitions_fccu_environment_t env_msg = {
            .fc_temp_c        = tel->fc_temp_x100,
            .ambient_temp_c   = tel->ambient_temp_x100,
            .ambient_humidity = tel->ambient_humidity_x100,
            .ambient_pressure = tel->ambient_pressure_x100
    };

    struct can_frame env_frame = {
            .id    = HYDROGREEN_CAN_DEFINITIONS_FCCU_ENVIRONMENT_FRAME_ID,
            .dlc   = HYDROGREEN_CAN_DEFINITIONS_FCCU_ENVIRONMENT_LENGTH,
            .flags = CAN_FRAME_IDE
    };

    if (hydrogreen_can_definitions_fccu_environment_pack(env_frame.data, &env_msg, sizeof(env_frame.data)) > 0) {
        int ret = can_send(can_dev, &env_frame, K_MSEC(10), can_tx_cb, NULL);
        if (ret != 0) {
            tx_err = ret;
        }
    }
#endif

    if (tx_err == 0 && prev_acked == 1) {
        set_system_status(STATUS_CAN_OK);
    } else {
        set_system_status(STATUS_CAN_ERROR);
    }
}

void fccu_can_send_flow_fast(uint32_t flow_x1000) {
    if (!device_is_ready(can_dev)) {
        return;
    }

    struct hydrogreen_can_definitions_fccu_hydrogen_flow_t msg = {
            .h2_flow = flow_x1000
    };

    struct can_frame frame = {
            .id    = HYDROGREEN_CAN_DEFINITIONS_FCCU_HYDROGEN_FLOW_FRAME_ID,
            .dlc   = HYDROGREEN_CAN_DEFINITIONS_FCCU_HYDROGEN_FLOW_LENGTH,
            .flags = CAN_FRAME_IDE
    };

    if (hydrogreen_can_definitions_fccu_hydrogen_flow_pack(frame.data, &msg, sizeof(frame.data)) > 0) {
        (void)can_send(can_dev, &frame, K_NO_WAIT, NULL, NULL);
    }
}