/***************
CTAG TBD >>to be determined<< is an open source eurorack synthesizer module.
(c) 2020 by Robert Manzke. All rights reserved.
Licensed under GPL 3.0.
***************/

#include "BtMidiReceiver.hpp"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"

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
static bool bt_peer_probe = false;   // bounded reconnect probe scan in flight
static bool bt_probe_connect = false; // probe hit seen; connect after teardown
static uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;

// Characteristic value handle for MIDI data (for notification subscription)
static uint16_t midi_val_handle = 0;
static uint16_t midiSvcEnd = 0;        // end handle of the MIDI service (dsc scan)
static uint16_t midi_cccd_handle = 0;  // 0x2902 CCCD handle once discovered
static uint32_t cccd_write_ms = 0;     // when the last CCCD write completed
static bool midi_cccd_written = false; // 0x2902 CCCD write landed

// Persistent peer (last successfully connected device): stored in NVS on
// connect, restored at host sync, retried from TickReconnect() until the
// peer answers. Explicit Disconnect() forgets it.
static int gap_event_cb(struct ble_gap_event *event, void *arg);  // fwd
static bool storedValid = false;
static uint8_t storedType = 0;
static uint8_t storedBda[6];
static bool bt_connecting = false;

static void storePeer(uint8_t type, const uint8_t *bda) {
    nvs_handle_t h;
    if (nvs_open("btmidi", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "atype", type);
    nvs_set_blob(h, "bda", bda, 6);
    nvs_commit(h);
    nvs_close(h);
    storedType = type;
    memcpy(storedBda, bda, 6);
    storedValid = true;
}

static void loadPeer() {
    nvs_handle_t h;
    if (nvs_open("btmidi", NVS_READONLY, &h) != ESP_OK) return;
    uint8_t type = 0;
    size_t sz = sizeof(storedBda);
    if (nvs_get_u8(h, "atype", &type) == ESP_OK &&
        nvs_get_blob(h, "bda", storedBda, &sz) == ESP_OK) {
        storedType = type;
        storedValid = true;
    }
    nvs_close(h);
}

static void forgetPeer() {
    storedValid = false;
    nvs_handle_t h;
    if (nvs_open("btmidi", NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, "atype");
        nvs_erase_key(h, "bda");
        nvs_commit(h);
        nvs_close(h);
    }
}

static bool connectStored() {
    if (!storedValid || bt_connected || bt_connecting) return false;
    ble_addr_t addr;
    addr.type = storedType;
    memcpy(addr.val, storedBda, 6);
    struct ble_gap_conn_params p;
    memset(&p, 0, sizeof(p));
    p.scan_itvl = 0x0010;
    p.scan_window = 0x0010;
    p.itvl_min = 0x0018;
    p.itvl_max = 0x0028;
    p.latency = 0;
    p.supervision_timeout = 0x00C8;
    // Short reconnect attempts with a half-duty connect scan: with the pad
    // off this loop runs forever, and a 100%-duty 30 s procedure starves
    // the link task past the DSP's panel-quiet timeout (DSP OFF, loads
    // refused). The pad either answers in ~1 s or it is not there.
    p.scan_itvl = 0x0020;
    p.scan_window = 0x0010;
    int rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &addr, 3000, &p,
                             gap_event_cb, nullptr);
    if (rc == 0) {
        bt_connecting = true;
        ESP_LOGI(TAG, "auto-reconnect to stored BLE peer...");
    } else {
        ESP_LOGW(TAG, "auto-reconnect rejected: %d", rc);
    }
    return rc == 0;
}

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
// Packet: [ts-hi|0x80][ts-lo] then MIDI messages, each optionally preceded
// by its own 2-byte timestamp. Status bytes ALSO have bit7 set, so the
// decoder tracks message position: at a message boundary a bit7-set byte
// is a timestamp (skipped with its low byte); mid-message it is a status
// (running-status override). System real-time bytes (>= 0xF8) are single
// bytes valid anywhere. Data bytes pass through — the Midi parser on the
// DSP handles running status across packets.
static void parse_ble_midi_notify(const uint8_t *data, int len) {
    int i = 0;
    if (len >= 2 && (data[0] & 0x80)) i = 2;   // leading packet timestamp
    int expected = 0;                          // data bytes left in current msg
    bool expectStatus = true;                  // next bit7-set byte is a status
    while (i < len) {
        uint8_t b = data[i];
        if ((b & 0x80) == 0) {                 // data byte
            ring_write(&b, 1);
            if (expected > 0) expected--;
            i++;
            continue;
        }
        if (b >= 0xF8) {                       // system real-time: 1 byte anywhere
            ring_write(&b, 1);
            i++;
            continue;
        }
        if (expectStatus) {                    // message status byte
            ring_write(&b, 1);
            expected = (b == 0xC0 || b == 0xD0) ? 1 : 2;
            expectStatus = false;
            i++;
            continue;
        }
        if (expected > 0) {                    // status override mid-message
            ring_write(&b, 1);
            expected = (b == 0xC0 || b == 0xD0) ? 1 : 2;
            i++;
            continue;
        }
        i += 2;                                // message complete: timestamp
        expectStatus = true;
    }
}

