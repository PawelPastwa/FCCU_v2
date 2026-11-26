#ifndef INZYNIERKA_FCCU_CONFIG_H
#define INZYNIERKA_FCCU_CONFIG_H

/* --- ZAWÓR PURGE (Anti-Idiot & domyślne wartości) --- */
#define FCCU_PURGE_DURATION_DEFAULT_MS          100U
#define FCCU_PURGE_DURATION_MIN_MS              10U
#define FCCU_PURGE_DURATION_MAX_MS              500U
#define FCCU_PURGE_VOLTAGE_DEFAULT_MV           20000U /* Domyślny próg: 20.0 V */
#define FCCU_PURGE_VOLTAGE_MIN_VALID_MV         5000   /* Poniżej 5.0 V ogniwo uznajemy za wyłączone (brak fałszywych purge) */
#define FCCU_PURGE_VOLTAGE_COOLDOWN_DEFAULT_S   5U     /* Min. odstęp między przedmuchami od napięcia [s] */
#define FCCU_MCU_VOLTAGE_TIMEOUT_MS             2000U  /* Maks. wiek ramki 0x104 z MCU [ms] */

/* --- CHŁODZENIE (Wentylator: Linear & PID) --- */
#define FCCU_FAN_DUTY_MIN_PCT           0U
#define FCCU_FAN_DUTY_MAX_PCT           100U
#define FCCU_FAN_LINEAR_ON_TEMP_C       35U
#define FCCU_FAN_LINEAR_OFF_TEMP_C      30U
#define FCCU_FAN_PID_SETPOINT_C         30

/* --- PRZEPŁYWOMIERZ --- */
#define FCCU_FLOW_K_FACTOR              1000U /* Impulsy na 1 Ln (1 impuls = 0.001 Ln) */
#define FCCU_FLOW_SCALE_X100            6000U /* (60 s/min * 100 dla skali 0.01) */
#define FCCU_FLOW_DEBOUNCE_US           1000U /* 1 ms -> maks. 1000 Hz (60 Ln/min) */
#define FCCU_FLOW_TIMEOUT_MS            1500U /* Po 1.5 s bez impulsu przepływ = 0 */
#define FCCU_FLOW_FAST_PERIOD_MS        10U   /* 10 ms = 100 Hz (1U = 1000 Hz) */

/* --- BEZPIECZEŃSTWO (Fail-Safe) --- */
/* Maksymalny czas bez ramki CAN z nadrzędnego sterownika przed awaryjnym STOP */
#define FCCU_CAN_TIMEOUT_MS             3000U
#define FCCU_WDT_TIMEOUT_MS             2000U

/* --- PRZYCISK --- */
#define FCCU_BUTTON_DEBOUNCE_MS         25U
#define FCCU_BUTTON_DOUBLE_CLICK_MS     400U

/* --- PRZELICZNIKI DZIELNIKÓW NAPIĘCIA (ze schematu FCCU v2) --- */
/* Dzielnik 10k / 15k -> V_in = V_adc * 5 / 3 */
#define FCCU_DIV_10K_15K_NUM            5
#define FCCU_DIV_10K_15K_DEN            3

/* Dzielnik FC_V / SC_V (R=6k8, RV=2k, R=3k3 -> środek potencjometru: 12.1k / 4.3k)
 * Po ustawieniu potencjometrów RV5 / RV4 zmierz napięcie multimetrem i skoryguj mianownik. */
#define FCCU_DIV_FC_V_NUM               121
#define FCCU_DIV_FC_V_DEN               43

/* --- PARAMETRY CZUJNIKÓW (uzupełnij z not katalogowych czujników) --- */
/* Zakresy wejściowe po odtworzeniu napięcia 0..5000 mV -> wartość fizyczna * 100 (skala DBC 0.01) */
#define FCCU_LP_MV_MIN                  500     /* np. 0.5 V = 0 bar */
#define FCCU_LP_MV_MAX                  4500    /* np. 4.5 V = max bar */
#define FCCU_LP_MAX_BAR_X100            1000    /* 10.00 bar (max w DBC) */

#define FCCU_HP_MV_MIN                  500
#define FCCU_HP_MV_MAX                  4500
#define FCCU_HP_MAX_BAR_X100            35000   /* 350.00 bar */

#define FCCU_FC_C_ZERO_MV               2500    /* np. 2.5 V dla 0 A */
#define FCCU_FC_C_SENS_MV_PER_A         100     /* np. 100 mV / A */

//temperaturaaaa jest pijana no i buja sie po klubach
#define FCCU_USE_BME280                 1

/* 0 = tylko flow_meter, fan, main_valve, purge_valve | 1 = pełne ADC + BME280 */
#define FCCU_USE_EXTRA_SENSORS          0

/* --- TERMISTOR OGNIWA (J7 T_R -> IC2 -> GPIO10 / ADC1_CH9) --- */
#define FCCU_USE_FC_TEMP                1
#define FCCU_FC_TEMP_TYPE_PT1000        0      /* 0 = NTC 1k, 1 = Pt1000 */
#define FCCU_FC_TEMP_VCC_MV             3300   /* Zasilanie dzielnika R58 (+3.3V) */
#define FCCU_FC_TEMP_PULLUP_OHM         10000  /* R58 = 10k */
#define FCCU_FC_TEMP_OPAMP_GAIN_NUM     1      /* 1 = wtórnik; jeśli wzmacniacz 11x -> wpisz 11 */
#define FCCU_FC_TEMP_OPAMP_GAIN_DEN     1
#define FCCU_FC_TEMP_R25_OHM            1000   /* Rezystancja czujnika w 25 degC (1k) */
#define FCCU_FC_TEMP_NTC_BETA_K         3950   /* Stała Beta NTC (zmień wg noty termistora) */

/* --- MASZYNA STANÓW --- */
/* 1 = start w STATE_INIT i natychmiastowe przejście do STATE_RUNNING (bez stacyjki)
 * 0 = oczekiwanie w STATE_INIT na sygnał stacyjki / ramkę CAN 0x510=1 */
#define FCCU_AUTO_START_FROM_INIT       1

#endif //INZYNIERKA_FCCU_CONFIG_H
