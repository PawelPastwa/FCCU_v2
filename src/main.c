#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/pwm.h>

#include "hydrogreen_can_definitions.h"
#include "system_status.h"

// Zakładamy domyślny podział 1:20 (fizycznie ustawione na potencjometrze)
#define VOLTAGE_DIVIDER_MULTIPLIER 20
#define VOLTAGE_DIVIDER_DIVISOR    1

/* ======================================================================= */
/*                         STRUKTURY URZĄDZEŃ                              */
/* ======================================================================= */

const struct device *can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));

// Pobranie pinów GPIO, fan pwm i adc z aliasów zdefiniowanych w pliku .dts
static const struct gpio_dt_spec main_valve_pin = GPIO_DT_SPEC_GET(DT_NODELABEL(main_valve0), gpios);
static const struct gpio_dt_spec purge_valve_pin = GPIO_DT_SPEC_GET(DT_NODELABEL(purge_valve0), gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), button_onboard_gpios);
static const struct gpio_dt_spec flow_sensor_pin = GPIO_DT_SPEC_GET(DT_NODELABEL(flow_meter_0), gpios);
static const struct adc_dt_spec hp_sensor = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 5);
static const struct adc_dt_spec fc_voltage_sensor = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 2);
static const struct pwm_dt_spec fan_pwm = PWM_DT_SPEC_GET(DT_NODELABEL(fan_pwm0));

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

// Zmienne sterujące elementami wykonawczymi (logiczne)
volatile uint8_t logic_main_valve = 0;
volatile uint8_t logic_purge_valve = 0;
volatile uint8_t logic_fan_state = 0;
volatile uint8_t logic_fan_duty = 0;

// Nastawy konfiguracyjne (0x520)
volatile uint8_t  set_fan_mode = 0;         // 0: Manual, 1: Linear, 2: PID
volatile uint16_t set_purge_interval_s = 0; // Cykliczny czas przedmuchu w sekundach (<= 0 wyłącza automat)
volatile uint16_t set_p_gain = 0;
volatile uint16_t set_i_gain = 0;
volatile uint16_t set_d_gain = 0;
volatile uint8_t manual_fan_duty = 0;

volatile uint16_t set_purge_duration_ms = 100; // Domyślnie 100 ms, wspólne dla wszystkich wyzwalaczy
volatile uint32_t last_purge_time = 0;         // Czas systemowy ostatniego przedmuchu

// --- ZMIENNE POMIAROWE (PRZEPŁYW I CIŚNIENIE) ---
volatile uint32_t pulse_count = 0;
volatile uint16_t current_flow_ln_min = 0;      // Skala x0.1 (np. 125 = 12.5 ln/min)
volatile uint32_t total_volume_consumed_ln = 0; // Suma litrów w skali x100
static struct gpio_callback flow_cb_data;
int16_t hp_sample_buffer = 0;


// [DO PODMIANY: Symulator temperatury - do usunięcia w finalnej wersji]
volatile uint8_t mock_temperature = 25;
// [DOCELOWO: Zmienna przechowująca rzeczywisty odczyt z ADC]
// volatile uint8_t real_temperature = 0;

// [DO PODMIANY: Symulator parametrów - do usunięcia w finalnej wersji]
volatile int16_t mock_fc_voltage = 4500; // Symulacja 45.00 V
volatile int16_t mock_fc_current = 1500; // Symulacja 15.00 A
// [DOCELOWO: Zmienne przechowujące przetworzone wartości z ADC


/* ======================================================================= */
/*                          FUNKCJE POMOCNICZE                             */
/* ======================================================================= */

// --- UNIWERSALNA LOGIKA ZAWORU ZRZUTOWEGO (PURGE) ---
static struct k_work_delayable purge_close_work;

// Funkcja wykonywana asynchronicznie po upływie zadanego czasu
void purge_close_handler(struct k_work *work) {
    logic_purge_valve = 0;
    gpio_pin_set_dt(&purge_valve_pin, 0);
    printk(" -> [PURGE] Czas minal (%d ms) - Zamkniecie zaworu\n", set_purge_duration_ms);
}

