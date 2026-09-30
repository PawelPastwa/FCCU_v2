#include <zephyr/kernel.h>
#include <zephyr/drivers/can.h>
#include <zephyr/logging/log.h>

#include "system_status.h"
#include "can_definitions.h"

LOG_MODULE_REGISTER(can_interface, LOG_LEVEL_INF);

/* 1. Definicja kontrolera CAN */
static const struct device *const can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));

/* 2. Systemowa kolejka wiadomości (FIFO) na 32 ramki */
CAN_MSGQ_DEFINE(can_rx_queue, 32);

/* 3. WĄTEK ODBIORCZY (Pracownik nasłuchujący) */
void can_rx_thread_function(void) {
    struct can_frame frame;

    while (1) {
        // Wątek zamarza w nieskończoność (K_FOREVER), czeka na system Zephyr
        k_msgq_get(&can_rx_queue, &frame, K_FOREVER);

        /* * === MIEJSCE NA LOGIKĘ Z candef.c ===

         */

        LOG_INF("Odebrano CAN ID: 0x%X, DLC: %d", frame.id, frame.dlc);
    }
}

K_THREAD_DEFINE(can_rx_thread_id, 1024, can_rx_thread_function, NULL, NULL, NULL, 2, 0, 0);

/* 4. INICJALIZACJA I FILTRY SPRZĘTOWE */
void init_can_system(void) {
    if (!device_is_ready(can_dev)) {
        LOG_ERR("Blad krzemu: Kontroler CAN nie odpowiada!");
        // Zgłaszamy natychmiastowy błąd do modułu UI (wątek led wybudzi się i zacznie mrugać)
        set_system_status(STATUS_CAN_ERROR);
        return;
    }

    /*==== CAN_MODE_LOOPBACK - do testowania na samej plytce bez transrecivera====*/
    can_set_mode(can_dev, CAN_MODE_NORMAL);
    /*=====can_set_mode(can_dev, CAN_MODE_LOOPBACK);====*/

    if (can_start(can_dev) != 0) {
        LOG_ERR("Nie udalo sie wystartowac kontrolera CAN!");
        set_system_status(STATUS_CAN_ERROR);
        return;
    }

    /* Filtr przepuszczający wszystko (do późniejszej modyfikacji) */
    const struct can_filter my_filter = {
            .id = 0x000,
            .mask = 0x000
    };

    int filter_id = can_add_rx_filter_msgq(can_dev, &can_rx_queue, &my_filter);

    if (filter_id < 0) {
        LOG_ERR("Nie udalo sie dodac filtru CAN!");
        set_system_status(STATUS_CAN_ERROR);
        return;
    }

    LOG_INF("System CAN zainicjalizowany i nasluchuje.");

    // Sukces inicjalizacji - system zdrowy
    set_system_status(STATUS_CAN_OK);
}

/* FUNKCJA NADAWCZA (Wysyłanie ramek) */
int can_transmit_message(uint32_t id, const uint8_t *data, uint8_t dlc) {
    if (!device_is_ready(can_dev)) {
        return -1;
    }

    struct can_frame frame = {
            .id = id,
            .dlc = dlc,
            // Domyślnie standardowy identyfikator 11-bitowy (CAN 2.0A)
            .flags = 0
    };

    // Kopiowanie bajtów do ramki sprzętowej
    for (int i = 0; i < dlc; i++) {
        frame.data[i] = data[i];
    }

    // K_NO_WAIT oznacza, że procesor nie będzie czekał (Zero Polling),
    // tylko od razu wrzuci ramkę do sprzętu i zajmie się czymś innym.
    int ret = can_send(can_dev, &frame, K_NO_WAIT, NULL, NULL);

    if (ret != 0) {
        LOG_ERR("Blad wysylania ramki CAN: %d", ret);
    }

    return ret;
}