#pragma once

static const char* cvSlotDisplayNames[100] = {
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
    // Slots 90-99: ModEngine dynamic CC + LFOs
    "CC1", "CC2", "CC3", "CC4", "CC5", "CC6", "CC7", "CC8",
    "LFO1", "LFO2"
};
