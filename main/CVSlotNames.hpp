#pragma once

#include <cstdint>

// Display names for CV slots. 0-89 mirror the MIDI-mapped
// sources, 90-97 are the MIDI-learn dynamic CC slots, 98-101 are the
// Marbles random CV outputs (X1..X3, Y), 102-105 are the SEQ modulator
// outputs (GATE x2 level, ARP x2 pitch).
static const char* cvSlotDisplayNames[106] = {
    // Slots 0-39: Voice A/B/C/D note/velocity/control params
    "A.Note", "A.Velo", "A.Bank", "A.SBnk", "A.Prog",
    "A.PB",   "A.PBLg", "A.AT",   "A.MW1",  "A.BC2",
    "B.Note", "B.Velo", "B.Bank", "B.SBnk", "B.Prog",
    "B.PB",   "B.PBLg", "B.AT",   "B.MW1",  "B.BC2",
    "C.Note", "C.Velo", "C.Bank", "C.SBnk", "C.Prog",
    "C.PB",   "C.PBLg", "C.AT",   "C.MW1",  "C.BC2",
    "D.Note", "D.Velo", "D.Bank", "D.SBnk", "D.Prog",
    "D.PB",   "D.PBLg", "D.AT",   "D.MW1",  "D.BC2",
    // Slots 40-71: Voice A/B/C/D macro + pitch CVs
    "A.Res", "A.Rel", "A.Atk", "A.Cut",
    "A.C1",  "A.C#1", "A.D1",  "A.D#1",
    "B.Res", "B.Rel", "B.Atk", "B.Cut",
    "B.E1",  "B.F1",  "B.F#1", "B.G1",
    "C.Res", "C.Rel", "C.Atk", "C.Cut",
    "C.G#1", "C.A1",  "C.A#1", "C.B1",
    "D.Res", "D.Rel", "D.Atk", "D.Cut",
    "D.C2",  "D.C#2", "D.D2",  "D.D#2",
    // Slots 72-89: Global MIDI CCs
    "G.PB",  "G.PBLg","G.AT",  "G.MW1",
    "G.BC2", "Foot",  "Data6", "Vol",
    "Bal",   "Pan",   "Expr",  "FX1",
    "FX2",   "Sust",  "Port",  "Sost",
    "Soft",  "Hold",
    // Slots 90-97: dynamic CC (MIDI learn)
    "CC1", "CC2", "CC3", "CC4", "CC5", "CC6", "CC7", "CC8",
    // Slots 98-101: Marbles random CV outputs
    "MB.X1", "MB.X2", "MB.X3", "MB.Y",
    // Slots 102-105: SEQ modulators (GATE x2 level, ARP x2 pitch)
    "SEQ.1Level", "SEQ.2Level", "SEQ.1Note", "SEQ.2Note"
};

// Display names for trigger outputs: Marbles T1-T3 at trig slots 40-42,
// SEQ modulator trig outputs at trig slots 43-50 (GATE generator trig,
// ARP step gate/accent/slide). SEQ.1Gate is the ARP step gate — the
// TBD03 trigger target.
static const char* trigSlotNames[11] = {
    "MB.T1", "MB.T2", "MB.T3",
    "SEQ.1Trig", "SEQ.2Trig", "SEQ.1Gate", "SEQ.2Gate",
    "SEQ.1Acc", "SEQ.2Acc", "SEQ.1Slide", "SEQ.2Slide"
};

// Render a trig slot for the UI: marbles slots by name, MIDI-mapped slots
// (0-39) by their cv-table name (same sources), anything else as its
// number. Uses a small static rotation so it can be called several times
// per snprintf argument list.
inline const char* TrigSlotName(int slot) {
static char buf[3][12];
static uint8_t rot = 0;
char *b = buf[rot];
rot = (rot + 1) % 3;
if (slot < 0) {
    b[0] = '-'; b[1] = 0;
} else if (slot >= 40 && slot < 51) {
    const char *s = trigSlotNames[slot - 40];
    uint8_t i = 0;
    while (s[i] && i < 11) { b[i] = s[i]; ++i; }
        b[i] = 0;
    } else if (slot < 40) {
        return cvSlotDisplayNames[slot];
    } else {
        // decimal into the same buffer
        int v = slot, i = 0;
        char tmp[8]; uint8_t n = 0;
        if (v == 0) tmp[n++] = '0';
        while (v > 0 && n < 7) { tmp[n++] = '0' + (v % 10); v /= 10; }
        while (n > 0) b[i++] = tmp[--n];
        b[i] = 0;
    }
    return b;
}
