//
// Created by pawel on 12/05/2026.
//

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include "system_status.h"

// Pobranie pinu can_led0 z Devicetree
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_NODELABEL(can_led0), gpios);

// Wewnętrzna zmienna przechowująca stan - musi używać nowego typu
static volatile can_status current_status = STATUS_CAN_ERROR;

// DEDYKOWANY WĄTEK DIODY (Thread)
void led_thread_function(void) {
    while (1) {
        // Kopiujemy status, żeby nie zmienił się w ułamku sekundy podczas mrugania
        can_status active_status = current_status;
        switch (active_status) {
            case STATUS_CAN_OK:
                gpio_pin_set_dt(&led, 1);
                k_msleep(100);
                if (current_status != active_status) continue;

                gpio_pin_set_dt(&led, 0);
                k_msleep(900);
                break;

            case STATUS_CAN_ERROR:
                gpio_pin_set_dt(&led, 1);
                k_msleep(100);
                if (current_status != active_status) continue;

                gpio_pin_set_dt(&led, 0);
                k_msleep(100);
                if (current_status != active_status) continue;

                gpio_pin_set_dt(&led, 1);
                k_msleep(100);
                if (current_status != active_status) continue;

                gpio_pin_set_dt(&led, 0);
                k_msleep(700);
                break;

            case STATUS_TEST:
                gpio_pin_set_dt(&led, 1);
                // Czekamy w nieskończoność (K_FOREVER), aż ktoś nas wybudzi z zewnątrz
                k_sleep(K_FOREVER);
                break;
        }
    }
}

// Rejestracja wątku
K_THREAD_DEFINE(led_thread_id, 512, led_thread_function, NULL, NULL, NULL, 7, 0, 0);

// INICJALIZACJA I API
void init_system_status(void) {
    if (!device_is_ready(led.port)) {
        return;
    }

    gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);

    // Status ustawiamy na błąd, a wątek uruchomiony wyżej automatycznie zacznie mrugać!
    set_system_status(STATUS_CAN_ERROR);
}

void set_system_status(can_status new_status) {
    if (current_status == new_status) return;

    current_status = new_status;

    //Natychmiastowe przerwanie k_msleep() w wątku led_thread!
    k_wakeup(led_thread_id);
}

can_status get_system_status(void) {
    return current_status;
}