static int on_chr_discovered(uint16_t conn,
                              const struct ble_gatt_error *error,
                              const struct ble_gatt_chr *chr,
                              void *arg);

static int on_dsc_discovered(uint16_t conn,
                             const struct ble_gatt_error *error,
                             uint16_t chr_val_handle,
                             const struct ble_gatt_dsc *dsc,
                             void *arg);

static int cccd_read_cb(uint16_t conn, const struct ble_gatt_error *error,
                        struct ble_gatt_attr *attr, void *arg);
static int cccd_read_cb(uint16_t conn, const struct ble_gatt_error *error,
                        struct ble_gatt_attr *attr, void *arg) {
    if (error && error->status != 0) {
        ESP_LOGW(TAG, "CCCD read-back FAILED: %d", error->status);
    } else if (attr != nullptr && attr->om != nullptr) {
        uint8_t v[2] = {0, 0};
        uint16_t n = OS_MBUF_PKTLEN(attr->om);
        if (n > sizeof(v)) n = sizeof(v);
        os_mbuf_copydata(attr->om, 0, n, v);
        ESP_LOGW(TAG, "CCCD read-back @%u = %02x %02x (%s)",
                 (unsigned) attr->handle, v[0], v[1],
                 (v[0] == 0x01) ? "subscribed" : "NOT subscribed");
    }
    return 0;
}

static int cccd_write_cb(uint16_t conn, const struct ble_gatt_error *error,
                         struct ble_gatt_attr *attr, void *arg) {
    if (error && error->status != 0) {
        ESP_LOGE(TAG, "CCCD write REJECTED at ATT level: %d", error->status);
    } else {
        midi_cccd_written = true;
        ESP_LOGI(TAG, "CCCD write accepted — notifications enabled");
        // The SMC-PAD may only start its notification stream after the
        // client writes to it (the Ableton script paces LED writes
        // constantly). Poke it with a BLE-MIDI keepalive packet.
        uint8_t ka[3] = {0x80, 0x00, 0xF8};   // ts header + MIDI clock tick
        int wrc = ble_gattc_write_flat(conn, midi_val_handle, ka, sizeof(ka),
                                       nullptr, nullptr);
        ESP_LOGI(TAG, "keepalive write rc=%d", wrc);
        // Read the CCCD back: does the pad actually hold the subscription?
        ble_gattc_read(conn, midi_cccd_handle, cccd_read_cb, nullptr);
        cccd_write_ms = (uint32_t) (xTaskGetTickCount() * portTICK_PERIOD_MS);
    }
    return 0;
}

static int on_svc_discovered(uint16_t conn,
                              const struct ble_gatt_error *error,
                              const struct ble_gatt_svc *svc,
                              void *arg) {
    if (error->status == 0 && svc != nullptr) {
        char uuid_str[37];
        ble_uuid_to_str(&svc->uuid.u, uuid_str);
        if (ble_uuid_cmp(&svc->uuid.u, &MIDI_SVC_UUID.u) == 0) {
            ESP_LOGI(TAG, "Found MIDI service (%u-%u), discovering characteristics",
                     svc->start_handle, svc->end_handle);
            midiSvcEnd = svc->end_handle;
            int rc = ble_gattc_disc_all_chrs(conn, svc->start_handle, svc->end_handle,
                                             on_chr_discovered, nullptr);
            if (rc != 0) ESP_LOGE(TAG, "Failed to start chr discovery: %d", rc);
        }
    } else if (error->status == BLE_HS_EDONE) {
        ESP_LOGI(TAG, "Service discovery complete");
    } else {
        ESP_LOGE(TAG, "Service discovery error: %d", error->status);
    }
    return 0;
}

