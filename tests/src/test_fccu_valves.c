#include <zephyr/ztest.h>
#include "fccu_valves.h"
#include "fccu_config.h"

static fccu_valves_ctx_t ctx;

static void before_each(void *f) {
    fccu_valves_init(&ctx);
}

ZTEST_SUITE(fccu_valves_tests, NULL, NULL, before_each, NULL, NULL);

ZTEST(fccu_valves_tests, test_anti_idiot_settings) {
zassert_equal(ctx.purge_duration_ms, FCCU_PURGE_DURATION_DEFAULT_MS);

zassert_true(fccu_valves_set_settings(&ctx, 15U, FCCU_PURGE_DURATION_MIN_MS));
zassert_equal(ctx.purge_duration_ms, FCCU_PURGE_DURATION_MIN_MS);

zassert_true(fccu_valves_set_settings(&ctx, 15U, FCCU_PURGE_DURATION_MAX_MS));
zassert_equal(ctx.purge_duration_ms, FCCU_PURGE_DURATION_MAX_MS);

/* Wartości poza zakresem muszą zostać odrzucone bez nadpisania poprawnej */
zassert_false(fccu_valves_set_settings(&ctx, 15U, FCCU_PURGE_DURATION_MIN_MS - 1U));
zassert_false(fccu_valves_set_settings(&ctx, 15U, FCCU_PURGE_DURATION_MAX_MS + 1U));
zassert_equal(ctx.purge_duration_ms, FCCU_PURGE_DURATION_MAX_MS);
}

ZTEST(fccu_valves_tests, test_manual_purge_and_main_valve) {
/* Purge zablokowany poza RUNNING */
zassert_false(fccu_valves_start_purge(&ctx, false, 1000U));
zassert_equal(ctx.purge_valve, 0U);

/* Purge dozwolony w RUNNING */
zassert_true(fccu_valves_start_purge(&ctx, true, 1000U));
zassert_equal(ctx.purge_valve, 1U);

fccu_valves_close_purge(&ctx);
zassert_equal(ctx.purge_valve, 0U);

/* Toggle i reset zaworu głównego */
fccu_valves_toggle_main(&ctx);
zassert_equal(ctx.main_valve, 1U);
fccu_valves_reset_outputs(&ctx);
zassert_equal(ctx.main_valve, 0U);
}

ZTEST(fccu_valves_tests, test_auto_purge_and_wraparound) {
fccu_valves_set_settings(&ctx, 10U, 200U);
fccu_valves_sync_purge_timer(&ctx, UINT32_MAX - 4000U);

/* Minęło 9000 ms (po przepełnieniu uint32_t) -> za wcześnie */
zassert_false(fccu_valves_check_auto_purge(&ctx, true, 4999U));
/* Minęło 10000 ms -> wyzwolenie */
zassert_true(fccu_valves_check_auto_purge(&ctx, true, 5999U));
zassert_equal(ctx.purge_valve, 1U);
}

ZTEST(fccu_valves_tests, test_purge_retrigger_blocked_and_null_safety) {
fccu_valves_ctx_t ctx;
fccu_valves_init(&ctx);

/* Pierwsze otwarcie w STATE_RUNNING -> sukces */
zassert_true(fccu_valves_start_purge(&ctx, true, 1000U), NULL);
zassert_equal(ctx.purge_valve, 1U, NULL);

/* Próba ponownego otwarcia przed zamknięciem -> zablokowana */
zassert_false(fccu_valves_start_purge(&ctx, true, 1050U), NULL);
zassert_equal(ctx.last_purge_time_ms, 1000U, NULL);

/* Po zamknięciu można otworzyć ponownie */
fccu_valves_close_purge(&ctx);
zassert_true(fccu_valves_start_purge(&ctx, true, 2000U), NULL);

/* Ochrona przed NULL */
zassert_false(fccu_valves_start_purge(NULL, true, 3000U), NULL);
}