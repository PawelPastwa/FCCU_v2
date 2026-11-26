#include "bsp/fccu_storage.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(fccu_storage, LOG_LEVEL_INF);

#define NVS_PARTITION        storage_partition
#define NVS_PARTITION_DEVICE PARTITION_DEVICE(NVS_PARTITION)
#define NVS_PARTITION_OFFSET PARTITION_OFFSET(NVS_PARTITION)
#define NVS_PARTITION_SIZE   PARTITION_SIZE(NVS_PARTITION)

#define FCCU_STORAGE_CFG_ID  1U
#define FCCU_STORAGE_MAGIC   0x46434355U /* "FCCU" */
#define FCCU_STORAGE_VERSION 1U

static struct nvs_fs nvs_handle;
static bool storage_ready = false;

int fccu_storage_init(void) {
    struct flash_pages_info info;

    nvs_handle.flash_device = NVS_PARTITION_DEVICE;
    if (!device_is_ready(nvs_handle.flash_device)) {
        LOG_ERR("Flash device not ready");
        return -ENODEV;
    }

    nvs_handle.offset = NVS_PARTITION_OFFSET;
    int rc = flash_get_page_info_by_offs(nvs_handle.flash_device, nvs_handle.offset, &info);
    if (rc != 0) {
        LOG_ERR("Unable to get flash page info: %d", rc);
        return rc;
    }

    uint32_t sectors = (uint32_t)(NVS_PARTITION_SIZE / info.size);
    if (sectors < 2U) {
        LOG_ERR("storage_partition too small for NVS");
        return -ENOSPC;
    }
    if (sectors > 4U) {
        sectors = 4U;
    }

    nvs_handle.sector_size  = (uint16_t)info.size;
    nvs_handle.sector_count = (uint16_t)sectors;

    rc = nvs_mount(&nvs_handle);
    if (rc != 0) {
        LOG_ERR("NVS mount failed: %d", rc);
        return rc;
    }

    storage_ready = true;
    LOG_INF("NVS mounted (%u sectors x %u B)", nvs_handle.sector_count, nvs_handle.sector_size);
    return 0;
}

bool fccu_storage_load(fccu_persisted_cfg_t *out) {
    if (!storage_ready || out == NULL) {
        return false;
    }

    fccu_persisted_cfg_t tmp;
    ssize_t rc = nvs_read(&nvs_handle, FCCU_STORAGE_CFG_ID, &tmp, sizeof(tmp));
    if (rc != (ssize_t)sizeof(tmp)) {
        LOG_INF("No saved FCCU config in Flash (using defaults)");
        return false;
    }

    if (tmp.magic != FCCU_STORAGE_MAGIC || tmp.version != FCCU_STORAGE_VERSION) {
        LOG_WRN("Saved FCCU config mismatch (magic/version), using defaults");
        return false;
    }

    *out = tmp;
    return true;
}

int fccu_storage_save(const fccu_persisted_cfg_t *cfg) {
    if (!storage_ready || cfg == NULL) {
        return -ENODEV;
    }

    fccu_persisted_cfg_t record;
    memset(&record, 0, sizeof(record));

    record.magic                      = FCCU_STORAGE_MAGIC;
    record.version                    = FCCU_STORAGE_VERSION;
    record.fan_mode                   = cfg->fan_mode;
    record.fan_manual_on              = cfg->fan_manual_on;
    record.fan_manual_duty            = cfg->fan_manual_duty;
    record.fan_p_gain                 = cfg->fan_p_gain;
    record.fan_i_gain                 = cfg->fan_i_gain;
    record.fan_d_gain                 = cfg->fan_d_gain;
    record.purge_mode                 = cfg->purge_mode;
    record.purge_interval_s           = cfg->purge_interval_s;
    record.purge_duration_ms          = cfg->purge_duration_ms;
    record.purge_voltage_threshold_mv = cfg->purge_voltage_threshold_mv;
    record.purge_voltage_cooldown_s   = cfg->purge_voltage_cooldown_s;

    ssize_t rc = nvs_write(&nvs_handle, FCCU_STORAGE_CFG_ID, &record, sizeof(record));
    if (rc < 0) {
        LOG_ERR("NVS write failed: %d", (int)rc);
        return (int)rc;
    }

    return 0;
}