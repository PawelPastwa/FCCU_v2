#include <zephyr/ztest.h>
#include "fccu_flow.h"

static fccu_flow_ctx_t ctx;

static void before_each(void *f) {
    fccu_flow_init(&ctx);
}

ZTEST_SUITE(fccu_flow_tests, NULL, NULL, before_each, NULL, NULL);

ZTEST(fccu_flow_tests, test_flow_calculation_and_saturation) {
zassert_equal(fccu_flow_calc_x100(0U), 0U);
zassert_equal(fccu_flow_calc_x100(100U), 600U);
zassert_equal(fccu_flow_calc_x100(250U), 1500U);
/* Ochrona przed przepełnieniem uint16_t */
zassert_equal(fccu_flow_calc_x100(50000U), UINT16_MAX);
}

ZTEST(fccu_flow_tests, test_volume_accumulation_and_reset) {
/* 100 Hz = 600 (6.00 ln/min) -> 600 / 60 = 10 (0.10 ln na sekundę) */
fccu_flow_update_1s(&ctx, 100U);
zassert_equal(ctx.current_flow_x100, 600U);
zassert_equal(ctx.total_volume_consumed_ln, 10U);

fccu_flow_update_1s(&ctx, 100U);
zassert_equal(ctx.total_volume_consumed_ln, 20U);

fccu_flow_reset_volume(&ctx);
zassert_equal(ctx.total_volume_consumed_ln, 0U);
}