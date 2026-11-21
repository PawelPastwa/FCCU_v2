#include "fccu_button.h"
#include "fccu_config.h"
#include "fccu_events.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/drivers/gpio.h>

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), button_onboard_gpios);
static struct gpio_callback button_cb_data;
static struct k_timer click_timer;
static struct k_work click_work;
static atomic_t click_count = ATOMIC_INIT(0);

static void click_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    atomic_val_t clicks = atomic_set(&click_count, 0);

    fccu_event_t evt = {
            .timestamp_ms = k_uptime_get_32()
    };

    if (clicks == 1) {
        evt.type = FCCU_EVT_BTN_SINGLE_CLICK;
        (void)fccu_event_post(&evt);
    } else if (clicks >= 2) {
        evt.type = FCCU_EVT_BTN_DOUBLE_CLICK;
        (void)fccu_event_post(&evt);
    }
}

static void click_timer_handler(struct k_timer *dummy) {
    ARG_UNUSED(dummy);
    k_work_submit(&click_work);
}

static void button_pressed_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);

    static uint32_t last_time = 0U;
    uint32_t now = k_uptime_get_32();

    if ((now - last_time) > FCCU_BUTTON_DEBOUNCE_MS) {
        atomic_inc(&click_count);
        last_time = now;
        k_timer_start(&click_timer, K_MSEC(FCCU_BUTTON_DOUBLE_CLICK_MS), K_NO_WAIT);
    }
}

int fccu_button_init(void) {
    if (!gpio_is_ready_dt(&button)) {
        return -ENODEV;
    }

    k_timer_init(&click_timer, click_timer_handler, NULL);
    k_work_init(&click_work, click_work_handler);

    gpio_pin_configure_dt(&button, GPIO_INPUT | GPIO_PULL_UP);
    gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
    gpio_init_callback(&button_cb_data, button_pressed_isr, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb_data);
    return 0;
}