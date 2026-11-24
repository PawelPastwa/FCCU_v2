#include "fccu_button.h"
#include "fccu_config.h"
#include "fccu_events.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), button_onboard_gpios);
static struct gpio_callback button_cb_data;

static struct k_work_delayable debounce_work;
static struct k_work_delayable click_timeout_work;

/* Dostęp wyłącznie z wątku system workqueue (brak wyścigu między handlerami) */
static uint8_t click_count = 0U;

static void click_timeout_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    if (click_count == 1U) {
        click_count = 0U;
        fccu_event_t evt = {
                .type         = FCCU_EVT_BTN_SINGLE_CLICK,
                .timestamp_ms = k_uptime_get_32()
        };
        (void)fccu_event_post(&evt);
    } else {
        click_count = 0U;
    }
}

static void debounce_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    /* Potwierdź, że przycisk nadal jest fizycznie wciśnięty (odrzuca drgania przy puszczeniu) */
    if (gpio_pin_get_dt(&button) <= 0) {
        return;
    }

    click_count++;

    if (click_count >= 2U) {
        (void)k_work_cancel_delayable(&click_timeout_work);
        click_count = 0U;

        fccu_event_t evt = {
                .type         = FCCU_EVT_BTN_DOUBLE_CLICK,
                .timestamp_ms = k_uptime_get_32()
        };
        (void)fccu_event_post(&evt);
    } else {
        (void)k_work_reschedule(&click_timeout_work, K_MSEC(FCCU_BUTTON_DOUBLE_CLICK_MS));
    }
}

static void button_pressed_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);

    (void)k_work_reschedule(&debounce_work, K_MSEC(FCCU_BUTTON_DEBOUNCE_MS));
}

int fccu_button_init(void) {
    if (!gpio_is_ready_dt(&button)) {
        return -ENODEV;
    }

    k_work_init_delayable(&debounce_work, debounce_work_handler);
    k_work_init_delayable(&click_timeout_work, click_timeout_work_handler);

    gpio_pin_configure_dt(&button, GPIO_INPUT | GPIO_PULL_UP);
    gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
    gpio_init_callback(&button_cb_data, button_pressed_isr, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb_data);
    return 0;
}