static int on_chr_discovered(uint16_t conn,
                              const struct ble_gatt_error *error,
                              const struct ble_gatt_chr *chr,
                              void *arg) {
    if (error->status == 0 && chr != nullptr) {
        char uuid_str[37];
        ble_uuid_to_str(&chr->uuid.u, uuid_str);
        // Keep this visible: the CCCD subscribe depends on finding this
        // characteristic and its 0x2902 descriptor.
        ESP_LOGI(TAG, "chr %s def=%u val=%u props=0x%02x",
                 uuid_str, chr->def_handle, chr->val_handle, chr->properties);
        // Match the Apple BLE-MIDI characteristic UUID; fall back to the
        // first (notify-capable) characteristic in the MIDI service —
        // vendors ship variants like the M-VAVE's ...f2669d106bf3.
        if (ble_uuid_cmp(&chr->uuid.u, &MIDI_CHR_UUID.u) == 0) {
            ESP_LOGI(TAG, "Found Apple BLE-MIDI characteristic");
        } else if (midi_val_handle == 0 && (chr->properties & BLE_GATT_CHR_F_NOTIFY)) {
            ESP_LOGI(TAG, "Using device-specific MIDI characteristic");
        } else {
            return 0; // already subscribed
        }
        midi_val_handle = chr->val_handle;
    } else if (error->status == BLE_HS_EDONE) {
        ESP_LOGI(TAG, "Characteristic discovery complete");
        if (midi_val_handle != 0) {
            // Discover the 0x2902 CCCD descriptor and write it with a
            // RELIABLE write — write-without-response CCCD pokes are
            // silently ignored by some peripherals (observed on M-VAVE).
            int rc = ble_gattc_disc_all_dscs(conn, midi_val_handle, midiSvcEnd,
                                             on_dsc_discovered, nullptr);
            if (rc != 0) ESP_LOGE(TAG, "dsc discovery failed: %d", rc);
        }
    } else {
        ESP_LOGE(TAG, "Characteristic discovery error: %d", error->status);
    }
    return 0;
}

static int on_dsc_discovered(uint16_t conn,
                             const struct ble_gatt_error *error,
                             uint16_t chr_val_handle,
                             const struct ble_gatt_dsc *dsc,
                             void *arg) {
    if (error->status == 0 && dsc != nullptr) {
        char uuid_str[37];
        ble_uuid_to_str(&dsc->uuid.u, uuid_str);
        ESP_LOGI(TAG, "dsc 0x%04x %s", dsc->handle, uuid_str);
        if (dsc->uuid.u.type == BLE_UUID_TYPE_16 &&
            dsc->uuid.u16.value == 0x2902) {
            midi_cccd_handle = dsc->handle;
            uint8_t val[2] = {0x01, 0x00};   // notify enable, little-endian
            // Reliable write WITH response — write-without-response CCCD
            // pokes are silently ignored by some peripherals (M-VAVE).
            // The callback surfaces the ATT-level status: rc=0 only means
            // the procedure started.
            struct os_mbuf *om = ble_hs_mbuf_from_flat(val, sizeof(val));
            int rc = om ? ble_gattc_write(conn, dsc->handle, om,
                                          cccd_write_cb, nullptr) : BLE_HS_ENOMEM;
            ESP_LOGI(TAG, "CCCD 0x%04x write started rc=%d", dsc->handle, rc);
        }
    } else if (error->status == BLE_HS_EDONE) {
        if (midi_cccd_written) {
            ESP_LOGI(TAG, "Subscribed to MIDI notifications");
        } else if (midi_val_handle != 0) {
            // No 0x2902 found in range — legacy fallback: CCCD at val+1.
            midi_cccd_handle = midi_val_handle + 1;
            uint8_t val[2] = {0x01, 0x00};
            struct os_mbuf *om = ble_hs_mbuf_from_flat(val, sizeof(val));
            int rc = om ? ble_gattc_write(conn, midi_val_handle + 1, om,
                                          cccd_write_cb, nullptr) : BLE_HS_ENOMEM;
            ESP_LOGW(TAG, "no 0x2902 dsc; fallback CCCD val+1 write started rc=%d", rc);
        }
        ESP_LOGI(TAG, "Descriptor discovery complete");
    } else {
        ESP_LOGE(TAG, "Descriptor discovery error: %d", error->status);
    }
    return 0;
}

