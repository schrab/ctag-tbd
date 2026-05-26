/***************
CTAG TBD >>to be determined<< is an open source eurorack synthesizer module.
(c) 2020 by Robert Manzke. All rights reserved.
Licensed under GPL 3.0.
***************/

#include "BtMidiReceiver.hpp"
#include "esp_log.h"
#include "nvs_flash.h"

// NimBLE headers
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

using namespace CTAG::DRIVERS;

static const char *TAG = "BLE_MIDI";

// BLE MIDI service UUID: 03B80E5A-EDE8-4B33-A751-6CE34EC4C700
static const ble_uuid128_t MIDI_SVC_UUID = BLE_UUID128_INIT(
    0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7,
    0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03);
static const ble_uuid128_t MIDI_CHR_UUID = BLE_UUID128_INIT(
    0x02, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7,
    0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03);

#define BT_MIDI_RING_SZ 2048
static uint8_t ring[BT_MIDI_RING_SZ];
static volatile int ring_wr = 0;
static volatile int ring_rd = 0;
static bool bt_connected = false;
static uint16_t conn_handle = 0;

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

static int midi_acc_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg) {
    ring_write(ctxt->om->om_data, ctxt->om->om_len);
    return 0;
}

static const struct ble_gatt_chr_def gatt_chrs[] = {
    {
        .uuid = (ble_uuid_t *)&MIDI_CHR_UUID,
        .access_cb = midi_acc_write_cb,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_NOTIFY,
    },
    {0}
};

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = (ble_uuid_t *)&MIDI_SVC_UUID,
        .characteristics = (struct ble_gatt_chr_def *)gatt_chrs,
    },
    {0}
};

static int gap_event_cb(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            bt_connected = true;
            conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "BLE MIDI connected");
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        bt_connected = false;
        ESP_LOGI(TAG, "BLE MIDI disconnected, re-advertising");
        ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                          NULL, gap_event_cb, NULL);
        return 0;
    default:
        return 0;
    }
}

static void host_task(void *param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void BtMidiReceiver::Init() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_flash_erase());
        nvs_flash_init();
    }

    nimble_port_init();
    ble_svc_gap_device_name_set("CTAG TBD");
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);
    ble_hs_cfg.reset_cb = NULL;
    ble_hs_cfg.sync_cb = NULL;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                      NULL, gap_event_cb, NULL);

    nimble_port_freertos_init(host_task);

    ESP_LOGI(TAG, "BLE MIDI initialized — advertise as 'CTAG TBD'");
}

void BtMidiReceiver::TaskFunction(void *param) {
    while (true) vTaskDelay(pdMS_TO_TICKS(1000));
}

void BtMidiReceiver::Read(uint8_t *buf, uint32_t *len) {
    *len = (uint32_t)ring_read(buf, BT_MIDI_RING_SZ);
}

bool BtMidiReceiver::IsConnected() { return bt_connected; }
bool BtMidiReceiver::IsScanning() { return false; }
int  BtMidiReceiver::GetDeviceCount() { return 0; }
const BtDeviceInfo* BtMidiReceiver::GetDevice(int) { return nullptr; }
void BtMidiReceiver::Connect(int) {}
void BtMidiReceiver::Disconnect() {}
void BtMidiReceiver::StartScan() {}
void BtMidiReceiver::StopScan() {}
