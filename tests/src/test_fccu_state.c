#include <zephyr/ztest.h>
#include "fccu_state.h"
#include "fccu_config.h"

static fccu_state_ctx_t ctx;

static void before_each(void *f) {
    fccu_state_init(&ctx);
}

ZTEST_SUITE(fccu_state_tests, NULL, NULL, before_each, NULL, NULL);

ZTEST(fccu_state_tests, test_transitions_and_restart) {
zassert_equal(ctx.state, STATE_INIT);

fccu_state_set_enable(&ctx, 1U, 1000U);
zassert_equal(ctx.state, STATE_STARTING);

zassert_equal(fccu_state_step(&ctx, 1500U), STATE_RUNNING);
zassert_equal(ctx.last_can_rx_ms, 1500U);

fccu_state_set_enable(&ctx, 0U, 2000U);
zassert_equal(ctx.state, STATE_STOPPED);

/* Ponowne uruchomienie ze stanu STOPPED */
fccu_state_set_enable(&ctx, 1U, 2500U);
zassert_equal(ctx.state, STATE_STARTING);
}

ZTEST(fccu_state_tests, test_failsafe_can_timeout) {
fccu_state_set_enable(&ctx, 1U, 1000U);
fccu_state_step(&ctx, 1000U);

/* Tuż przed timeoutem -> nadal RUNNING */
zassert_false(fccu_state_check_failsafe(&ctx, 1000U + FCCU_CAN_TIMEOUT_MS - 1U));
zassert_equal(ctx.state, STATE_RUNNING);

/* Odświeżenie komunikacji resetuje timer */
fccu_state_notify_can_rx(&ctx, 3500U);
zassert_false(fccu_state_check_failsafe(&ctx, 3500U + FCCU_CAN_TIMEOUT_MS - 1U));

/* Przekroczenie timeoutu -> awaryjny STOP */
zassert_true(fccu_state_check_failsafe(&ctx, 3500U + FCCU_CAN_TIMEOUT_MS));
zassert_equal(ctx.state, STATE_STOPPED);
}

ZTEST(fccu_state_tests, test_failsafe_uint32_wraparound) {
fccu_state_set_enable(&ctx, 1U, UINT32_MAX - 1000U);
fccu_state_step(&ctx, UINT32_MAX - 1000U);

/* Po przepełnieniu licznika minęło 2000 ms -> brak timeoutu */
zassert_false(fccu_state_check_failsafe(&ctx, 999U));
/* Minęło dokładnie FCCU_CAN_TIMEOUT_MS (3000 ms) -> STOP */
zassert_true(fccu_state_check_failsafe(&ctx, 1999U));
zassert_equal(ctx.state, STATE_STOPPED);
}