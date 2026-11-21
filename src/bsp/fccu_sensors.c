#include "bsp/fccu_sensors.h"
#include "fccu_config.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#if FCCU_USE_EXTRA_SENSORS
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/sensor.h>
#endif

LOG_MODULE_REGISTER(fccu_sensors, LOG_LEVEL_INF);

static const struct gpio_dt_spec flow_sensor_pin = GPIO_DT_SPEC_GET(DT_NODELABEL(flow_meter_0), gpios);
static struct gpio_callback flow_cb_data;
static atomic_t pulse_count = ATOMIC_INIT(0);

#if FCCU_USE_EXTRA_SENSORS
static const struct device *const bme280_dev    = DEVICE_DT_GET(DT_NODELABEL(bme280_76));
static const struct adc_dt_spec adc_lp_pressure = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);
static const struct adc_dt_spec adc_fc_voltage  = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 1);
static const struct adc_dt_spec adc_sc_voltage  = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 2);
static const struct adc_dt_spec adc_fc_current  = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 4);
static const struct adc_dt_spec adc_hp_pressure = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 7);

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
#endif /* FCCU_USE_EXTRA_SENSORS */

static void flow_sensor_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);

    static uint32_t last_interrupt_time = 0U;
    uint32_t now = k_uptime_get_32();

    if ((now - last_interrupt_time) > 15U) {
        atomic_inc(&pulse_count);
        last_interrupt_time = now;
    }

    //W flow_sensor_isr() jest debounce > 15U (15 ms), co oznacza maksymalną mierzalną częstotliwość ok. 62 Hz. Przy FCCU_FLOW_K_FACTOR = 1000 daje to maks. ~3,7 Ln/min —  czy przepływomierz nie generuje wyższej częstotliwości impulsów (jeśli tak, zmniejsz debounce np. do 2U lub 5U).

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
    if (!device_is_ready(bme280_dev)) {
        LOG_WRN("BME280 (0x76) not ready");
    }
#endif

    return 0;
}

void fccu_sensors_read_all(fccu_sensors_data_t *out) {
    if (out == NULL) {
        return;
    }

    memset(out, 0, sizeof(*out));

#if FCCU_USE_EXTRA_SENSORS
    out->lp_pressure_mv = read_adc_mv(&adc_lp_pressure);
    out->fc_voltage_mv  = read_adc_mv(&adc_fc_voltage);
    out->sc_voltage_mv  = read_adc_mv(&adc_sc_voltage);
    out->fc_current_mv  = read_adc_mv(&adc_fc_current);
    out->hp_pressure_mv = read_adc_mv(&adc_hp_pressure);

    if (device_is_ready(bme280_dev) && sensor_sample_fetch(bme280_dev) == 0) {
        struct sensor_value temp, press, hum;
        (void)sensor_channel_get(bme280_dev, SENSOR_CHAN_AMBIENT_TEMP, &temp);
        (void)sensor_channel_get(bme280_dev, SENSOR_CHAN_PRESS, &press);
        (void)sensor_channel_get(bme280_dev, SENSOR_CHAN_HUMIDITY, &hum);

        out->ambient_temp_x100     = (int16_t)(temp.val1 * 100 + temp.val2 / 10000);
        out->ambient_humidity_x100 = (uint16_t)(hum.val1 * 100 + hum.val2 / 10000);
        out->ambient_pressure_x100 = (uint16_t)(press.val1 * 1000 + press.val2 / 1000);
    }
#endif
}

uint32_t fccu_sensors_consume_flow_pulses(void) {
    return (uint32_t)atomic_set(&pulse_count, 0);
}