static int gap_event_cb(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT: {
        if (event->connect.status == 0) {
            conn_handle = event->connect.conn_handle;
            bt_connected = true;
            bt_connecting = false;
            bt_scanning = false;
            ESP_LOGI(TAG, "Connected, discovering services");
            // Remember the peer for persistent reconnection.
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->connect.conn_handle, &desc) == 0)
                storePeer(desc.peer_id_addr.type, desc.peer_id_addr.val);
            ble_gattc_disc_all_svcs(conn_handle, on_svc_discovered, nullptr);
        } else {
            ESP_LOGE(TAG, "Connection failed: %d", event->connect.status);
            bt_connected = false;
            bt_connecting = false;   // TickReconnect() will retry
        }
        return 0;
    }
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected");
        conn_handle = BLE_HS_CONN_HANDLE_NONE;
        bt_connected = false;
        bt_connecting = false;       // stored peer (if any) auto-reconnects
        midi_val_handle = 0;
        midiSvcEnd = 0;
        midi_cccd_handle = 0;
        midi_cccd_written = false;
        return 0;
    case BLE_GAP_EVENT_DISC: {
        const struct ble_gap_disc_desc &disc = event->disc;
        // Peer probe hit: the stored pad is advertising again — stop the
        // probe and connect; the procedure completes immediately because
        // the peer is really there. The pad must advertise its identity
        // address (it does — the old blind reconnect by stored address
        // only worked under that same assumption).
        if (bt_peer_probe && storedValid && !bt_connected && !bt_connecting &&
            disc.addr.type == storedType &&
            memcmp(disc.addr.val, storedBda, 6) == 0) {
            bt_peer_probe = false;
            // The disc teardown completes asynchronously (DISC_COMPLETE fires
            // after this callback) — connecting here races it and the first
            // attempt is rejected with BLE_HS_EBUSY. Defer one event — unless
            // the scan already ended on its own (cancel fails: no event will
            // come), in which case connect right away.
            if (ble_gap_disc_cancel() == 0) {
                bt_probe_connect = true;
            } else {
                connectStored();
            }
            return 0;
        }
        if (bt_peer_probe) return 0;   // probe results stay out of the UI list
        // Manually parse AD data for device name (type 0x08=short, 0x09=complete)
        char nameBuf[32];
        const char *advName = nullptr;
        {
            int off = 0;
            const uint8_t *d = disc.data;
            int dlen = disc.length_data;
            while (off + 1 < dlen) {
                int fieldLen = d[off];
                if (fieldLen == 0 || off + 1 + fieldLen > dlen) break;
                int type = d[off + 1];
                if (type == 0x08 || type == 0x09) {
                    int nameLen = fieldLen - 1;
                    if (nameLen > 31) nameLen = 31;
                    memcpy(nameBuf, &d[off + 2], nameLen);
                    nameBuf[nameLen] = '\0';
                    advName = nameBuf;
                    break;
                }
                off += 1 + fieldLen;
            }
        }
        // Deduplicate: check if address already in list
        for (int i = 0; i < deviceCount; i++) {
            if (memcmp(devices[i].bda, disc.addr.val, 6) == 0) {
                // Update name from scan response (may carry name not in adv)
                if (advName) {
                    int copyLen = strlen(advName) < 31 ? strlen(advName) : 31;
                    memcpy(devices[i].name, advName, copyLen);
                    devices[i].name[copyLen] = '\0';
                }
                return 0;
            }
        }
        // New device
        if (deviceCount >= MAX_DEVICES) return 0;
        BtDeviceInfo &d = devices[deviceCount];
        if (advName) {
            int copyLen = strlen(advName) < 31 ? strlen(advName) : 31;
            memcpy(d.name, advName, copyLen);
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
        bt_peer_probe = false;
        if (bt_probe_connect) {
            // Probe hit + scan teardown finished — now the connect lands.
            bt_probe_connect = false;
            connectStored();
        }
        if (bt_scanning) {
            ESP_LOGI(TAG, "Scan complete, %d devices found", deviceCount);
            bt_scanning = false;
        }
        return 0;
    case BLE_GAP_EVENT_NOTIFY_RX: {
        const struct os_mbuf *om = event->notify_rx.om;
        if (om && om->om_len > 0) {
            // 1 Hz probe: is the pad notifying at all? (midi-death split)
            static uint32_t lastNotifyLogMs = 0;
            uint32_t nowMs = (uint32_t) (xTaskGetTickCount() * portTICK_PERIOD_MS);
            if (nowMs - lastNotifyLogMs > 1000) {
                lastNotifyLogMs = nowMs;
                ESP_LOGI(TAG, "notify rx %d B", om->om_len);
            }
            parse_ble_midi_notify(om->om_data, om->om_len);
        }
        return 0;
    }
    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        // The SMC-PAD initiates pairing right after discovery; ignoring it
        // stalls its stack and notifications never start. Auto-accept
        // numeric comparison (Just Works on IO-cap-none devices) and pass
        // the rest through so the procedure completes.
        struct ble_sm_io pkey;
        memset(&pkey, 0, sizeof(pkey));
        pkey.action = event->passkey.params.action;
        if (pkey.action == BLE_SM_IOACT_NUMCMP) pkey.numcmp_accept = 1;
        int rc = ble_sm_inject_io(event->connect.conn_handle, &pkey);
        ESP_LOGI(TAG, "passkey action %d injected rc=%d", pkey.action, rc);
        return 0;
    }
    case BLE_GAP_EVENT_ENC_CHANGE:
        // Pairing/encryption settled: pairing resets CCCD state, so
        // re-subscribe once the connection is encrypted.
        ESP_LOGI(TAG, "enc change: status=%d", event->enc_change.status);
        if (event->enc_change.status == 0 && midi_val_handle != 0 &&
            midi_cccd_handle != 0) {
            uint8_t val[2] = {0x01, 0x00};
            struct os_mbuf *om = ble_hs_mbuf_from_flat(val, sizeof(val));
            int rc = om ? ble_gattc_write(conn_handle, midi_cccd_handle, om,
                                          nullptr, nullptr) : BLE_HS_ENOMEM;
            ESP_LOGI(TAG, "CCCD re-subscribe after pairing rc=%d", rc);
        }
        return 0;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGD(TAG, "MTU updated: %d", event->mtu.value);
        return 0;
    default:
        // Probe: log each event TYPE once (connection-parameter updates,
        // keepalives, anything the peer sends we don't explicitly handle).
        static uint8_t seenEvents[256] = {0};
        if (event->type < 256 && !seenEvents[event->type]) {
            seenEvents[event->type] = 1;
            ESP_LOGI(TAG, "gap event type %d (first sighting)", event->type);
        }
        return 0;
    }
}

