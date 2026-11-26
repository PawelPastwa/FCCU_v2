#include "bsp/fccu_sensors.h"
#include "fccu_config.h"

#include <stdbool.h>
#include <string.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/sensor.h>

#if FCCU_USE_EXTRA_SENSORS || FCCU_USE_FC_TEMP
#include <zephyr/drivers/adc.h>
#endif

LOG_MODULE_REGISTER(fccu_sensors, LOG_LEVEL_INF);

static const struct gpio_dt_spec flow_sensor_pin = GPIO_DT_SPEC_GET(DT_NODELABEL(flow_meter_0), gpios);
static struct gpio_callback flow_cb_data;
static atomic_t pulse_count = ATOMIC_INIT(0);

static uint32_t flow_last_cyc = 0U;
static uint32_t flow_last_ms = 0U;
static uint32_t flow_last_period_us = 0U;
static uint64_t flow_total_pulses = 0ULL;
static bool     flow_has_prev = false;

#if FCCU_USE_BME280
static const struct device *const bme280_dev_76 = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(bme280_76));
static const struct device *const bme280_dev_77 = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(bme280_77));
static const struct device *bme280_dev = NULL;
#endif

#if FCCU_USE_EXTRA_SENSORS
static const struct adc_dt_spec adc_lp_pressure = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);
static const struct adc_dt_spec adc_fc_voltage  = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 1);
static const struct adc_dt_spec adc_sc_voltage  = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 2);
static const struct adc_dt_spec adc_fc_current  = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 4);
static const struct adc_dt_spec adc_hp_pressure = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 7);
#endif /* FCCU_USE_EXTRA_SENSORS */

#if FCCU_USE_EXTRA_SENSORS || FCCU_USE_FC_TEMP
static int setup_adc_channel(const struct adc_dt_spec *spec) {
    if (!adc_is_ready_dt(spec)) {
        return -ENODEV;
    }
    return adc_channel_setup_dt(spec);
}

static int32_t read_adc_mv(const struct adc_dt_spec *spec) {
    int16_t raw = 0;
    struct adc_sequence seq = {
            .buffer      = &raw,
            .buffer_size = sizeof(raw)
    };

    if (!adc_is_ready_dt(spec)) {
        return 0;
    }

    (void)adc_sequence_init_dt(spec, &seq);
    if (adc_read_dt(spec, &seq) < 0) {
        return 0;
    }

    int32_t val_mv = (int32_t)raw;
    (void)adc_raw_to_millivolts_dt(spec, &val_mv);
    return val_mv;
}
#endif /* FCCU_USE_EXTRA_SENSORS || FCCU_USE_FC_TEMP */

#if FCCU_USE_FC_TEMP
static const struct adc_dt_spec adc_fc_temp = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 3);

#if !FCCU_FC_TEMP_TYPE_PT1000
static float approx_ln(float x) {
    int k = 0;
    while (x > 1.5f) {
        x *= 0.5f;
        k++;
    }
    while (x < 0.75f) {
        x *= 2.0f;
        k--;
    }
    float y  = (x - 1.0f) / (x + 1.0f);
    float y2 = y * y;
    float s  = y * (1.0f + y2 * ((1.0f / 3.0f) + y2 * ((1.0f / 5.0f) + y2 * (1.0f / 7.0f))));
    return 2.0f * s + (float)k * 0.69314718f;
}
#endif

static int16_t fc_temp_mv_to_x100(int32_t adc_mv) {
    if (adc_mv <= 0) {
        return 0;
    }

    float v_sens = ((float)adc_mv * (float)FCCU_FC_TEMP_OPAMP_GAIN_DEN) /
                   (float)FCCU_FC_TEMP_OPAMP_GAIN_NUM;
    if (v_sens <= 1.0f || v_sens >= (float)(FCCU_FC_TEMP_VCC_MV - 1)) {
        return 0;
    }

    float r_th = (float)FCCU_FC_TEMP_PULLUP_OHM * v_sens /
                 ((float)FCCU_FC_TEMP_VCC_MV - v_sens);
    float temp_c;

#if FCCU_FC_TEMP_TYPE_PT1000
    temp_c = (r_th - (float)FCCU_FC_TEMP_R25_OHM) / 3.851f;
#else
    float inv_t = (1.0f / 298.15f) +
                  (approx_ln(r_th / (float)FCCU_FC_TEMP_R25_OHM) / (float)FCCU_FC_TEMP_NTC_BETA_K);
    temp_c = (1.0f / inv_t) - 273.15f;
#endif

    if (temp_c < -50.0f) {
        temp_c = -50.0f;
    } else if (temp_c > 200.0f) {
        temp_c = 200.0f;
    }
    return (int16_t)(temp_c * 100.0f);
}
#endif /* FCCU_USE_FC_TEMP */

static void flow_sensor_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);

    unsigned int key = irq_lock();
    uint32_t now_cyc = k_cycle_get_32();
    uint32_t now_ms  = k_uptime_get_32();

    if (flow_has_prev) {
        uint32_t dt_ms = now_ms - flow_last_ms;
        if (dt_ms < 4000U) {
            uint32_t dt_us = k_cyc_to_us_floor32(now_cyc - flow_last_cyc);
            if (dt_us < FCCU_FLOW_DEBOUNCE_US) {
                irq_unlock(key);
                return;
            }
            flow_last_period_us = dt_us;
        } else {
            flow_last_period_us = 0U;
        }
    }

    flow_last_cyc = now_cyc;
    flow_last_ms  = now_ms;
    flow_has_prev = true;
    flow_total_pulses++;
    atomic_inc(&pulse_count);
    irq_unlock(key);
}

