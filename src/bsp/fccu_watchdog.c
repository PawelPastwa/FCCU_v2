#include "bsp/fccu_watchdog.h"
#include "fccu_config.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(fccu_wdt, LOG_LEVEL_INF);

static const struct device *const wdt = DEVICE_DT_GET(DT_ALIAS(watchdog0));
static int wdt_channel_id = -1;

int fccu_watchdog_init(void) {
    if (!device_is_ready(wdt)) {
        LOG_ERR("Watchdog device not ready");
        return -ENODEV;
    }

    struct wdt_timeout_cfg wdt_config = {
            .flags = WDT_FLAG_RESET_SOC,
            .window.min = 0U,
            .window.max = FCCU_WDT_TIMEOUT_MS,
            .callback = NULL
    };

    wdt_channel_id = wdt_install_timeout(wdt, &wdt_config);
    if (wdt_channel_id < 0) {
        LOG_ERR("Watchdog install timeout failed: %d", wdt_channel_id);
        return wdt_channel_id;
    }

    int err = wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG);
    if (err < 0) {
        LOG_ERR("Watchdog setup failed: %d", err);
        return err;
    }

    LOG_INF("Watchdog initialized (%u ms)", FCCU_WDT_TIMEOUT_MS);
    return 0;
}

void fccu_watchdog_feed(void) {
    if (wdt_channel_id >= 0) {
        (void)wdt_feed(wdt, wdt_channel_id);
    }
}