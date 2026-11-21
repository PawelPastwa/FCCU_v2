#include "fccu_actuators.h"
#include "fccu_events.h"
#include "fccu_fan.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>

static const struct gpio_dt_spec main_valve_pin  = GPIO_DT_SPEC_GET(DT_NODELABEL(main_valve0), gpios);
static const struct gpio_dt_spec purge_valve_pin = GPIO_DT_SPEC_GET(DT_NODELABEL(purge_valve0), gpios);
static const struct pwm_dt_spec  fan_pwm         = PWM_DT_SPEC_GET(DT_NODELABEL(fan_pwm0));

static struct k_work_delayable purge_close_work;

static void purge_close_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    gpio_pin_set_dt(&purge_valve_pin, 0);

    fccu_event_t evt = {
            .type         = FCCU_EVT_PURGE_TIMEOUT,
            .timestamp_ms = k_uptime_get_32()
    };
    (void)fccu_event_post(&evt);
}

int fccu_actuators_init(void) {
    if (!gpio_is_ready_dt(&main_valve_pin) ||
        !gpio_is_ready_dt(&purge_valve_pin) ||
        !pwm_is_ready_dt(&fan_pwm)) {
        return -ENODEV;
    }

    gpio_pin_configure_dt(&main_valve_pin, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&purge_valve_pin, GPIO_OUTPUT_INACTIVE);
    k_work_init_delayable(&purge_close_work, purge_close_work_handler);

    fccu_actuators_set_fan_duty(0U);
    return 0;
}

void fccu_actuators_set_main_valve(uint8_t state) {
    gpio_pin_set_dt(&main_valve_pin, state ? 1 : 0);
}

void fccu_actuators_set_purge_valve(uint8_t state) {
    gpio_pin_set_dt(&purge_valve_pin, state ? 1 : 0);
}

void fccu_actuators_schedule_purge_close(uint16_t duration_ms) {
    k_work_reschedule(&purge_close_work, K_MSEC(duration_ms));
}

void fccu_actuators_cancel_purge_close(void) {
    k_work_cancel_delayable(&purge_close_work);
}

void fccu_actuators_set_fan_duty(uint8_t duty_percent) {
    uint8_t clamped = fccu_fan_clamp_duty(duty_percent);
    uint32_t pulse_ns = (uint32_t)(((uint64_t)fan_pwm.period * clamped) / 100U);
    pwm_set_pulse_dt(&fan_pwm, pulse_ns);
}

void fccu_actuators_emergency_stop(void) {
    fccu_actuators_cancel_purge_close();
    fccu_actuators_set_main_valve(0U);
    fccu_actuators_set_purge_valve(0U);
    fccu_actuators_set_fan_duty(0U);
}