int fccu_sensors_init(void) {
    if (gpio_is_ready_dt(&flow_sensor_pin)) {
        gpio_pin_configure_dt(&flow_sensor_pin, GPIO_INPUT);
        gpio_pin_interrupt_configure_dt(&flow_sensor_pin, GPIO_INT_EDGE_RISING);
        gpio_init_callback(&flow_cb_data, flow_sensor_isr, BIT(flow_sensor_pin.pin));
        gpio_add_callback(flow_sensor_pin.port, &flow_cb_data);
    }

#if FCCU_USE_EXTRA_SENSORS
    if (setup_adc_channel(&adc_lp_pressure) != 0) {
        LOG_WRN("ADC lp_pressure init failed");
    }
    if (setup_adc_channel(&adc_fc_voltage) != 0) {
        LOG_WRN("ADC fc_voltage init failed");
    }
    if (setup_adc_channel(&adc_sc_voltage) != 0) {
        LOG_WRN("ADC sc_voltage init failed");
    }
    if (setup_adc_channel(&adc_fc_current) != 0) {
        LOG_WRN("ADS1015 fc_current init failed");
    }
    if (setup_adc_channel(&adc_hp_pressure) != 0) {
        LOG_WRN("ADS1015 hp_pressure init failed");
    }
#endif

#if FCCU_USE_BME280
    if (bme280_dev_76 != NULL && device_is_ready(bme280_dev_76)) {
        bme280_dev = bme280_dev_76;
        LOG_INF("BME/BMP280 ready at 0x76");
    } else if (bme280_dev_77 != NULL && device_is_ready(bme280_dev_77)) {
        bme280_dev = bme280_dev_77;
        LOG_INF("BME/BMP280 ready at 0x77");
    } else {
        bme280_dev = NULL;
        LOG_WRN("BME/BMP280 not ready at 0x76 or 0x77");
    }
#endif

#if FCCU_USE_FC_TEMP
    if (setup_adc_channel(&adc_fc_temp) != 0) {
        LOG_WRN("ADC fc_temp (GPIO10) init failed");
    }
#endif

    return 0;
}

void fccu_sensors_read_all(fccu_sensors_data_t *out) {
    if (out == NULL) {
        return;
    }

    memset(out, 0, sizeof(*out));

#if FCCU_USE_FC_TEMP
    out->fc_temp_mv   = read_adc_mv(&adc_fc_temp);
    out->fc_temp_x100 = fc_temp_mv_to_x100(out->fc_temp_mv);
#endif

#if FCCU_USE_EXTRA_SENSORS
    out->lp_pressure_mv = read_adc_mv(&adc_lp_pressure);
    out->fc_voltage_mv  = read_adc_mv(&adc_fc_voltage);
    out->sc_voltage_mv  = read_adc_mv(&adc_sc_voltage);
    out->fc_current_mv  = read_adc_mv(&adc_fc_current);
    out->hp_pressure_mv = read_adc_mv(&adc_hp_pressure);
#endif /* FCCU_USE_EXTRA_SENSORS */

#if FCCU_USE_BME280
    if (device_is_ready(bme280_dev) && sensor_sample_fetch(bme280_dev) == 0) {
        struct sensor_value temp, press, hum;
        (void)sensor_channel_get(bme280_dev, SENSOR_CHAN_AMBIENT_TEMP, &temp);
        (void)sensor_channel_get(bme280_dev, SENSOR_CHAN_PRESS, &press);
        (void)sensor_channel_get(bme280_dev, SENSOR_CHAN_HUMIDITY, &hum);

        out->ambient_temp_x100     = (int16_t)(temp.val1 * 100 + temp.val2 / 10000);
        out->ambient_humidity_x100 = (uint16_t)(hum.val1 * 100 + hum.val2 / 10000);
        out->ambient_pressure_x100 = (uint16_t)(press.val1 * 1000 + press.val2 / 1000);
    }
#endif /* FCCU_USE_BME280 */
}

uint32_t fccu_sensors_consume_flow_pulses(void) {
    return (uint32_t)atomic_set(&pulse_count, 0);
}

void fccu_sensors_get_flow_snapshot(fccu_flow_snapshot_t *out) {
    if (out == NULL) {
        return;
    }

    unsigned int key = irq_lock();
    uint32_t now_cyc        = k_cycle_get_32();
    uint32_t now_ms         = k_uptime_get_32();
    bool     has_prev       = flow_has_prev;
    uint32_t last_cyc       = flow_last_cyc;
    uint32_t last_ms        = flow_last_ms;
    uint32_t last_period_us = flow_last_period_us;
    uint64_t total          = flow_total_pulses;
    irq_unlock(key);

    uint32_t effective_period_us = 0U;
    if (has_prev && last_period_us > 0U) {
        uint32_t age_ms = now_ms - last_ms;
        if (age_ms < FCCU_FLOW_TIMEOUT_MS && age_ms < 4000U) {
            uint32_t age_us = k_cyc_to_us_floor32(now_cyc - last_cyc);
            effective_period_us = (age_us > last_period_us) ? age_us : last_period_us;
        }
    }

    out->effective_period_us = effective_period_us;
    out->total_pulses        = total;
}

void fccu_sensors_reset_flow_pulses(void) {
    unsigned int key = irq_lock();
    flow_last_period_us = 0U;
    flow_total_pulses   = 0ULL;
    flow_has_prev       = false;
    (void)atomic_set(&pulse_count, 0);
    irq_unlock(key);
}