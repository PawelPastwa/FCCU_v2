#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include "can_definitions.h"
#include "system_status.h"

#define CLAMP(value, min, max) (((value) > (max)) ? (max) : (((value) < (min)) ? (min) : (value)))

const struct device *can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));

//Funkcja przerwania (Callback) do przechwytywania ramek w tle
void can_rx_callback(const struct device *dev, struct can_frame *frame, void *user_data) {
    printk("\n---> ODEBRANO RAMKE! ID: 0x%X, Dlugosc: %d bajtow\n", frame->id, frame->dlc);

    if (frame->dlc > 0) {
        printk("     Dane: ");
        for (int i = 0; i < frame->dlc; i++) {
            printk("%02X ", frame->data[i]);
        }
        printk("\n\n");
    }
}

int main(void) {
    // 1. Inicjalizacja wątku LED, zacznie mrugać kodem błędu (podwójnie) jak wszystko jest git to zacznie pojedyńczo
    init_system_status();

    if (!device_is_ready(can_dev)) {
        printk("Blad krytyczny: Urzadzenie CAN nie jest gotowe!\n");
        return 0;
    }

    can_set_mode(can_dev, CAN_MODE_NORMAL);
    can_start(can_dev);

    const struct can_filter rx_filter = {
            .id = 0,
            .mask = 0
    };
    can_add_rx_filter(can_dev, &can_rx_callback, NULL, &rx_filter);

    k_sleep(K_MSEC(1000));
    printk("FCCU CAN wystartowal poprawnie. Trwa nadawanie i nasluch...\n");

    while (1) {
        float raw_voltage = 10.0f;
        float raw_current = -5.0f;

        float safe_voltage = CLAMP(raw_voltage, -50.0f, 50.0f);
        float safe_current = CLAMP(raw_current, -20.0f, 20.0f);

        struct hydrogreen_can_definitions_fccu_power_t power_data;
        power_data.fc_voltage = safe_voltage;
        power_data.fc_current = safe_current;
        power_data.dcdc_voltage = 0.0f;
        power_data.dcdc_current = 0.0f;
        power_data.load_current = 0.0f;

        uint8_t payload[8];
        hydrogreen_can_definitions_fccu_power_pack(payload, &power_data, 8);

        struct can_frame frame = {
                .id = 0x201,
                .dlc = 8,
        };
        memcpy(frame.data, payload, 8);

        int ret = can_send(can_dev, &frame, K_MSEC(100), NULL, NULL);
        if (ret != 0) {
            printk("Blad wysylki: %d\n", ret);
            // 2. Jeśli CAN zgłasza problem, wymuszamy podwójne mignięcia
            set_system_status(STATUS_CAN_ERROR);
        } else {
            printk("FCCU_POWER wyslane pomyslnie!\n");
            // 3. Wysłanie poprawne - wymuszamy pojedyncze
            set_system_status(STATUS_CAN_OK);
        }

        k_sleep(K_MSEC(1000));
    }
    return 0;
}