static void on_sync(void) {
    ESP_LOGI(TAG, "NimBLE host synced with controller");
    // Restore the persistent peer as soon as the host is ready, but do NOT
    // blind-connect here: with the pad off that is one more connect into
    // the void (TickReconnect's probe handles the reconnection).
    loadPeer();
}

static void host_task(void *param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void BtMidiReceiver::Init() {
    // The precompiled C3 BT controller needs the ECO3 ROM BT helpers
    // (chip revision >= 3, i.e. v0.3+ — this bench's v0.4 qualifies).
    // IDF 5.5.4 links esp32c3.rom.eco3*_bt_funcs.ld only when REV_MIN >= 3,
    // so boards older than v0.3 cannot run BLE here; skip them cleanly.
    esp_chip_info_t ci;
    esp_chip_info(&ci);
    if (ci.revision < 3) {
        ESP_LOGW(TAG, "BLE-MIDI unavailable: chip rev %u < 3 (ECO3 ROM required)",
                 ci.revision);
        return;
    }
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_flash_erase());
        nvs_flash_init();
    }

    ESP_LOGI(TAG, "heap before NimBLE: free=%u largest=%u",
             (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    nimble_port_init();
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    nimble_port_freertos_init(host_task);

    ESP_LOGI(TAG, "BLE MIDI initialized (central mode)");
}

void BtMidiReceiver::Read(uint8_t *buf, uint32_t *len) {
    *len = (uint32_t)ring_read(buf, BT_MIDI_RING_SZ);
}

// Called from the midi relay task (~10 ms cadence): every 3 s, run a
// bounded 2 s advertisement probe for the stored peer. The pad only
// advertises while disconnected, so the probe is the presence detector:
// with the pad off nothing is heard and no procedure is ever pending
// (blind connect attempts into the void were starving the single core
// and taking the board down). Seeing the pad triggers one connect that
// completes immediately.
void BtMidiReceiver::TickReconnect() {
    static uint32_t lastTry = 0;
    // Late re-subscribe: if the pad's engine start clears the CCCD after
    // our early write, this re-arms it 2.5 s in and reads it back.
    static bool reSubscribed = false;
    if (bt_connected && midi_cccd_written && midi_cccd_handle != 0 &&
        !reSubscribed &&
        (uint32_t) (xTaskGetTickCount() * portTICK_PERIOD_MS) - cccd_write_ms > 2500) {
        reSubscribed = true;
        ESP_LOGW(TAG, "late CCCD re-subscribe @%u", (unsigned) midi_cccd_handle);
        struct os_mbuf *om = ble_hs_mbuf_from_flat((const uint8_t *) "\x01\x00", 2);
        int rc = om ? ble_gattc_write(conn_handle, midi_cccd_handle, om,
                                      cccd_write_cb, nullptr) : BLE_HS_ENOMEM;
        if (rc != 0) ESP_LOGW(TAG, "late re-subscribe rc=%d", rc);
    }
    // A probe hit deferred its connect but the DISC_COMPLETE event was lost
    // (scan expired in the same instant) — recover here.
    if (bt_probe_connect && !bt_connected && !bt_connecting) {
        bt_probe_connect = false;
        connectStored();
        return;
    }
    if (!storedValid || bt_connected || bt_connecting || bt_scanning) return;
    uint32_t now = xTaskGetTickCount();
    if (now - lastTry < pdMS_TO_TICKS(3000)) return;
    lastTry = now;
    struct ble_gap_disc_params params = {
        .itvl = 0x0020,          // 20 ms interval / 10 ms window: half-duty,
        .window = 0x0010,        // the probe must not hog the single core
        .filter_policy = BLE_HCI_CONN_FILT_NO_WL,
        .limited = 0,
        .passive = 0,
        .filter_duplicates = 0,
    };
    bt_peer_probe = true;
    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, 2000, &params, gap_event_cb, nullptr);
    if (rc != 0) {
        bt_peer_probe = false;
        if (rc != BLE_HS_EBUSY) ESP_LOGW(TAG, "peer probe scan failed: %d", rc);
    }
}

