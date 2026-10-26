#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/can.h>

// [DOCELOWO: Dodanie nagłówków sprzętowych]
// #include <zephyr/drivers/gpio.h>
// #include <zephyr/drivers/adc.h>
// #include <zephyr/drivers/pwm.h>

#include "hydrogreen_can_definitions.h"
#include "system_status.h"

/* ======================================================================= */
/*                         ZMIENNE STANÓW SYSTEMU                          */
/* ======================================================================= */
typedef enum {
    STATE_INIT     = 0,
    STATE_STARTING = 1,
    STATE_RUNNING  = 2,
    STATE_STOPPED  = 3
} fccu_state_t;

volatile fccu_state_t current_state = STATE_INIT;

// Zmienne sterujące elementami wykonawczymi
volatile uint8_t logic_main_valve = 0;
volatile uint8_t logic_purge_valve = 0;
volatile uint8_t logic_fan_state = 0;

// Nastawy konfiguracyjne
volatile uint8_t  set_fan_mode = 0;
volatile uint16_t set_purge_interval_s = 0;
volatile uint16_t set_p_gain = 0;
volatile uint16_t set_i_gain = 0;
volatile uint16_t set_d_gain = 0;

// [DO PODMIANY: Symulator temperatury - do usunięcia w finalnej wersji]
volatile uint8_t mock_temperature = 25;
// [DOCELOWO: Zmienna przechowująca rzeczywisty odczyt z ADC]
// volatile uint8_t real_temperature = 0;

volatile uint8_t logic_fan_duty = 0;

const struct device *can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));

// [DOCELOWO: Pobranie struktury urządzeń z Device Tree]
// static const struct gpio_dt_spec main_valve_pin = GPIO_DT_SPEC_GET(DT_NODELABEL(main_valve), gpios);
// static const struct gpio_dt_spec purge_valve_pin = GPIO_DT_SPEC_GET(DT_NODELABEL(purge_valve), gpios);
// static const struct pwm_dt_spec fan_pwm = PWM_DT_SPEC_GET(DT_NODELABEL(fan_pwm));
// (Oraz analogicznie konfiguracja kanału ADC)

/* ======================================================================= */
/*                              CAN RX CALLBACKI                           */
/* ======================================================================= */

void rx_system_enable_cb(const struct device *dev, struct can_frame *frame, void *user_data) {
    struct hydrogreen_can_definitions_fccu_system_enable_t rx;
    if (hydrogreen_can_definitions_fccu_system_enable_unpack(&rx, frame->data, frame->dlc) == 0) {
        // opcja 2: zgodnie ze sztuka ale bedzie sprawiac problemy
        //if (rx.cmd_system_enable == 1 && (current_state == STATE_INIT)
        if (rx.cmd_system_enable == 1 && (current_state == STATE_INIT || current_state == STATE_STOPPED)) {
            current_state = STATE_STARTING;
            printk("[CAN RX] System Enable = 1 -> Przejscie do STARTING\n");
        } else if (rx.cmd_system_enable == 0) {
            current_state = STATE_STOPPED;
            printk("[CAN RX] System Enable = 0 -> Przejscie do STOPPED\n");
        }
    }
}

void rx_main_valve_cb(const struct device *dev, struct can_frame *frame, void *user_data) {
    struct hydrogreen_can_definitions_fccu_main_valve_command_t rx;
    if (hydrogreen_can_definitions_fccu_main_valve_command_unpack(&rx, frame->data, frame->dlc) == 0) {
        logic_main_valve = rx.cmd_main_valve;
        printk("[CAN RX] Main Valve Command: %d\n", logic_main_valve);

        // [DOCELOWO: Bezpośrednie wysterowanie pinu po komendzie CAN, jeśli system pozwala]
        // if (current_state == STATE_RUNNING) {
        //     gpio_pin_set_dt(&main_valve_pin, logic_main_valve);
        // }
    }
}

void rx_manual_purge_cb(const struct device *dev, struct can_frame *frame, void *user_data) {
    struct hydrogreen_can_definitions_fccu_manual_purge_t rx;
    if (hydrogreen_can_definitions_fccu_manual_purge_unpack(&rx, frame->data, frame->dlc) == 0) {
        logic_purge_valve = rx.cmd_manual_purge;
        printk("[CAN RX] Manual Purge Command: %d\n", logic_purge_valve);

        // [DOCELOWO: Ręczne wysterowanie pinu zaworu zrzutowego]
        // if (current_state == STATE_RUNNING) {
        //     gpio_pin_set_dt(&purge_valve_pin, logic_purge_valve);
        // }
    }
}

