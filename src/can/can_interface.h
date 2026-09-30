//
// Created by pawel on 08/07/2026.
//

#ifndef CAN_INTERFACE_H
#define CAN_INTERFACE_H

#include <stdint.h>

/**
 * @brief Inicjalizuje kontroler TWAI w ESP32-S3 i uruchamia wątek nasłuchujący.
 */
void init_can_system(void);

/**
 * @brief Wrzuca ramkę do sprzętowego bufora nadawczego (TX).
 * * @param id  Identyfikator ramki CAN
 * @param data Wskaźnik na tablicę danych (max 8 bajtów)
 * @param dlc Długość danych (Data Length Code)
 * @return int 0 w przypadku sukcesu, kod błędu jeśli bufor jest pełny
 */
int can_transmit_message(uint32_t id, const uint8_t *data, uint8_t dlc);

#endif /* CAN_INTERFACE_H */
