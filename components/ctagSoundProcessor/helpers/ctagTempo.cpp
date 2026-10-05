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

#include "ctagTempo.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace CTAG {
    namespace SP {
        namespace HELPERS {
            ctagTempo::ctagTempo() {
                phase = 0.0f;
                running = true;
                lastTapTick = 0;
                tapCount = 0;
                tapHistoryCount = 0;
                lastMidiClockTick = 0;
                hasMidiClockTick = false;
                midiClockHistoryCount = 0;
                blocks_per_second = 1378.125f; // 44100 / 32, TBD default
            }

            void ctagTempo::UpdatePhaseIncrement() {
                if (blocks_per_second <= 0.0f) return;
                delta_phase_per_block = (bpm / 60.0f) / blocks_per_second;
            }

            void ctagTempo::SetSampleRate(float fs_Hz, uint32_t block_size) {
                if (block_size == 0) return;
                blocks_per_second = fs_Hz / static_cast<float>(block_size);
                UpdatePhaseIncrement();
            }

            void ctagTempo::SetBPM(float new_bpm) {
                if (new_bpm < 20.0f) new_bpm = 20.0f;
                if (new_bpm > 300.0f) new_bpm = 300.0f;
                bpm = new_bpm;

                // Recompute from the configured sample rate, not a literal.
                UpdatePhaseIncrement();
            }

            float ctagTempo::GetBPM() const {
                return bpm;
            }

            void ctagTempo::SetSource(Source src) {
                source = src;
                // Switching source invalidates any pending transport state.
                hasMidiClockTick = false;
                midiClockHistoryCount = 0;
                running = true;
                UpdatePhaseIncrement();
            }

            ctagTempo::Source ctagTempo::GetSource() const {
                return source;
            }

            // Transport messages belong to the external MIDI clock. When the source is
            // INTERNAL they are ignored, so a stray 0xFC from an unrelated
            // device cannot freeze the internal tempo with no way to restart it
            // from the UI.
            void ctagTempo::Start() {
                if (source != Source::MIDI_CLOCK) return;
                running = true;
                phase = 0.0f;
                hasMidiClockTick = false;
                midiClockHistoryCount = 0;
            }

            void ctagTempo::Stop() {
                if (source != Source::MIDI_CLOCK) return;
                running = false;
                phase = 0.0f;
                hasMidiClockTick = false;
                midiClockHistoryCount = 0;
            }

            void ctagTempo::Continue() {
                if (source != Source::MIDI_CLOCK) return;
                running = true;
            }

            // Reset the internal transport to a known state. Always available,
            // unlike Start/Stop/Continue which act only on an external clock.
            void ctagTempo::Reset() {
                running = true;
                phase = 0.0f;
                tapCount = 0;
                tapHistoryCount = 0;
                hasMidiClockTick = false;
                midiClockHistoryCount = 0;
            }

            bool ctagTempo::IsRunning() const {
                return running;
            }

            void ctagTempo::Tick() {
                if (!running || source != Source::INTERNAL) return;
                
                phase += delta_phase_per_block;
                if (phase >= 1.0f) {
                    phase -= 1.0f;
                }
            }

            float ctagTempo::GetPhase() const {
                return phase;
            }

            void ctagTempo::OnMidiClock() {
                if (source != Source::MIDI_CLOCK || !running) return;

                // Derive the external tempo from the spacing of the incoming
                // clocks. Without this the sequencers and gates -- which derive
                // their rate from GetBPM() -- would keep running at whatever
                // tempo was last dialled in while the LFOs tracked the
                // external clock.
                TickType_t now = xTaskGetTickCount();
                if (hasMidiClockTick) {
                    uint32_t delta_ms = static_cast<uint32_t>(now - lastMidiClockTick) * portTICK_PERIOD_MS;
                    // Plausible clock interval: ~20ms (300 BPM) .. ~3100ms (20 BPM)
                    if (delta_ms > 2 && delta_ms < 3100) {
                        midiClockIntervalHistory[midiClockHistoryCount % kMidiClockHistorySize] = delta_ms;
                        midiClockHistoryCount++;
                        if (midiClockHistoryCount > kMidiClockHistorySize) {
                            midiClockHistoryCount = kMidiClockHistorySize;
                        }

                        uint32_t sum = 0;
                        for (int i = 0; i < midiClockHistoryCount; i++) sum += midiClockIntervalHistory[i];
                        float avgInterval = static_cast<float>(sum) / static_cast<float>(midiClockHistoryCount);

                        // 24 PPQN: 24 intervals make one beat.
                        float measured_bpm = 60000.0f / (avgInterval * 24.0f);
                        SetBPM(measured_bpm);
                    }
                }
                lastMidiClockTick = now;
                hasMidiClockTick = true;

                // MIDI clock is 24 PPQN (Pulses Per Quarter Note)
                // So 24 ticks = 1 beat (phase 1.0)
                phase += 1.0f / 24.0f;
                if (phase >= 1.0f) {
                    phase -= 1.0f;
                }
            }

            void ctagTempo::OnTapTempo() {
                TickType_t now = xTaskGetTickCount();

                if (tapCount == 0) {
                    lastTapTick = now;
                    tapCount = 1;
                    tapHistoryCount = 0;
                } else {
                    TickType_t deltaTicks = now - lastTapTick;
                    lastTapTick = now;
                    tapCount++;

                    // Convert ticks to ms
                    uint32_t delta_ms = deltaTicks * portTICK_PERIOD_MS;

                    // Ignore taps that are too fast (< 200ms) or too slow (> 3000ms)
                    if (delta_ms > 200 && delta_ms < 3000) {
                        // Average the recent intervals so a single late tap does
                        // not dominate the result.
                        tapIntervalHistory[tapHistoryCount % kTapHistorySize] = delta_ms;
                        tapHistoryCount++;
                        if (tapHistoryCount > kTapHistorySize) tapHistoryCount = kTapHistorySize;

                        uint32_t sum = 0;
                        for (int i = 0; i < tapHistoryCount; i++) sum += tapIntervalHistory[i];
                        float avgInterval = static_cast<float>(sum) / static_cast<float>(tapHistoryCount);

                        SetBPM(60000.0f / avgInterval);

                        if (tapCount >= kTapHistorySize) {
                            tapCount = 0;
                        }
                    } else {
                        tapCount = 1;
                        tapHistoryCount = 0;
                        lastTapTick = now;
                    }
                }
            }
        } // HELPERS
    } // SP
} // CTAG