// Funkcja wyzwalająca jeden cykl przedmuchu (wywoływana przez CAN, Przycisk lub Auto-Litr)
void start_purge_cycle(void) {
    if (current_state == STATE_RUNNING) {
        logic_purge_valve = 1;
        gpio_pin_set_dt(&purge_valve_pin, 1);

        // Reset systemowego czasu dla cyklicznego odliczania
        last_purge_time = k_uptime_get_32();

        // Zawsze kasujemy licznik objętości - w układzie znowu jest czysty wodór
        total_volume_consumed_ln = 0;

        printk(" -> [PURGE] Start przedmuchu na %d ms\n", set_purge_duration_ms);
        k_work_reschedule(&purge_close_work, K_MSEC(set_purge_duration_ms));
    }
}

// --- ZMIENNE I FUNKCJE DLA ZEWNĘTRZNEGO PRZYCISKU ---
static uint8_t click_count = 0;
static struct k_timer click_timer;
static struct k_work click_work;
static struct gpio_callback button_cb_data;

void click_work_handler(struct k_work *work) {
    if (click_count == 1) {
        // --- 1 KLIKNIĘCIE: TOGGLE MAIN VALVE ---
        printk("[BUTTON] Pojedyncze klikniecie -> Main Valve Toggle\n");
        logic_main_valve = !logic_main_valve;
        gpio_pin_set_dt(&main_valve_pin, logic_main_valve);
        printk(" -> Main Valve: %s\n", logic_main_valve ? "ON" : "OFF");

    } else if (click_count >= 2) {
        // --- 2 KLIKNIĘCIA: WYZWOLENIE PURGE ---
        printk("[BUTTON] Podwojne klikniecie -> Ręczne wyzwolenie Purge\n");
        start_purge_cycle();
    }

    click_count = 0;
}

void click_timer_handler(struct k_timer *dummy) {
    k_work_submit(&click_work);
}

void button_pressed_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
    printk("[ISR] Zlapano zbocze na pinie przycisku!\n");
    static uint32_t last_time = 0;
    uint32_t now = k_uptime_get_32();

    if (now - last_time > 50) {
        click_count++;
        last_time = now;
        k_timer_start(&click_timer, K_MSEC(300), K_NO_WAIT);
    }
}

// ==============================================================================================

void flow_sensor_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
    static uint32_t last_interrupt_time = 0;
    uint32_t current_time = k_uptime_get_32();

    // Ignoruje szybkie szumy (krótsze niż 15 ms)
    if ((current_time - last_interrupt_time) > 15) {
        pulse_count++;
        last_interrupt_time = current_time;
    }
}

// Funkcja aktualizująca sprzętowe wypełnienie PWM
void update_hardware_fan(uint8_t duty_percent) {

    if (duty_percent > 100) duty_percent = 100;
    uint32_t pulse_ns = (fan_pwm.period * duty_percent) / 100;
    pwm_set_pulse_dt(&fan_pwm, pulse_ns);

}

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

        // Bezpośrednie wysterowanie pinu po odebraniu komendy
        if (current_state == STATE_RUNNING) {
            gpio_pin_set_dt(&main_valve_pin, logic_main_valve);
        }
    }
}

void rx_manual_purge_cb(const struct device *dev, struct can_frame *frame, void *user_data) {
    struct hydrogreen_can_definitions_fccu_manual_purge_t rx;
    if (hydrogreen_can_definitions_fccu_manual_purge_unpack(&rx, frame->data, frame->dlc) == 0) {
        // Ręczne wysterowanie pinu zaworu zrzutowego
        if (rx.cmd_manual_purge == 1) {
            printk("[CAN RX] Otrzymano komende Manual Purge (WYZWOLENIE)\n");
            start_purge_cycle();
        }
    }
}

