#include "comm/fccu_can.h"
#include "comm/dbc/hydrogreen_can_definitions.h"
#include "fccu_config.h"
#include "fccu_events.h"
#include "system_status.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/can.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(fccu_can, LOG_LEVEL_INF);

static const struct device *const can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));

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
                evt.data.u8_val = msg.cmd_fan_on;
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
                evt.data.valve_settings.interval_s  = (msg.purge_interval_s > 0) ? (uint16_t)msg.purge_interval_s : 0U;
                evt.data.valve_settings.duration_ms = msg.purge_duration_ms;
                (void)fccu_event_post(&evt);
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
            HYDROGREEN_CAN_DEFINITIONS_FCCU_VALVE_SETTINGS_FRAME_ID
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
    set_system_status(STATUS_CAN_OK);
    return 0;
}

void fccu_can_send_telemetry(const fccu_telemetry_t *tel) {
    if (tel == NULL || !device_is_ready(can_dev)) {
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
        } else if (state == CAN_STATE_STOPPED || state == CAN_STATE_ERROR_PASSIVE) {
            set_system_status(STATUS_CAN_ERROR);
        } else {
            set_system_status(STATUS_CAN_OK);
        }
    }

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
        (void)can_send(can_dev, &state_frame, K_MSEC(10), NULL, NULL);
    }

    /* 2. Ramka FCCU_HYDROGEN (0x502, 8 bajtów) */
    struct hydrogreen_can_definitions_fccu_hydrogen_t h2_msg = {
            .h2_flow         = tel->flow_x100,
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
        (void)can_send(can_dev, &h2_frame, K_MSEC(10), NULL, NULL);
    }

#if FCCU_USE_EXTRA_SENSORS
    /* 3. Ramka FCCU_POWER (0x503, 8 bajtów) */
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
        (void)can_send(can_dev, &pwr_frame, K_MSEC(10), NULL, NULL);
    }

    /* 4. Ramka FCCU_ENVIRONMENT (0x504, 8 bajtów) */
    struct hydrogreen_can_definitions_fccu_environment_t env_msg = {
        .fc_temp_c        = tel->ambient_temp_x100,
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
        (void)can_send(can_dev, &env_frame, K_MSEC(10), NULL, NULL);
    }
#endif
}