// Client → pad write on the MIDI characteristic (keepalive/clock ticks —
// the SMC-PAD starts notifying once the client talks to it).
void BtMidiReceiver::SendKeepalive(const uint8_t *data, uint32_t len) {
    if (!bt_connected || midi_val_handle == 0) return;
    ble_gattc_write_flat(conn_handle, midi_val_handle, data,
                         (uint16_t) len, nullptr, nullptr);
}

bool BtMidiReceiver::IsConnected() { return bt_connected; }
bool BtMidiReceiver::IsScanning() { return bt_scanning; }

int BtMidiReceiver::GetDeviceCount() { return deviceCount; }

const BtDeviceInfo* BtMidiReceiver::GetDevice(int idx) {
    if (idx < 0 || idx >= deviceCount) return nullptr;
    return &devices[idx];
}

void BtMidiReceiver::StartScan() {
    // Scanning while connected (or mid-connect) is allowed: drop the
    // current connection first. The stored peer is kept — a new successful
    // connection replaces it.
    if (bt_connecting) ble_gap_conn_cancel();
    if (bt_connected)
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    bt_connected = false;
    bt_connecting = false;
    bt_peer_probe = false;
    bt_scanning = true;
    deviceCount = 0;
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
    forgetPeer();   // explicit disconnect = forget the peer
    ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
}