void rx_fan_cb(const struct device *dev, struct can_frame *frame, void *user_data) {
    struct hydrogreen_can_definitions_fccu_fan_t rx;
    if (hydrogreen_can_definitions_fccu_fan_unpack(&rx, frame->data, frame->dlc) == 0) {
        logic_fan_state = rx.cmd_fan_on;
        printk("[CAN RX] Fan Command: %d\n", logic_fan_state);

        // [DOCELOWO: Ręczne ustawienie PWM przy komendzie dla trybu MANUAL (set_fan_mode == 1)]
        // if (set_fan_mode == 1 && current_state == STATE_RUNNING) {
        //     logic_fan_duty = logic_fan_state ? 50 : 0; // Przykładowo 50% lub inna wartość domyślna
        //     pwm_set_pulse_dt(&fan_pwm, (fan_pwm.period * logic_fan_duty) / 100);
        // }
    }
}

void rx_settings_cb(const struct device *dev, struct can_frame *frame, void *user_data) {
    struct hydrogreen_can_definitions_fccu_settings_t rx;
    if (hydrogreen_can_definitions_fccu_settings_unpack(&rx, frame->data, frame->dlc) == 0) {
        set_fan_mode = rx.fan_mode;
        set_purge_interval_s = rx.purge_interval_s;
        set_p_gain = rx.p_gain;
        set_i_gain = rx.i_gain;
        set_d_gain = rx.d_gain;
        printk("[CAN RX] Nowe nastawy -> FanMode: %d, Purge: %ds, PID: %d/%d/%d\n",
               set_fan_mode, set_purge_interval_s, set_p_gain, set_i_gain, set_d_gain);
    }
}

void add_ext_rx_filter(uint32_t ext_id, can_rx_callback_t cb) {
    struct can_filter filter = {
            .id = ext_id,
            .mask = CAN_EXT_ID_MASK,
            .flags = CAN_FILTER_IDE
    };
    can_add_rx_filter(can_dev, cb, NULL, &filter);
}

/* ======================================================================= */
/*                                PĘTLA GŁÓWNA                             */
/* ======================================================================= */

