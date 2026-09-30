//
// Created by pawel on 12/05/2026.
//

#ifndef SYSTEM_STATUS_H
#define SYSTEM_STATUS_H

typedef enum {
    STATUS_CAN_OK,
    STATUS_CAN_ERROR,
    STATUS_TEST
} can_status;

// Funkcje dostępu (API modułu)
void init_system_status(void);
void set_system_status(can_status new_status);
can_status get_system_status(void);

#endif //SYSTEM_STATUS_H
