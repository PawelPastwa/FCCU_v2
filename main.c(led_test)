#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include "system_status.h"

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_NODELABEL(button0), gpios);

int main(void) {
    if (!device_is_ready(button.port)) return 0;
    gpio_pin_configure_dt(&button, GPIO_INPUT);

    init_system_status();

    bool can_initialized = false;
    uint32_t start_time = k_uptime_get_32();

    printk("System FCCU PRO wystartowal!\n");

    while (1) {
        if (gpio_pin_get_dt(&button) == 1) {
            set_system_status(STATUS_TEST);
        } else {
            if (can_initialized) {
                set_system_status(STATUS_CAN_OK);
            } else {
                set_system_status(STATUS_CAN_ERROR);
            }
        }

        if (!can_initialized && (k_uptime_get_32() - start_time > 5000)) {
            can_initialized = true;
            printk("Symulacja: CAN nawiazal polaczenie.\n");
        }

        k_msleep(50);
    }
    return 0;
}