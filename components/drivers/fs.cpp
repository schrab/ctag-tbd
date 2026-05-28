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
#include "esp_vfs_fat.h"
#include "fs.hpp"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"

using namespace CTAG::DRIVERS;

static sdmmc_card_t* sdcard = nullptr;

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
    ESP_LOGI("fs", "Initializing SD card via SDMMC");

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.gpio_cd = GPIO_NUM_NC; // skip card detect — manually polled via gpio 34 instead
    slot_config.gpio_wp = GPIO_NUM_NC;
    slot_config.width = 4;

    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 0,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };

    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sd", &host, &slot_config, &mount_config, &sdcard);
    if (ret != ESP_OK) {
        ESP_LOGE("fs", "SD card mount failed: %s", esp_err_to_name(ret));
        sdcard = nullptr;
        return false;
    }

    ESP_LOGI("fs", "SD card: %s, size %lluMB | FAT mounted at /sd",
             sdcard->cid.name,
             (uint64_t)sdcard->csd.capacity * sdcard->csd.sector_size / (1024 * 1024));
    return true;
}

bool FileSystem::IsSDMounted() {
    return sdcard != nullptr;
}