void rx_fan_cb(const struct device *dev, struct can_frame *frame, void *user_data) {
    struct hydrogreen_can_definitions_fccu_fan_t rx;
    if (hydrogreen_can_definitions_fccu_fan_unpack(&rx, frame->data, frame->dlc) == 0) {
        logic_fan_state = rx.cmd_fan_on;
        printk("[CAN RX] Fan Command: %d\n", logic_fan_state);

//        if (set_fan_mode == 0) {
//            // Jeśli włączono (1), ustaw 100%. Jeśli wyłączono (0), ustaw 0%.
//            logic_fan_duty = logic_fan_state ? 100 : 0;
//        }
        // Aktualizacja sprzętu tylko w trybie MANUAL
        if (set_fan_mode == 0) {
            logic_fan_duty = logic_fan_state ? 100 : 0;
            if (current_state == STATE_RUNNING) {
                update_hardware_fan(logic_fan_duty);
            }
        }
    }
}

void rx_settings_cb(const struct device *dev, struct can_frame *frame, void *user_data) {
    struct hydrogreen_can_definitions_fccu_fan_settings_t rx;
    if (hydrogreen_can_definitions_fccu_fan_settings_unpack(&rx, frame->data, frame->dlc) == 0) {
        set_fan_mode = rx.fan_mode;
        set_p_gain = rx.p_gain;
        set_i_gain = rx.i_gain;
        set_d_gain = rx.d_gain;
        printk("[CAN RX] Nowe nastawy -> FanMode: %d, Purge: %ds, PID: %d/%d/%d\n",
               set_fan_mode, set_p_gain, set_i_gain, set_d_gain);
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

//Odbiór nastaw zaworu (Interwał i Czas Trwania)
void rx_valve_settings_cb(const struct device *dev, struct can_frame *frame, void *user_data) {
    struct hydrogreen_can_definitions_fccu_valve_settings_t rx;
    if (hydrogreen_can_definitions_fccu_valve_settings_unpack(&rx, frame->data, frame->dlc) == 0) {

        // Aktualizacja interwału (<= 0 wyłącza automat)
        set_purge_interval_s = rx.purge_interval_s;

        // Zabezpieczenie czasu trwania (Anti-Idiot) w przedziale 10-500 ms
        if (rx.purge_duration_ms >= 10 && rx.purge_duration_ms <= 500) {
            set_purge_duration_ms = rx.purge_duration_ms;
        } else {
            printk("[CAN RX] BLAD: Odrzucono czas Purge (%d ms) - poza zakresem!\n", rx.purge_duration_ms);
        }

        printk("[CAN RX] Nowe nastawy zaworu -> Interwal: %d s, Czas zrzutu: %d ms\n",
               set_purge_interval_s, set_purge_duration_ms);
    }
}

/* ======================================================================= */
/*                                PĘTLA GŁÓWNA                             */
/* ======================================================================= */

int main(void) {
    k_sleep(K_MSEC(2000));
    printk("\n--- START SYSTEMU FCCU (Etap 1: Maszyna Stanow + LED) ---\n");

    // Inicjalizacja modułu LED (dioda zacznie migać błędem, póki CAN nie ruszy)
    init_system_status();

    // Inicjalizacja asynchronicznego domykania zaworu zrzutowego
    k_work_init_delayable(&purge_close_work, purge_close_handler);

    // --- INICJALIZACJA PRZYCISKU (Zewnętrznego) ---
    k_timer_init(&click_timer, click_timer_handler, NULL);
    k_work_init(&click_work, click_work_handler);

    if (gpio_is_ready_dt(&button)) {
        gpio_pin_configure_dt(&button, GPIO_INPUT | GPIO_PULL_UP);
        gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
        gpio_init_callback(&button_cb_data, button_pressed_isr, BIT(button.pin));
        gpio_add_callback(button.port, &button_cb_data);
    } else {
        printk("Blad: Przycisk zewnetrzny nie jest gotowy!\n");
    }

    // Weryfikacja i konfiguracja sprzętowa (Zawory i Wentylator)
    if (!gpio_is_ready_dt(&main_valve_pin)) {
        printk("Blad: Pin zaworu glownego nie jest gotowy\n");
        return -1;
    }
    gpio_pin_configure_dt(&main_valve_pin, GPIO_OUTPUT_INACTIVE);

    if (!gpio_is_ready_dt(&purge_valve_pin)) {
        printk("Blad: Pin zaworu purge nie jest gotowy\n");
        return -1;
    }
    gpio_pin_configure_dt(&purge_valve_pin, GPIO_OUTPUT_INACTIVE);

    if (!pwm_is_ready_dt(&fan_pwm)) {
        printk("Blad: Urzadzenie PWM (wentylator) nie jest gotowe!\n");
        return -1;
    }
    update_hardware_fan(0); // Ustawienie 0% na starcie

    if (!device_is_ready(can_dev)) {
        printk("Blad: Urzadzenie CAN nie gotowe!\n");
        return -1;
    }

    if (!adc_is_ready_dt(&fc_voltage_sensor)) {
        printk("Blad: ADC dla FC_VOLTAGE nie jest gotowe!\n");
        return -1;
    }

    if (gpio_is_ready_dt(&flow_sensor_pin)) {
        gpio_pin_configure_dt(&flow_sensor_pin, GPIO_INPUT);
        gpio_pin_interrupt_configure_dt(&flow_sensor_pin, GPIO_INT_EDGE_RISING);
        gpio_init_callback(&flow_cb_data, flow_sensor_isr, BIT(flow_sensor_pin.pin));
        gpio_add_callback(flow_sensor_pin.port, &flow_cb_data);
    }


    if (adc_is_ready_dt(&fc_voltage_sensor)) {
        adc_channel_setup_dt(&fc_voltage_sensor);
    }

    if (adc_is_ready_dt(&hp_sensor)) {
        adc_channel_setup_dt(&hp_sensor);
    }

    add_ext_rx_filter(HYDROGREEN_CAN_DEFINITIONS_FCCU_SYSTEM_ENABLE_FRAME_ID, &rx_system_enable_cb);
    add_ext_rx_filter(HYDROGREEN_CAN_DEFINITIONS_FCCU_MAIN_VALVE_COMMAND_FRAME_ID, &rx_main_valve_cb);
    add_ext_rx_filter(HYDROGREEN_CAN_DEFINITIONS_FCCU_MANUAL_PURGE_FRAME_ID, &rx_manual_purge_cb);
    add_ext_rx_filter(HYDROGREEN_CAN_DEFINITIONS_FCCU_FAN_FRAME_ID, &rx_fan_cb);
    add_ext_rx_filter(HYDROGREEN_CAN_DEFINITIONS_FCCU_FAN_SETTINGS_FRAME_ID, &rx_settings_cb);
    add_ext_rx_filter(HYDROGREEN_CAN_DEFINITIONS_FCCU_VALVE_SETTINGS_FRAME_ID, &rx_valve_settings_cb);

    can_set_mode(can_dev, CAN_MODE_NORMAL);
    can_start(can_dev);

    uint32_t loop_counter = 0;

    int err = adc_channel_setup_dt(&fc_voltage_sensor);
    if (err < 0) {
        printk("Blad konfiguracji kanalu napiecia ADC: %d\n", err);
    }

    while (1) {

        // --- ODCZYT PRZEPŁYWU (Hz na Litry) ---
        // Zbieramy impulsy przez 1 sekunde (10 cykli pętli 100ms) aby ustabilizować odczyt
        static uint32_t accumulated_pulses = 0;
        static uint32_t total_volume_ml = 0;
        static uint8_t flow_timer = 0;

        accumulated_pulses += pulse_count;
        pulse_count = 0;
        flow_timer++;

        if (flow_timer >= 10) { // Wykonaj raz na sekunde
            uint32_t current_hz = accumulated_pulses; // W 1 sekunde ilosc impulsow to po prostu Hz
            // Każdy impuls to ~1 ml gazu (zgodnie z Quantity = 0.0009994 L)
            total_volume_ml += accumulated_pulses;

            // Przeliczenie mililitrów na skalę x100 (1 litr = 100 jednostek, więc 10 ml = 1 jednostka)
            total_volume_consumed_ln = total_volume_ml / 10;

            accumulated_pulses = 0;
            flow_timer = 0;
            uint32_t k_factor = 1000;

            // Przeliczenie na skale x100 dla magistrali CAN (zgodnie z plikiem DBC: 0.01)
            // Wzór: (Hz * 60) / K = [L/min]
            // Skala x100 (aby zachowac ułamki): (Hz * 60 * 100) / K = (Hz * 6000) / K
            uint32_t calculated_flow = (current_hz * 6000) / k_factor;

            current_flow_ln_min = (uint16_t)calculated_flow;

            // Zuzucie w skali x100 (Dodajemy przepływ z ostatniej sekundy)
            // L/min przerobione na L/sekunde -> dzielimy przez 60
            total_volume_consumed_ln += (current_flow_ln_min / 60);

            // Wyświetlanie danych pomiarowych z poprawnym formatowaniem ułamków (2 miejsca po przecinku)
            printk("[PRZEPLYW] %d Hz | %d.%02d ln/min | Zuzucie: %d.%02d ln\n",
                   current_hz, current_flow_ln_min / 100, current_flow_ln_min % 100,
                   total_volume_consumed_ln / 100, total_volume_consumed_ln % 100);
        }

        // --- LOGIKA MASZYNY STANÓW ---
        switch (current_state) {
            case STATE_INIT:
                break;
            case STATE_STARTING:
                current_state = STATE_RUNNING;
                last_purge_time = k_uptime_get_32(); // Start odliczania czasu cyklicznego od teraz!
                printk("[STATE] Przejscie ze STARTING do RUNNING\n");
                break;
            case STATE_RUNNING:
                // Automatyczny zawór wydmuchowy (Purge) - Zależny od objętości
                if (set_purge_interval_s > 0) {
                    static uint32_t purge_time_ms = 0;
                    static bool is_purging = false;

                    if (set_purge_interval_s > 0) {
                        uint32_t current_time = k_uptime_get_32();
                        // Zabezpieczenie przed przedwczesnym zrzutem i przeliczenie sekund na milisekundy
                        if ((current_time - last_purge_time) >= ((uint32_t)set_purge_interval_s * 1000)) {
                            printk("[LOGIKA] Auto-Purge: Minelo %d sekund - Cykliczne wyzwolenie\n", set_purge_interval_s);
                            start_purge_cycle();
                        }
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

                // [DOCELOWO: zastąpić go fizycznym odczytem]
                // adc_read(adc_dev, &sequence);
                // real_temperature = calculate_temp_from_raw_adc(sample_buffer[0]);
                // (W poniższych blokach case 2 i case 3 zmienić  "mock_temperature" na "real_temperature")

                // [DO PODMIANY: Usuń ten blok, docelowo tu wpadną przeliczenia z prawdziwego sprzętu]
                if (temp_sim_timer == 19) { // Aktualizacja chwilę przed skokiem temperatury
                    mock_fc_voltage -= 10;  // Spadek napięcia o 0.10 V
                    mock_fc_current += 20;  // Wzrost prądu o 0.20 A
                    if (mock_fc_voltage < 3500) mock_fc_voltage = 4500;
                    if (mock_fc_current > 3000) mock_fc_current = 1500;
                }

                // --- Koniec Symulacji ---

                // Algorytm sterowania wentylatorem
                switch (set_fan_mode) {
                    case 0: // MANUAL
                        // Stan zmieniany bezpośrednio ramką 0x513 (Manual Fan Command)
                        // Obsługiwane wyżej w rx_fan_cb
                        break;
                    case 1: // LINEAR_TEMP (Termostat)
                        // Miejsce na przyszłą logikę odczytu temperatury z przetwornika ADC

                        //test
                        //===================================
                        if (mock_temperature >= 35) {
                            if (logic_fan_state == 0) printk("[LOGIKA] Fan ON (Temp >= 35C)\n");
                            logic_fan_state = 1;
                            logic_fan_duty = 100;
                        } else if (mock_temperature <= 30) {
                            if (logic_fan_state == 1) printk("[LOGIKA] Fan OFF (Temp <= 30C)\n");
                            logic_fan_state = 0;
                            logic_fan_duty = 0;
                        }
                        update_hardware_fan(logic_fan_duty);
                        //===================================
                        break;
                    case 2: // PID
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

                        update_hardware_fan(logic_fan_duty);
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
                k_work_cancel_delayable(&purge_close_work);

                logic_main_valve = 0;
                logic_purge_valve = 0;
                logic_fan_state = 0;

                // [DOCELOWO: Awaryjne wyłączenie wszystkich pinów sprzętowych]
                gpio_pin_set_dt(&main_valve_pin, 0);
                gpio_pin_set_dt(&purge_valve_pin, 0);
                pwm_set_pulse_dt(&fan_pwm, 0);
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

            // --- 3. NADAWANIE TELEMETRII ---

            // Ramka 0x504: Środowisko (FCCU_ENVIRONMENT)
            struct hydrogreen_can_definitions_fccu_environment_t tx_env = {
                    .fc_temp_c = mock_temperature * 100, // Zastosowanie skali 0.01 z DBC
                    .ambient_temp_c = 2200,              // Stałe 22.00 C
                    .ambient_humidity = 4500,            // Stałe 45.00 %
                    .ambient_pressure = 1013
            };
            uint8_t payload_env[8] = {0};
            int len_env = hydrogreen_can_definitions_fccu_environment_pack(payload_env, &tx_env, 8);
            struct can_frame frame_env = {
                    .id = HYDROGREEN_CAN_DEFINITIONS_FCCU_ENVIRONMENT_FRAME_ID,
                    .flags = CAN_FRAME_IDE,
                    .dlc = len_env
            };
            memcpy(frame_env.data, payload_env, len_env);
            can_send(can_dev, &frame_env, K_NO_WAIT, NULL, NULL);

            // Ramka 0x503: Zasilanie (FCCU_POWER)
            struct hydrogreen_can_definitions_fccu_power_t tx_power = {
                    .fc_voltage = mock_fc_voltage,
                    .fc_current = mock_fc_current,
                    .dcdc_voltage = 1200,                // Stałe 12.00 V
                    .dcdc_current = 500                  // Stałe 5.00 A
            };

            uint8_t payload_power[8] = {0};
            int len_power = hydrogreen_can_definitions_fccu_power_pack(payload_power, &tx_power, 8);

            struct can_frame frame_power = {
                    .id = HYDROGREEN_CAN_DEFINITIONS_FCCU_POWER_FRAME_ID,
                    .flags = CAN_FRAME_IDE,
                    .dlc = len_power
            };

            memcpy(frame_power.data, payload_power, len_power);
            can_send(can_dev, &frame_power, K_NO_WAIT, NULL, NULL);
        }

        struct hydrogreen_can_definitions_fccu_hydrogen_t tx_hydro = {
                .h2_flow = current_flow_ln_min,     // Przepływ w skali x0.01 (zgodnie z DBC)
                .h2_lp_pressure = hp_sample_buffer, // Zastępczo odczyt z ADS1015
                .h2_hp_pressure = 0,                // Brak podłączonego czujnika
                .leakage_voltage = 0                // Brak podłączonego czujnika
        };

        uint8_t payload_hydro[8] = {0};
        int len_hydro = hydrogreen_can_definitions_fccu_hydrogen_pack(payload_hydro, &tx_hydro, 8);

        struct can_frame frame_hydro = {
                .id = HYDROGREEN_CAN_DEFINITIONS_FCCU_HYDROGEN_FRAME_ID,
                .flags = CAN_FRAME_IDE,
                .dlc = len_hydro
        };

        memcpy(frame_hydro.data, payload_hydro, len_hydro);
        can_send(can_dev, &frame_hydro, K_NO_WAIT, NULL, NULL);

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