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

// Panel<->DSP link endpoint (doc/ui-split-sdd.md §4/§5.2). Owns UART1 —
// the former MIDI DIN port, GPIO18 TX / GPIO19 RX — at 921600 8N1. The
// midiuart driver installed by Midi.cpp's static instance is deleted and
// reinstalled with big rings; Midi::Update() is dormant on BBA so nothing
// else touches the port (the DIN side of MIDI moves to the panel at P4,
// then this task also becomes Midi::Update()'s caller and the midiIn/midiOut
// relay).
//
// Transport and dispatch are the proven bench shape: STX/ETX JSON frames,
// /debug/* served by DebugUI::handleCommand (one dispatcher for both
// transports), /link/* handled here. Pushes: boot metadata after the
// handshake, a 10 Hz stream (VU/marbles/CPU/SD), and on-change copies of
// the big JSON blobs — detected by hashing the model's cached buffers every
// stream tick (poll-and-push), so every mutation source (panel link, bench
// scripts, the A1S-local UI until P6) is covered without touching SPManager.
//
// Stack is 8 KB on purpose: /debug/setActivePlugin runs the full plugin
// delete+create chain inline on this task (4 KB overflowed, see
// radio-free-dsp-test.md §bugs).

#pragma once

#include <string>
#include <cstdint>

namespace CTAG {
    namespace CTRL {
        class LinkEndpoint final {
        public:
            LinkEndpoint() = delete;
            // Take over UART1, spawn the link task. Safe to call once.
            static void Start();
        // True after a version handshake and until the panel goes quiet.
        static bool IsOnline();
        // Debug-only: retune the link UART at runtime (baud sweep for the
        // handshake hunt). False when the rate is out of range.
        static bool SetBaud(int baud);
        };
    }
}
