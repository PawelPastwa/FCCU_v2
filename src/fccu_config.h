#ifndef INZYNIERKA_FCCU_CONFIG_H
#define INZYNIERKA_FCCU_CONFIG_H

/* --- ZAWÓR PURGE (Anti-Idiot & domyślne wartości) --- */
#define FCCU_PURGE_DURATION_DEFAULT_MS  100U
#define FCCU_PURGE_DURATION_MIN_MS      10U
#define FCCU_PURGE_DURATION_MAX_MS      500U

/* --- CHŁODZENIE (Wentylator: Linear & PID) --- */
#define FCCU_FAN_DUTY_MIN_PCT           0U
#define FCCU_FAN_DUTY_MAX_PCT           100U
#define FCCU_FAN_LINEAR_ON_TEMP_C       35U
#define FCCU_FAN_LINEAR_OFF_TEMP_C      30U
#define FCCU_FAN_PID_SETPOINT_C         30

/* --- PRZEPŁYWOMIERZ --- */
#define FCCU_FLOW_K_FACTOR              1000U
#define FCCU_FLOW_SCALE_X100            6000U /* (60 s/min * 100 dla skali 0.01) */

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

#endif //INZYNIERKA_FCCU_CONFIG_H
