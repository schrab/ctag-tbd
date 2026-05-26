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

#include "BtMidiReceiver.hpp"
#include <cstring>
#include <cstdio>
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_bt_device.h"
#include "esp_spp_api.h"
#include "nvs_flash.h"

using namespace CTAG::DRIVERS;

static const char *TAG = "BT_MIDI";

#define BT_MIDI_RING_SZ 2048
#define MAX_DEVICES 20

// Ring buffer
static uint8_t ring[BT_MIDI_RING_SZ];
static volatile int ring_wr = 0;
static volatile int ring_rd = 0;

// Connection state
static bool bt_initialized = false;
static uint32_t bt_spp_handle = 0;
static bool bt_connected = false;
static bool bt_scanning = false;

// Device list
static BtDeviceInfo devices[MAX_DEVICES];
static int device_count = 0;

static void ring_write(const uint8_t *data, int len) {
    for (int i = 0; i < len; i++) {
        int next = (ring_wr + 1) & (BT_MIDI_RING_SZ - 1);
        if (next == ring_rd) break;
        ring[ring_wr] = data[i];
        ring_wr = next;
    }
}

static int ring_read(uint8_t *buf, int max) {
    int n = 0;
    while (ring_rd != ring_wr && n < max) {
        buf[n++] = ring[ring_rd];
        ring_rd = (ring_rd + 1) & (BT_MIDI_RING_SZ - 1);
    }
    return n;
}

static char *bda2str(uint8_t *bda, char *str, size_t size) {
    if (!bda || !str || size < 18) return nullptr;
    snprintf(str, size, "%02x:%02x:%02x:%02x:%02x:%02x",
             bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
    return str;
}

static bool get_name_from_eir(uint8_t *eir, char *bdname, uint8_t *bdname_len) {
    uint8_t *rmt_bdname = nullptr;
    uint8_t rmt_bdname_len = 0;
    if (!eir) return false;
    rmt_bdname = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &rmt_bdname_len);
    if (!rmt_bdname) {
        rmt_bdname = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &rmt_bdname_len);
    }
    if (rmt_bdname) {
        if (rmt_bdname_len > ESP_BT_GAP_MAX_BDNAME_LEN) rmt_bdname_len = ESP_BT_GAP_MAX_BDNAME_LEN;
        if (bdname) { memcpy(bdname, rmt_bdname, rmt_bdname_len); bdname[rmt_bdname_len] = '\0'; }
        if (bdname_len) *bdname_len = rmt_bdname_len;
        return true;
    }
    return false;
}

static void gapCallback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
    switch (event) {
    case ESP_BT_GAP_DISC_RES_EVT: {
        if (device_count >= MAX_DEVICES) break;
        BtDeviceInfo &d = devices[device_count];
        memcpy(d.bda, param->disc_res.bda, 6);
        d.cod = 0;
        d.name[0] = '\0';
        for (int i = 0; i < param->disc_res.num_prop; i++) {
            if (param->disc_res.prop[i].type == ESP_BT_GAP_DEV_PROP_EIR) {
                get_name_from_eir((uint8_t*)param->disc_res.prop[i].val, d.name, nullptr);
            } else if (param->disc_res.prop[i].type == ESP_BT_GAP_DEV_PROP_BDNAME) {
                if (param->disc_res.prop[i].len > 0) {
                    memcpy(d.name, param->disc_res.prop[i].val,
                           param->disc_res.prop[i].len < 31 ? param->disc_res.prop[i].len : 31);
                    d.name[31] = '\0';
                }
            } else if (param->disc_res.prop[i].type == ESP_BT_GAP_DEV_PROP_COD) {
                d.cod = *(const uint32_t *)param->disc_res.prop[i].val;
            }
        }
        device_count++;
        char bda_str[18];
        ESP_LOGI(TAG, "Found: %s [%s] COD=%06" PRIx32,
                 (d.name[0] ? d.name : "(unnamed)"),
                 bda2str(d.bda, bda_str, sizeof(bda_str)), d.cod);
        break;
    }
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
        if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
            bt_scanning = false;
            ESP_LOGI(TAG, "Discovery stopped, %d devices found", device_count);
        }
        break;
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        ESP_LOGI(TAG, "Auth complete: status=%d", param->auth_cmpl.stat);
        break;
    case ESP_BT_GAP_PIN_REQ_EVT: {
        esp_bt_pin_code_t pin = {};
        esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin);
        break;
    }
    case ESP_BT_GAP_CFM_REQ_EVT:
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
    default:
        break;
    }
}

