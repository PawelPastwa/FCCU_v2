#include <zephyr/ztest.h>
#include "fccu_fan.h"
#include "fccu_config.h"

static fccu_fan_ctx_t ctx;

static void before_each(void *f) {
    fccu_fan_init(&ctx);
}

ZTEST_SUITE(fccu_fan_tests, NULL, NULL, before_each, NULL, NULL);

ZTEST(fccu_fan_tests, test_clamp_and_manual_mode) {
zassert_equal(fccu_fan_clamp_duty(0U), 0U);
zassert_equal(fccu_fan_clamp_duty(55U), 55U);
zassert_equal(fccu_fan_clamp_duty(150U), FCCU_FAN_DUTY_MAX_PCT);

fccu_fan_set_manual(&ctx, 1U);
zassert_equal(ctx.duty, FCCU_FAN_DUTY_MAX_PCT);
zassert_equal(ctx.state, 1U);

fccu_fan_stop(&ctx);
zassert_equal(ctx.duty, 0U);
zassert_equal(ctx.state, 0U);
}

ZTEST(fccu_fan_tests, test_linear_hysteresis) {
fccu_fan_set_settings(&ctx, FAN_MODE_LINEAR, 0U, 0U, 0U);

fccu_fan_step(&ctx, true, FCCU_FAN_LINEAR_ON_TEMP_C);
zassert_equal(ctx.duty, FCCU_FAN_DUTY_MAX_PCT);

/* W strefie histerezy (32 C) wentylator utrzymuje 100 proc. */
fccu_fan_step(&ctx, true, 32U);
zassert_equal(ctx.duty, FCCU_FAN_DUTY_MAX_PCT);

fccu_fan_step(&ctx, true, FCCU_FAN_LINEAR_OFF_TEMP_C);
zassert_equal(ctx.duty, FCCU_FAN_DUTY_MIN_PCT);
}

ZTEST(fccu_fan_tests, test_pid_and_anti_windup) {
fccu_fan_set_settings(&ctx, FAN_MODE_PID, 10U, 2U, 0U);

/* error = 5 -> P=50, I=10 -> 60 proc. */
fccu_fan_step(&ctx, true, 35U);
zassert_equal(ctx.duty, 60U);
zassert_equal(ctx.pid_integral, 5);

/* Nasycenie przy 50 C -> 100 proc., anti-windup blokuje przyrost całki */
fccu_fan_step(&ctx, true, 50U);
zassert_equal(ctx.duty, FCCU_FAN_DUTY_MAX_PCT);
zassert_equal(ctx.pid_integral, 5);

/* Spadek poniżej setpointu zeruje całkę i wyjście */
fccu_fan_step(&ctx, true, 25U);
zassert_equal(ctx.duty, 0U);
zassert_equal(ctx.pid_integral, 0);
}

ZTEST(fccu_fan_tests, test_invalid_mode_and_pid_reset_on_mode_change) {
fccu_fan_ctx_t ctx;
fccu_fan_init(&ctx);

/* Uruchom PID i nabij całkę */
fccu_fan_set_settings(&ctx, FAN_MODE_PID, 5U, 2U, 1U);
fccu_fan_step(&ctx, true, 35U);
zassert_true(ctx.pid_integral > 0, NULL);

/* Nieprawidłowy tryb (np. 3) ma zostać zignorowany */
fccu_fan_set_settings(&ctx, 3U, 5U, 2U, 1U);
zassert_equal(ctx.mode, FAN_MODE_PID, NULL);

/* Zmiana trybu na MANUAL ma wyzerować całkę i poprzedni błąd */
fccu_fan_set_settings(&ctx, FAN_MODE_MANUAL, 5U, 2U, 1U);
zassert_equal(ctx.mode, FAN_MODE_MANUAL, NULL);
zassert_equal(ctx.pid_integral, 0, NULL);
zassert_equal(ctx.pid_prev_error, 0, NULL);
}