int main(void) {
    k_sleep(K_MSEC(2000));
    printk("\n--- START SYSTEMU FCCU (Etap 1: Maszyna Stanow + LED) ---\n");

    // Inicjalizacja Twojego modułu LED (dioda zacznie migać błędem, póki CAN nie ruszy)
    init_system_status();

    // [DOCELOWO: Inicjalizacja peryferiów sprzętowych na starcie]
    // gpio_pin_configure_dt(&main_valve_pin, GPIO_OUTPUT_INACTIVE);
    // gpio_pin_configure_dt(&purge_valve_pin, GPIO_OUTPUT_INACTIVE);
    // adc_channel_setup_dt(...);

    if (!device_is_ready(can_dev)) {
        printk("Blad: Urzadzenie CAN nie gotowe!\n");
        return -1;
    }

    add_ext_rx_filter(HYDROGREEN_CAN_DEFINITIONS_FCCU_SYSTEM_ENABLE_FRAME_ID, &rx_system_enable_cb);
    add_ext_rx_filter(HYDROGREEN_CAN_DEFINITIONS_FCCU_MAIN_VALVE_COMMAND_FRAME_ID, &rx_main_valve_cb);
    add_ext_rx_filter(HYDROGREEN_CAN_DEFINITIONS_FCCU_MANUAL_PURGE_FRAME_ID, &rx_manual_purge_cb);
    add_ext_rx_filter(HYDROGREEN_CAN_DEFINITIONS_FCCU_FAN_FRAME_ID, &rx_fan_cb);
    add_ext_rx_filter(HYDROGREEN_CAN_DEFINITIONS_FCCU_SETTINGS_FRAME_ID, &rx_settings_cb);

    can_set_mode(can_dev, CAN_MODE_NORMAL);
    can_start(can_dev);

    uint32_t loop_counter = 0;

    while (1) {
        // --- 1. LOGIKA MASZYNY STANÓW ---
        switch (current_state) {
            case STATE_INIT:
                break;
            case STATE_STARTING:
                // [DO PODMIANY: Obecnie przejście jest bezwarunkowe]
                // [DOCELOWO: W tym miejscu zrobisz odczyt z czujników (napięcie stosu/butli) przed startem]
                // if (adc_read_voltage() > MIN_VOLTAGE) {
                current_state = STATE_RUNNING;
                printk("[STATE] Przejscie ze STARTING do RUNNING\n");
                // } else {
                //     current_state = STATE_STOPPED;
                //     printk("[ERR] Niskie napiecie, start przerwany!\n");
                // }
                break;
            case STATE_RUNNING:
                // Automatyczny zawór wydmuchowy (Purge)
                if (set_purge_interval_s > 0) {
                    static uint32_t purge_timer = 0;
                    static bool is_purging = false;

                    purge_timer++;

                    // Główna pętla wykonuje się co 100ms (mnożnik x10 dla sekund)
                    if (!is_purging && (purge_timer >= (set_purge_interval_s * 10))) {
                        logic_purge_valve = 1;
                        is_purging = true;
                        purge_timer = 0;
                        printk("[LOGIKA] Auto-Purge: Otwarcie zaworu\n");
                        // [DOCELOWO: Wysterowanie pinu na stan WYSOKI]
                        // gpio_pin_set_dt(&purge_valve_pin, 1);
                    }
                        // Czas trwania samego wydmuchu (np. 500ms = 5 cykli)
                    else if (is_purging && (purge_timer >= 5)) {
                        logic_purge_valve = 0;
                        is_purging = false;
                        purge_timer = 0;
                        printk("[LOGIKA] Auto-Purge: Zamkniecie zaworu\n");
                        // [DOCELOWO: Wysterowanie pinu na stan NISKI]
                        // gpio_pin_set_dt(&purge_valve_pin, 0);
                    }
                }
                //test, [DOCELOWO: Caly blok do usunięcia]
                //===========================================
                // --- 2. Symulacja wzrostu temperatury ---
                static uint16_t temp_sim_timer = 0;
                temp_sim_timer++;
                if (temp_sim_timer >= 20) { // Co 2 sekundy (20 * 100ms)
                    mock_temperature++;
                    if (mock_temperature > 45) {
                        mock_temperature = 25; // Reset po osiągnięciu 45 stopni
                    }
                    temp_sim_timer = 0;
                    printk("[MOCK] Aktualna temperatura: %d C\n", mock_temperature);
                }
                //===========================================
                // [DOCELOWO: zastąpić go fizycznym odczytem]
                // adc_read(adc_dev, &sequence);
                // real_temperature = calculate_temp_from_raw_adc(sample_buffer[0]);
                // (W poniższych blokach case 2 i case 3 zmienić  "mock_temperature" na "real_temperature")

                // Algorytm sterowania wentylatorem
                switch (set_fan_mode) {
                    case 0: // OFF
                        logic_fan_state = 0;
                        logic_fan_duty = 0;
                        break;
                    case 1: // MANUAL
                        // Stan zmieniany bezpośrednio ramką 0x513 (Manual Fan Command)
                        // Obsługiwane wyżej w rx_fan_cb
                        break;
                    case 2: // LINEAR_TEMP (Termostat)
                        // Miejsce na przyszłą logikę odczytu temperatury z przetwornika ADC

                        //test
                        //===================================
                        if (mock_temperature >= 35) {
                            if (logic_fan_state == 0) printk("[LOGIKA] Fan ON (Temp >= 35C)\n");
                            logic_fan_state = 1;
                        } else if (mock_temperature <= 30) {
                            if (logic_fan_state == 1) printk("[LOGIKA] Fan OFF (Temp <= 30C)\n");
                            logic_fan_state = 0;
                        }
                        //===================================
                        break;
                    case 3: // PID
                        // Miejsce na dyskretną pętlę PID korzystającą z set_p_gain, set_i_gain, set_d_gain
                        // Obliczenia uchybu między wartością zadaną a odczytem temperatury

                        //test
                        //=================================================
                    {
                        static int32_t integral = 0;
                        static int32_t previous_error = 0;
                        int32_t setpoint = 30; // Utrzymujemy 30 stopni

                        int32_t error = mock_temperature - setpoint;

                        // Zbieramy uchyb
                        integral += error;

                        // Obliczamy poszczególne człony
                        int32_t p_term = set_p_gain * error;
                        int32_t i_term = set_i_gain * integral;
                        int32_t d_term = set_d_gain * (error - previous_error);

                        previous_error = error;
                        int32_t output = p_term + i_term + d_term;

                        // Skuteczny Anti-Windup dla urządzenia chłodzącego
                        if (output >= 100) {
                            output = 100;
                            // Jeśli wysterowanie osiągnęło 100%, zabraniamy dalszego rośnięcia całki
                            if (error > 0) {
                                integral -= error;
                            }
                        } else if (error <= 0 || output <= 0) {
                            // Jeśli temperatura spadła do zadanej (lub niżej),
                            // odcinamy wiatrak i CZYŚCIMY pamięć całki
                            output = 0;
                            integral = 0;
                        }

                        logic_fan_duty = (uint8_t)output;
                        logic_fan_state = (logic_fan_duty > 0) ? 1 : 0;
                        break;
                    }
                        //=================================================
                        // [DOCELOWO: Wysłanie obliczonego wypełnienia PWM na pin]
                        // Jeśli jesteśmy w trybie Auto (2 lub 3), aktualizujemy sygnał sprzętowy:
                        // if (set_fan_mode == 2 || set_fan_mode == 3) {
                        //     pwm_set_pulse_dt(&fan_pwm, (fan_pwm.period * logic_fan_duty) / 100);
                        // }
                }
                break;
            case STATE_STOPPED:
                logic_main_valve = 0;
                logic_purge_valve = 0;
                logic_fan_state = 0;

                // [DOCELOWO: Awaryjne wyłączenie wszystkich pinów sprzętowych]
                // gpio_pin_set_dt(&main_valve_pin, 0);
                // gpio_pin_set_dt(&purge_valve_pin, 0);
                // pwm_set_pulse_dt(&fan_pwm, 0);
                break;
        }

        // --- 2. NADAWANIE RAMEK CAN (TX) ---
        struct hydrogreen_can_definitions_fccu_state_t tx_state = {
                .state = current_state,
                .main_valve = logic_main_valve,
                .purge_valve = logic_purge_valve,
                .fan_state = logic_fan_state,
                .fan_duty = logic_fan_duty
        };

        uint8_t payload_state[8] = {0};
        int len_state = hydrogreen_can_definitions_fccu_state_pack(payload_state, &tx_state, 8);

        struct can_frame frame_state = {
                .id = HYDROGREEN_CAN_DEFINITIONS_FCCU_STATE_FRAME_ID,
                .flags = CAN_FRAME_IDE,
                .dlc = len_state
        };
        memcpy(frame_state.data, payload_state, len_state);

        int ret = can_send(can_dev, &frame_state, K_MSEC(10), NULL, NULL);
        if (ret != 0) {
            // Jeżeli ramka nie przeszła na szynę - zgłoś błąd komunikacji (Dioda LED ERROR)
            set_system_status(STATUS_CAN_ERROR);

            enum can_state bus_state;
            struct can_bus_err_cnt err_cnt;
            can_get_state(can_dev, &bus_state, &err_cnt);

            if (bus_state == CAN_STATE_BUS_OFF) {
                printk("[CAN ERR] BUS-OFF! Oczekiwanie na automatyczny reset magistrali przez Zephyr...\n");
            }
        } else {
            // Jeżeli wysyłanie przeszło pomyślnie - ustaw status LED OK
            set_system_status(STATUS_CAN_OK);
        }

        if (loop_counter % 50 == 0) {
            struct hydrogreen_can_definitions_fccu_version_t tx_version = {
                    .firmware_major = 1,
                    .firmware_minor = 0,
                    .firmware_patch = 0,
                    .build_id = 123456,
                    .vehicle_type = 2,
                    .fuelcell_type = 1
            };

            uint8_t payload_ver[8] = {0};
            int len_ver = hydrogreen_can_definitions_fccu_version_pack(payload_ver, &tx_version, 8);

            struct can_frame frame_version = {
                    .id = HYDROGREEN_CAN_DEFINITIONS_FCCU_VERSION_FRAME_ID,
                    .flags = CAN_FRAME_IDE,
                    .dlc = len_ver
            };
            memcpy(frame_version.data, payload_ver, len_ver);
            can_send(can_dev, &frame_version, K_NO_WAIT, NULL, NULL);
        }

        loop_counter++;
        k_sleep(K_MSEC(100));
    }
    return 0;
}