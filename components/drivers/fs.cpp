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
#include "driver/gpio.h"
#if defined(CONFIG_TBD_PLATFORM_BBA2)
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#endif

using namespace CTAG::DRIVERS;

static sdmmc_card_t* sdcard = nullptr;

// Card-detect: GPIO34, input-only (no internal pull — the socket provides
// it). Active-low: the detect switch closes to GND with a card inserted.
// Verify on the bench via the io-caps push ("cd" field) after flashing.
static constexpr bool SD_CD_ACTIVE_LOW = true;

void FileSystem::InitFS() {
    ESP_LOGI("fs", "Initializing LITTLEFS");

    // Card-detect input: GPIO34 is input-only and resets as an input, but
    // configure it explicitly so gpio_get_level is well-defined.
#if !defined(CONFIG_TBD_PLATFORM_BBA2)
    // BBA2 has no card-detect pin (SDD §7): presence = mount state, and
    // GPIO34 is a spare on that board — leave it unconfigured.
    gpio_config_t cd = {};
    cd.pin_bit_mask = 1ULL << GPIO_NUM_34;
    cd.mode = GPIO_MODE_INPUT;
    cd.pull_up_en = GPIO_PULLUP_DISABLE; // 34-39 have no internal pulls
    cd.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cd.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cd);
#endif

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
#if defined(CONFIG_TBD_PLATFORM_BBA2)
    // BBA v2 rev A: the SD breakout is an SPI-mode module (MISO/MOSI/CS/CLK
    // behind a unidirectional level shifter) — use the SDSPI host. Native
    // SDMMC would need a bidirectional CMD line this module can't do.
    // Hot-swap note: the SPI bus is initialized once and stays up across
    // UNMOUNT/MOUNT cycles (esp_vfs_fat_sdcard_unmount deinits the sdspi
    // slot but not the raw bus); MountSD re-runs this whole function.
    ESP_LOGI("fs", "Initializing SD card via SDSPI");

    static bool s_spi_bus_ready = false;
    if (!s_spi_bus_ready) {
        spi_bus_config_t bus_cfg = {};
        bus_cfg.mosi_io_num = 14; // MOSI -> card CMD/DI
        bus_cfg.miso_io_num = 21; // MISO <- card D0/DO
        bus_cfg.sclk_io_num = 13; // SCK
        bus_cfg.quadwp_io_num = -1;
        esp_err_t bus_ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
        if (bus_ret != ESP_OK && bus_ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGE("fs", "SD SPI bus init failed: %s", esp_err_to_name(bus_ret));
            return false;
        }
        s_spi_bus_ready = true;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT(); // SPI2_HOST, 20 MHz default
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.host_id = SPI2_HOST;
    slot_config.gpio_cs = GPIO_NUM_47;
    slot_config.gpio_cd = SDSPI_SLOT_NO_CD; // no CD pin — presence = mount state
    slot_config.gpio_wp = SDSPI_SLOT_NO_WP;

    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 0,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };

    esp_err_t ret = esp_vfs_fat_sdspi_mount("/sd", &host, &slot_config, &mount_config, &sdcard);
    if (ret != ESP_OK) {
        ESP_LOGE("fs", "SD card mount failed: %s", esp_err_to_name(ret));
        sdcard = nullptr;
        return false;
    }
#else
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
#endif

    ESP_LOGI("fs", "SD card: %s, size %lluMB | FAT mounted at /sd",
             sdcard->cid.name,
             (uint64_t)sdcard->csd.capacity * sdcard->csd.sector_size / (1024 * 1024));
    return true;
}

bool FileSystem::IsSDMounted() {
    return sdcard != nullptr;
}

void FileSystem::InitCD() {
    // Kept as a no-op hook: the CD input is configured in InitFS before any
    // consumer can poll it.
}

bool FileSystem::IsSDPresent() {
#if defined(CONFIG_TBD_PLATFORM_BBA2)
    // No card-detect pin on BBA2 (SDD §7). Report "present" so the panel's
    // MOUNT flow (gated on present) always reaches the mount attempt, which
    // is the only presence probe this hardware has. Review finding: with
    // present == mount state, UNMOUNT made the MOUNT item show NO CARD
    // forever — dead hot-swap. The "UNMOUNTED"/"NO CARD" labels on the panel
    // become cosmetic; the "sd" stream field still reflects the real state.
    return true;
#else
    int lvl = gpio_get_level(GPIO_NUM_34);
    return SD_CD_ACTIVE_LOW ? lvl == 0 : lvl != 0;
#endif
}

bool FileSystem::UnmountSD() {
    if (sdcard == nullptr) return true;
    // esp_vfs_fat_sdcard_unmount also deinitials the SDMMC host; MountSD
    // re-runs the full init sequence.
    esp_err_t err = esp_vfs_fat_sdcard_unmount("/sd", sdcard);
    sdcard = nullptr;
    if (err != ESP_OK) {
        ESP_LOGE("fs", "SD unmount failed: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI("fs", "SD card unmounted");
    return true;
}

bool FileSystem::MountSD() {
    if (sdcard != nullptr) return true; // already mounted
#if !defined(CONFIG_TBD_PLATFORM_BBA2)
    // CD-pin boards: cheap pin pre-check before the slow mount attempt.
    // BBA2 has no pin — IsSDPresent() is the mount state (false right after
    // UNMOUNT), so the mount attempt itself must stay the presence probe.
    if (!IsSDPresent()) {
        ESP_LOGW("fs", "Mount requested but no card present (CD pin)");
        return false;
    }
#endif
    return InitSD();
}
