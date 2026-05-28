/***************
CTAG TBD >>to be determined<< is an open source eurorack synthesizer module.
(c) 2020 by Robert Manzke. All rights reserved.
Licensed under GPL 3.0.
***************/

#include "BtMidiReceiver.hpp"
#include "esp_log.h"
#include "nvs_flash.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_uuid.h"

using namespace CTAG::DRIVERS;

static const char *TAG = "BLE_MIDI";

// BLE MIDI service UUID: 03B80E5A-EDE8-4B33-A751-6CE34EC4C700
static const ble_uuid128_t MIDI_SVC_UUID = BLE_UUID128_INIT(
    0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7,
    0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03);
static const ble_uuid128_t MIDI_CHR_UUID = BLE_UUID128_INIT(
    0x02, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7,
    0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03);

// Ring buffer
#define BT_MIDI_RING_SZ 2048
static uint8_t ring[BT_MIDI_RING_SZ];
static volatile int ring_wr = 0;
static volatile int ring_rd = 0;

// Device list (discovered during scan)
#define MAX_DEVICES 16
static BtDeviceInfo devices[MAX_DEVICES];
static int deviceCount = 0;

// Connection state
static bool bt_connected = false;
static bool bt_scanning = false;
static uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;

// Characteristic value handle for MIDI data (for notification subscription)
static uint16_t midi_val_handle = 0;

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

// Parse Apple BLE-MIDI notification data.
// Format: [ts_high | 0x80] [ts_low] [MIDI bytes…]
// Multiple groups can appear in one notification.
// We skip the 2-byte timestamp headers and write raw MIDI bytes.
static void parse_ble_midi_notify(const uint8_t *data, int len) {
    int i = 0;
    while (i < len) {
        if ((data[i] & 0x80) && i + 2 <= len) {
            // Timestamp header: skip 2 bytes
            i += 2;
        } else {
            // Raw MIDI byte
            int start = i;
            while (i < len && !(data[i] & 0x80)) i++;
            ring_write(data + start, i - start);
        }
    }
}

static int on_chr_discovered(uint16_t conn,
                              const struct ble_gatt_error *error,
                              const struct ble_gatt_chr *chr,
                              void *arg);

static int on_svc_discovered(uint16_t conn,
                              const struct ble_gatt_error *error,
                              const struct ble_gatt_svc *svc,
                              void *arg) {
    if (error->status == 0 && svc != nullptr) {
        if (ble_uuid_cmp(&svc->uuid.u, &MIDI_SVC_UUID.u) == 0) {
            ESP_LOGI(TAG, "Found MIDI service, discovering characteristics");
            ble_gattc_disc_all_chrs(conn, svc->start_handle, svc->end_handle,
                                    on_chr_discovered, nullptr);
        }
    }
    return 0;
}

static int on_chr_discovered(uint16_t conn,
                              const struct ble_gatt_error *error,
                              const struct ble_gatt_chr *chr,
                              void *arg) {
    if (error->status == 0 && chr != nullptr) {
        if (ble_uuid_cmp(&chr->uuid.u, &MIDI_CHR_UUID.u) == 0) {
            ESP_LOGI(TAG, "Found MIDI characteristic");
            midi_val_handle = chr->val_handle;
            // Subscribe to notifications: write 0x0001 to CCCD
            uint8_t val[2] = {0x01, 0x00};
            int rc = ble_gattc_write_flat(conn, chr->val_handle + 1,
                                          val, sizeof(val),
                                          nullptr, nullptr);
            if (rc == 0) {
                ESP_LOGI(TAG, "Subscribed to MIDI notifications");
            } else {
                ESP_LOGE(TAG, "Subscribe failed: %d", rc);
            }
        }
    }
    return 0;
}

