#include <zephyr/ztest.h>
#include "fccu_config.h"
#include "core/fccu_scaling.h"

ZTEST_SUITE(fccu_scaling_tests, NULL, NULL, NULL, NULL, NULL);

ZTEST(fccu_scaling_tests, test_divider_and_voltage) {
/* Dzielnik 10k/15k: V_in = V_adc * 5 / 3 */
zassert_equal(scale_div_10k_15k_mv(0), 0, NULL);
zassert_equal(scale_div_10k_15k_mv(1500), 2500, NULL);
zassert_equal(scale_div_10k_15k_mv(3000), 5000, NULL);

/* Napięcie stosu FC_V: V_in = V_adc * 121 / 43, wynik w [V * 100] */
zassert_equal(calc_fc_voltage_x100(0), 0, NULL);
zassert_equal(calc_fc_voltage_x100(4300), 1210, NULL); /* 12.10 V */
}

ZTEST(fccu_scaling_tests, test_pressure_bounds_and_midpoint) {
/* Poniżej i na progu min (V_in <= 500 mV -> V_adc <= 300 mV) */
zassert_equal(calc_pressure_x100(200, FCCU_LP_MV_MIN, FCCU_LP_MV_MAX, FCCU_LP_MAX_BAR_X100), 0U, NULL);
zassert_equal(calc_pressure_x100(300, FCCU_LP_MV_MIN, FCCU_LP_MV_MAX, FCCU_LP_MAX_BAR_X100), 0U, NULL);

/* Środek zakresu (V_adc = 1500 mV -> V_in = 2500 mV -> 5.00 bar) */
zassert_equal(calc_pressure_x100(1500, FCCU_LP_MV_MIN, FCCU_LP_MV_MAX, FCCU_LP_MAX_BAR_X100), 500U, NULL);

/* Na progu i powyżej max (V_in >= 4500 mV -> V_adc >= 2700 mV) */
zassert_equal(calc_pressure_x100(2700, FCCU_LP_MV_MIN, FCCU_LP_MV_MAX, FCCU_LP_MAX_BAR_X100), 1000U, NULL);
zassert_equal(calc_pressure_x100(3000, FCCU_LP_MV_MIN, FCCU_LP_MV_MAX, FCCU_LP_MAX_BAR_X100), 1000U, NULL);
}

ZTEST(fccu_scaling_tests, test_fc_current_zero_pos_neg) {
/* 0 A przy V_in = 2500 mV (V_adc = 1500 mV) */
zassert_equal(calc_fc_current_x100(1500), 0, NULL);

/* +10.00 A przy V_in = 3500 mV (V_adc = 2100 mV) */
zassert_equal(calc_fc_current_x100(2100), 1000, NULL);

/* -10.00 A przy V_in = 1500 mV (V_adc = 900 mV) */
zassert_equal(calc_fc_current_x100(900), -1000, NULL);
}