static void sppCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param) {
    char bda_str[18];
    switch (event) {
    case ESP_SPP_INIT_EVT:
        if (param->init.status == ESP_SPP_SUCCESS) {
            ESP_LOGI(TAG, "SPP init OK");
            esp_bt_gap_set_device_name("CTAG TBD MIDI");
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
        }
        break;
    case ESP_SPP_SRV_OPEN_EVT:
        bt_connected = true;
        bt_spp_handle = param->srv_open.handle;
        ESP_LOGI(TAG, "SPP connected handle=%" PRIu32 " rem=[%s]",
                 param->srv_open.handle,
                 bda2str(param->srv_open.rem_bda, bda_str, sizeof(bda_str)));
        break;
    case ESP_SPP_OPEN_EVT:
        bt_connected = true;
        bt_spp_handle = param->open.handle;
        ESP_LOGI(TAG, "SPP open handle=%" PRIu32 " rem=[%s]",
                 param->open.handle,
                 bda2str(param->open.rem_bda, bda_str, sizeof(bda_str)));
        break;
    case ESP_SPP_CLOSE_EVT:
        bt_connected = false;
        bt_spp_handle = 0;
        ESP_LOGI(TAG, "SPP closed");
        break;
    case ESP_SPP_DATA_IND_EVT:
        ring_write(param->data_ind.data, param->data_ind.len);
        break;
    case ESP_SPP_DISCOVERY_COMP_EVT:
        ESP_LOGI(TAG, "SPP discovery complete");
        break;
    default:
        break;
    }
}

void BtMidiReceiver::Init() {
    if (bt_initialized) return;

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if (esp_bt_controller_init(&bt_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "BT controller init failed");
        return;
    }
    if (esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT) != ESP_OK) {
        ESP_LOGE(TAG, "BT controller enable failed");
        return;
    }

    esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    if (esp_bluedroid_init_with_cfg(&bluedroid_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid init failed");
        return;
    }
    if (esp_bluedroid_enable() != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid enable failed");
        return;
    }

    if (esp_bt_gap_register_callback(gapCallback) != ESP_OK) {
        ESP_LOGE(TAG, "GAP register failed");
        return;
    }
    if (esp_spp_register_callback(sppCallback) != ESP_OK) {
        ESP_LOGE(TAG, "SPP register failed");
        return;
    }

    esp_spp_cfg_t spp_cfg = {
        .mode = ESP_SPP_MODE_CB,
        .enable_l2cap_ertm = true,
        .tx_buffer_size = 0,
    };
    if (esp_spp_enhanced_init(&spp_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "SPP init failed");
        return;
    }

    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_FIXED;
    esp_bt_pin_code_t pin_code = {};
    pin_code[0] = '0'; pin_code[1] = '0'; pin_code[2] = '0'; pin_code[3] = '0';
    esp_bt_gap_set_pin(pin_type, 4, pin_code);

    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_IO;
    esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &iocap, sizeof(uint8_t));

    bt_initialized = true;
    ESP_LOGI(TAG, "BT MIDI receiver initialized");
}

void BtMidiReceiver::TaskFunction(void *) {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void BtMidiReceiver::Read(uint8_t *buf, uint32_t *len) {
    *len = (uint32_t)ring_read(buf, BT_MIDI_RING_SZ);
}

int BtMidiReceiver::GetDeviceCount() { return device_count; }

const BtDeviceInfo* BtMidiReceiver::GetDevice(int idx) {
    if (idx < 0 || idx >= device_count) return nullptr;
    return &devices[idx];
}

void BtMidiReceiver::Connect(int idx) {
    if (idx < 0 || idx >= device_count) return;
    esp_spp_start_discovery(devices[idx].bda);
}

void BtMidiReceiver::Disconnect() {
    if (bt_connected && bt_spp_handle) {
        esp_spp_disconnect(bt_spp_handle);
    }
}

bool BtMidiReceiver::IsConnected() { return bt_connected; }

bool BtMidiReceiver::IsScanning() { return bt_scanning; }

void BtMidiReceiver::StartScan() {
    if (bt_scanning) return;
    device_count = 0;
    bt_scanning = true;
    esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 30);
    ESP_LOGI(TAG, "Scan started");
}

void BtMidiReceiver::StopScan() {
    if (!bt_scanning) return;
    bt_scanning = false;
    esp_bt_gap_cancel_discovery();
}
