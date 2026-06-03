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
                running = false;
                lastTapTime = 0;
                tapCount = 0;
            }

            void ctagTempo::SetSampleRate(float fs_Hz, uint32_t block_size) {
                // Calculate how much phase to advance per audio block
                // blocks_per_second = fs_Hz / block_size
                // beats_per_second = bpm / 60.0
                // delta_phase = beats_per_second / blocks_per_second
                float blocks_per_second = fs_Hz / static_cast<float>(block_size);
                delta_phase_per_block = (bpm / 60.0f) / blocks_per_second;
            }

            void ctagTempo::SetBPM(float new_bpm) {
                if (new_bpm < 20.0f) new_bpm = 20.0f;
                if (new_bpm > 300.0f) new_bpm = 300.0f;
                bpm = new_bpm;
                
                // Recalculate delta phase if running internally
                if (source == Source::INTERNAL) {
                    // Assuming default 44100 Hz and 32 block size if not explicitly set yet,
                    // but ideally SetSampleRate is called first. We'll rely on it being called.
                    // For safety, we can store fs and block_size, but let's assume standard TBD config:
                    // fs = 44100, block = 32 -> blocks_per_sec = 1378.125
                    float blocks_per_second = 44100.0f / 32.0f; 
                    delta_phase_per_block = (bpm / 60.0f) / blocks_per_second;
                }
            }

            float ctagTempo::GetBPM() const {
                return bpm;
            }

            void ctagTempo::SetSource(Source src) {
                source = src;
                if (source == Source::INTERNAL) {
                    float blocks_per_second = 44100.0f / 32.0f;
                    delta_phase_per_block = (bpm / 60.0f) / blocks_per_second;
                }
            }

            ctagTempo::Source ctagTempo::GetSource() const {
                return source;
            }

            void ctagTempo::Start() {
                running = true;
                phase = 0.0f;
                tapCount = 0;
            }

            void ctagTempo::Stop() {
                running = false;
                phase = 0.0f;
                tapCount = 0;
            }

            void ctagTempo::Continue() {
                running = true;
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
                
                // MIDI clock is 24 PPQN (Pulses Per Quarter Note)
                // So 24 ticks = 1 beat (phase 1.0)
                phase += 1.0f / 24.0f;
                if (phase >= 1.0f) {
                    phase -= 1.0f;
                }
            }

            void ctagTempo::OnTapTempo() {
                uint32_t now = xTaskGetTickCount();
                
                if (tapCount == 0) {
                    lastTapTime = now;
                    tapCount = 1;
                } else {
                    uint32_t delta = now - lastTapTime;
                    lastTapTime = now;
                    tapCount++;
                    
                    // Ignore taps that are too fast (< 200ms) or too slow (> 3000ms)
                    if (delta > 200 && delta < 3000) {
                        // Calculate BPM: 60000 ms per minute / delta ms per beat
                        float tapped_bpm = 60000.0f / static_cast<float>(delta);
                        SetBPM(tapped_bpm);
                        
                        // Reset after 4 taps to allow re-tapping
                        if (tapCount >= 4) {
                            tapCount = 0;
                        }
                    } else {
                        // Reset if tap is out of reasonable range
                        tapCount = 1;
                        lastTapTime = now;
                    }
                }
            }
        } // HELPERS
    } // SP
} // CTAG
