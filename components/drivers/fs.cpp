/***************
CTAG TBD >>to be determined<< is an open source eurorack synthesizer module.

A project conceived within the Creative Technologies Arbeitsgruppe of
Kiel University of Applied Sciences: https://www.creative-technologies.de

(c) 2020 by Robert Manzke. All rights reserved.

The CTAG TBD software is licensed under the GNU General Public License
(GPL 3.0), available here: https://www.gnu.org/licenses/gpl-3.0.txt

The CTAG TBD hardware design is released under the Creative Commons
Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0).
Details here: https://creativecommons.org/licenses/by-nc-sa/4.0/

CTAG TBD is provided "as is" without any express or implied warranties.

License and copyright details for specific submodules are included in their
respective component folders / files if different from this license.
***************/

#include <sys/unistd.h>
#include <sys/stat.h>
#include <cstdlib>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_littlefs.h"
#include "fs.hpp"

#if CONFIG_LITTLEFS_SDMMC_SUPPORT
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#endif

using namespace CTAG::DRIVERS;

#if CONFIG_LITTLEFS_SDMMC_SUPPORT
static sdmmc_card_t* sdcard = nullptr;
#endif

void FileSystem::InitFS() {
    ESP_LOGI("fs", "Initializing LITTLEFS");

    esp_vfs_littlefs_conf_t conf = {};
    conf.base_path = "/spiffs";
    conf.partition_label = "storage";
    conf.format_if_mount_failed = true;
    conf.grow_on_mount = 1;

    esp_err_t ret = esp_vfs_littlefs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE("fs", "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE("fs", "Failed to find LITTLEFS partition");
        } else {
            ESP_LOGE("fs", "Failed to initialize LITTLEFS (%s)", esp_err_to_name(ret));
        }
        return;
    }

    size_t total = 0, used = 0;
    ret = esp_littlefs_info("storage", &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE("fs", "Failed to get LITTLEFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI("fs", "Partition size: total: %d, used: %d", total, used);
    }
}

bool FileSystem::InitSD() {
#if CONFIG_LITTLEFS_SDMMC_SUPPORT
    ESP_LOGI("fs", "Initializing SD card via SDMMC");

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.gpio_cd = GPIO_NUM_34;
    slot_config.gpio_wp = GPIO_NUM_NC;
    slot_config.width = 4;

    esp_err_t ret = sdmmc_host_init();
    if (ret != ESP_OK) {
        ESP_LOGE("fs", "SDMMC host init failed: %s", esp_err_to_name(ret));
        return false;
    }

    ret = sdmmc_host_init_slot(host.slot, &slot_config);
    if (ret != ESP_OK) {
        ESP_LOGE("fs", "SDMMC slot init failed: %s", esp_err_to_name(ret));
        sdmmc_host_deinit();
        return false;
    }

    sdcard = (sdmmc_card_t*)malloc(sizeof(sdmmc_card_t));
    if (!sdcard) {
        ESP_LOGE("fs", "Failed to allocate SD card struct");
        sdmmc_host_deinit();
        return false;
    }

    ret = sdmmc_card_init(&host, sdcard);
    if (ret != ESP_OK) {
        ESP_LOGE("fs", "SD card init failed: %s", esp_err_to_name(ret));
        free(sdcard);
        sdcard = nullptr;
        sdmmc_host_deinit();
        return false;
    }

    ESP_LOGI("fs", "SD card: %s, size %lluMB",
             sdcard->cid.name,
             (uint64_t)sdcard->csd.capacity * sdcard->csd.sector_size / (1024 * 1024));

    esp_vfs_littlefs_conf_t conf = {};
    conf.base_path = "/sd";
    conf.partition_label = NULL;
    conf.sdcard = sdcard;

    ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE("fs", "Failed to mount SD as LittleFS: %s", esp_err_to_name(ret));
        return false;
    }

    size_t total = 0, used = 0;
    esp_littlefs_info("sd", &total, &used);
    ESP_LOGI("fs", "SD LittleFS: total %d, used %d", total, used);
    return true;
#else
    ESP_LOGW("fs", "SDMMC support not enabled in Kconfig");
    return false;
#endif
}

bool FileSystem::IsSDMounted() {
#if CONFIG_LITTLEFS_SDMMC_SUPPORT
    return sdcard != nullptr;
#else
    return false;
#endif
}