static int gap_event_cb(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT: {
        if (event->connect.status == 0) {
            conn_handle = event->connect.conn_handle;
            bt_connected = true;
            bt_scanning = false;
            ESP_LOGI(TAG, "Connected, discovering services");
            ble_gattc_disc_all_svcs(conn_handle, on_svc_discovered, nullptr);
        } else {
            ESP_LOGE(TAG, "Connection failed: %d", event->connect.status);
            bt_connected = false;
        }
        return 0;
    }
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected");
        conn_handle = BLE_HS_CONN_HANDLE_NONE;
        bt_connected = false;
        midi_val_handle = 0;
        return 0;
    case BLE_GAP_EVENT_DISC: {
        const struct ble_gap_disc_desc &disc = event->disc;
        if (deviceCount >= MAX_DEVICES) return 0;
        // Parse AD data for device name
        struct ble_hs_adv_fields fields;
        int rc = ble_hs_adv_parse_fields(&fields, disc.data, disc.length_data);
        BtDeviceInfo &d = devices[deviceCount];
        if (rc == 0 && fields.name_len > 0) {
            int copyLen = fields.name_len < 31 ? fields.name_len : 31;
            memcpy(d.name, fields.name, copyLen);
            d.name[copyLen] = '\0';
        } else {
            snprintf(d.name, sizeof(d.name), "BLE-%02X%02X%02X%02X%02X%02X",
                     disc.addr.val[5], disc.addr.val[4], disc.addr.val[3],
                     disc.addr.val[2], disc.addr.val[1], disc.addr.val[0]);
        }
        memcpy(d.bda, disc.addr.val, 6);
        deviceCount++;
        return 0;
    }
    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGI(TAG, "Scan complete, %d devices found", deviceCount);
        bt_scanning = false;
        return 0;
    case BLE_GAP_EVENT_NOTIFY_RX: {
        const struct os_mbuf *om = event->notify_rx.om;
        if (om && om->om_len > 0) {
            parse_ble_midi_notify(om->om_data, om->om_len);
        }
        return 0;
    }
    case BLE_GAP_EVENT_MTU:
        ESP_LOGD(TAG, "MTU updated: %d", event->mtu.value);
        return 0;
    default:
        return 0;
    }
}

static void on_sync(void) {
    ESP_LOGI(TAG, "NimBLE host synced with controller");
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
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    nimble_port_freertos_init(host_task);

    ESP_LOGI(TAG, "BLE MIDI initialized (central mode)");
}

void BtMidiReceiver::Read(uint8_t *buf, uint32_t *len) {
    *len = (uint32_t)ring_read(buf, BT_MIDI_RING_SZ);
}

bool BtMidiReceiver::IsConnected() { return bt_connected; }
bool BtMidiReceiver::IsScanning() { return bt_scanning; }

int BtMidiReceiver::GetDeviceCount() { return deviceCount; }

const BtDeviceInfo* BtMidiReceiver::GetDevice(int idx) {
    if (idx < 0 || idx >= deviceCount) return nullptr;
    return &devices[idx];
}

void BtMidiReceiver::StartScan() {
    if (bt_scanning || bt_connected) return;
    deviceCount = 0;
    bt_scanning = true;
    struct ble_gap_disc_params params = {
        .itvl = 0,
        .window = 0,
        .filter_policy = BLE_HCI_CONN_FILT_NO_WL,
        .limited = 0,
        .passive = 0,
        .filter_duplicates = 0,
    };
    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &params, gap_event_cb, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "Scan start failed: %d", rc);
        bt_scanning = false;
    } else {
        ESP_LOGI(TAG, "Scanning for BLE MIDI devices...");
    }
}

void BtMidiReceiver::StopScan() {
    if (!bt_scanning) return;
    ble_gap_disc_cancel();
    bt_scanning = false;
}

void BtMidiReceiver::Connect(int idx) {
    if (bt_connected || idx < 0 || idx >= deviceCount) return;
    StopScan();
    ble_addr_t addr;
    addr.type = BLE_ADDR_PUBLIC;
    memcpy(addr.val, devices[idx].bda, 6);
    struct ble_gap_conn_params params;
    memset(&params, 0, sizeof(params));
    params.scan_itvl = 0x0010;
    params.scan_window = 0x0010;
    params.itvl_min = 0x0018;
    params.itvl_max = 0x0028;
    params.latency = 0;
    params.supervision_timeout = 0x00C8;
    params.min_ce_len = 0;
    params.max_ce_len = 0;
    int rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &addr, 30000, &params, gap_event_cb, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "Connect failed: %d", rc);
    } else {
        ESP_LOGI(TAG, "Connecting to %s...", devices[idx].name);
    }
}

void BtMidiReceiver::Disconnect() {
    if (!bt_connected) return;
    ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
}
