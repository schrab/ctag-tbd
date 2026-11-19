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

#pragma once

#include <cstdint>
#include <cstring>

namespace CTAG::DRIVERS {

struct BtDeviceInfo {
    char name[32];
    uint8_t bda[6];
};

class BtMidiReceiver {
public:
    static void Init();
    static void Read(uint8_t *buf, uint32_t *len);
    // Retry a stored peer every ~3 s while idle (call from a periodic task).
    static void TickReconnect();
    // Write a raw packet TO the BLE-MIDI characteristic (keepalive/clock).
    static void SendKeepalive(const uint8_t *data, uint32_t len);
    static int GetDeviceCount();
    static const BtDeviceInfo* GetDevice(int idx);
    static void Connect(int idx);
    static void Disconnect();
    static bool IsConnected();
    static bool IsScanning();
    static void StartScan();
    static void StopScan();
    // Bench receive-path counters (2026-11-19 MIDI-chain bisection):
    // notifications delivered by NimBLE, BLE-MIDI payload bytes parsed,
    // bytes handed out by Read. If connected is true but notifyRx stays
    // 0 while the pad plays, the pad is not sending notifications.
    static uint32_t GetNotifyRxCount();
    static uint32_t GetParsedByteCount();
    static uint32_t GetReadByteCount();
    static uint32_t GetKeepaliveOk();
    static uint32_t GetKeepaliveFail();
    static uint32_t GetKeepaliveSkip();
    // Delete the stored peer's bond and drop the connection — the
    // auto-reconnect then pairs as a brand-new central, which re-arms
    // pads whose app layer never restarts the stream for a bonded
    // central reconnect.
    static void RepairBond();
};

}

