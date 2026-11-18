/***************
CTAG TBD >>to be determined<< is an open source eurorack synthesizer module.

A project conceived within the Creative Technologies Arbeitsgruppe of
Kiel University of Applied Sciences: https://www.creative-technologies.de

(c) 2024 by Robert Manzke. All rights reserved.

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
#include "helpers/ctagSlotBounds.hpp"
#include "SeqEngineCore.hpp"

namespace CTAG::DRIVERS {

// SEQ modulators: 2x rhythmic GATE + 2x generative ARP running alongside
// Marbles (no engine switching). Same contract shape as MarblesEngine:
// scalar getters/setters (UIMenu/link task), Process on the audio task.
// Trig outputs are ACTIVE-LOW (0 = active, 1 = inactive, idle 1) and every
// owned slot is written every block; CV/trig writes funnel through
// ClampCVSlot/ClampTrigSlot (audio-task stack safety).
//
// Outputs (spec §4.2, defaults):
//   cv 102/103 = GAT.1Level/GAT.2Level   gate level, slewed 0..1
//   cv 104/105 = SEQ.1Note/SEQ.2Note     arp pitch, 1 V/oct
//   trig 43/44 = GAT.1Trig/GAT.2Trig     gate window
//   trig 45/46 = arp gate window (55 %, 100 % on slide steps)
//   trig 47/48 = arp accent (0 on accent steps)
//   trig 49/50 = arp slide (0 on slide steps)
//
// Clock: per-source Global / Internal / MIDI. Global follows the transport
// clock source (ModTransport::GetGlobalClockMidi); Internal uses the
// fractional PPQ accumulator at the global BPM; MIDI consumes integer PPQ
// ticks from ModTransport::TicksThisBlock and freezes while stopped.
class SeqEngine {
public:
    static void Init();   // InitDefaults + LoadConfig
    static void InitDefaults();
    // Audio task, once per block. dt in seconds.
    static void Process(float *cv, uint8_t *trig, float dt);
    // Live output word for the panel strip (see spec §4.2 bit map).
    static uint32_t GetOutputSnapshot();

    // --- gates (i = 0/1) ---
    static int GetGateMode(int i);     static void SetGateMode(int i, int v);   // 0..18
    static int GetGateDens(int i);     static void SetGateDens(int i, int v);   // 1..16
    static int GetGateSync(int i);     static void SetGateSync(int i, int v);   // 0..100 %
    static float GetGateLen(int i);    static void SetGateLen(int i, float v);  // 0..1
    static int GetGateClk(int i);      static void SetGateClk(int i, int v);    // 0 global 1 int 2 midi
    static int GetGateDiv(int i);      static void SetGateDiv(int i, int v);    // kDivisions idx
    static int GetGateTrigSlot(int i); static void SetGateTrigSlot(int i, int s);
    static int GetGateCvSlot(int i);   static void SetGateCvSlot(int i, int s);
    static uint32_t GetGateMask(int i);   // pattern preview for the panel

    // --- arps (i = 0/1) ---
    // Algo getter/setter use the panel encoding: v = algo*3 + (range-1),
    // algo 0..4, range 1..3 -> v 0..14.
    // Internal companion output: VARG Chord progression voicing (0..16) for
    // ARP i, or -1 if that ARP is not running a VARG Chord algorithm.
    static int GetVargVoicing(int i);
    static int GetArpAlgo(int i);      static void SetArpAlgo(int i, int v);
    static int GetArpP(int i, int k);  static void SetArpP(int i, int k, int v); // 0..100
    static int GetArpGlide(int i);     static void SetArpGlide(int i, int v);   // 0..100 ms
    static int GetArpOct(int i);       static void SetArpOct(int i, int v);     // -2..+2 octaves
    static int GetArpClk(int i);       static void SetArpClk(int i, int v);
    static int GetArpDiv(int i);       static void SetArpDiv(int i, int v);
    static int GetArpPitchSlot(int i); static void SetArpPitchSlot(int i, int s);
    static int GetArpGateSlot(int i);  static void SetArpGateSlot(int i, int s);
    static int GetArpAccSlot(int i);   static void SetArpAccSlot(int i, int s);
    static int GetArpSlideSlot(int i); static void SetArpSlideSlot(int i, int s);
    // 16-step gate pattern of the current arp pattern, bit (15-step) set on
    // gated steps — same convention as GetGateMask; feeds the panel strip.
    static uint32_t GetArpGateMask(int i);
    // Accent / slide patterns (same bit convention) for the detail strip.
    static uint32_t GetArpAccMask(int i);
    static uint32_t GetArpSlideMask(int i);
    // Current gate step 0..15 (the strip's playhead; rides the 25 Hz stream).
    static int GetGateStep(int i);

    // --- shared across both arps ---
    static int GetRoot();     static void SetRoot(int v);      // pitch class 0..11
    static int GetRootMode(); static void SetRootMode(int v);  // 0 fixed 1 W7 2 neo
    static int GetNeoOp();    static void SetNeoOp(int v);     // 0..7
    static int GetScale();    static void SetScale(int v);     // 0..19

    // --- persistence ("seq" section of mod-config.jsn) ---
    static void LoadConfig();
    static void SaveConfig();
    // Flat spec §4.4 JSON over the whole engine state (root stored as the
    // MIDI note 48+pc per the spec example).
    static void SerializeConfig(char *dst, int cap);
    static bool DeserializeConfig(const char *json);
};

} // namespace CTAG::DRIVERS
