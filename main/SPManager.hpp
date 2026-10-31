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

#include <atomic>
#include <memory>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "ctagSoundProcessor.hpp"
#include "ctagSoundProcessorFactory.hpp"
#include "SPManagerDataModel.hpp"
#include <atomic>

using namespace CTAG::SP;

namespace CTAG {
    namespace AUDIO {
        class SoundProcessorManager final {
        public:
            SoundProcessorManager() = delete;
            static void StartSoundProcessor();

            static const char *GetCStrJSONSoundProcessors() {
                if (!model) return "[]";
                return model->GetCStrJSONSoundProcessors();
            }

            // Remove a plugin from the bundle's available list (deletes its
            // mui-/mp- descriptor files on SPIFFS and refreshes the model).
            static bool RemovePlugin(const string &id) {
                if (!model) return false;
                return model->RemovePlugin(id);
            }

            static const char *GetCStrJSONActivePluginParams(const int chan) {
                if (sp[chan] == nullptr) return "{}";
                return sp[chan]->GetCStrJSONParamSpecs();
            }

            static const char *GetCStrJSONGetPresets(const int chan) { // names of all available presets
                if (sp[chan] == nullptr) return "{}";
                return sp[chan]->GetCStrJSONPresets();
            }

            static const char *GetCStrJSONAllPresetData(const int chan) { // current preset as JSON
                if (sp[chan] == nullptr) return "{}";
                return sp[chan]->GetCStrJSONAllPresetData();
            }

            static const char *GetCStrJSONConfiguration() {
                return model->GetCStrJSONConfiguration();
            }

            static const char *GetCStrJSONSoundProcessorPresets(const string &id) {
                return model->GetCStrJSONSoundProcessorPresets(id);
            }

            static void SetCStrJSONSoundProcessorPreset(const char* id, const char *data) {
                model->SetCStrJSONSoundProcessorPreset(id, data);
            }

            static void SetConfigurationFromJSON(const string &data);

            static string GetStringID(const int chan);

            static bool IsPluginStereo(const string &id) {
                if (!model) return false;
                return model->IsStereo(id);
            }

            static void SetSoundProcessorChannel(const int chan, const string &id);

            static void SetChannelParamValue(const int chan, const string &id, const string &key, const int val);

            static void ChannelSavePreset(const int chan, const string &name, const int number);

            static void ChannelLoadPreset(const int chan, const int number);

            static void KillAudioTask();

            static void DisablePluginProcessing();
            static void EnablePluginProcessing();
            static void RefreshSampleRom();
            static bool GetCPUOverload() { return cpuOverload != 0; }
            // Cycles spent in the most recent audio block after Codec::ReadBuffer
            // (DC cut, gate, plugins, output stage). Debug/bring-up only.
            static uint32_t GetLastAudioTaskCycles() {
                return lastAudioTaskCycles.load(std::memory_order_relaxed);
            }
            // Minimum-ever-free audio-task stack, in bytes. Zero until the
            // audio task has been created. Used by the hardware bring-up UI.
            static uint32_t GetAudioStackHighWaterBytes() {
                if (audioTaskH == nullptr) return 0;
                return static_cast<uint32_t>(uxTaskGetStackHighWaterMark(audioTaskH))
                       * sizeof(StackType_t);
            }

            // Peak metering for the MIX page. While metering is enabled the
            // audio task stores per-block peaks scaled to 0..1000; it skips
            // the extra pass entirely otherwise (the DSP budget is tight).
            static void SetMeteringEnabled(bool on) { meteringEnabled = on; }

            // Rolling output capture for bench click forensics: the last
            // kCaptureBlocks blocks of the finished stereo output. Written
            // unconditionally by the audio task; read by
            // /debug/getAudioCapture. Races the writer by design — the
            // capture is diagnostic, not a reference stream.
            static constexpr int kCaptureBlocks = 8;
            static float audioRing[kCaptureBlocks][64];
            static atomic<uint32_t> audioRingSeq;
            static uint32_t GetAudioRingSeq() {
                return audioRingSeq.load(std::memory_order_relaxed);
            }

            // Full-coverage dropout forensics: per-block output peak
            // (0..1000) for the last kVUHistLen blocks, written every block
            // with no blind windows — /debug/getAudioCapture returns the
            // history so bench analysis sees EVERY dropout. muteBlocks
            // counts blocks the audio task muted because processMutex was
            // held elsewhere (plugin load, preset I/O, config write).
            static constexpr int kVUHistLen = 256;
            static uint16_t vuHist[kVUHistLen];
            static atomic<uint32_t> vuHistSeq;
            static atomic<uint32_t> muteBlocks;
            static uint32_t GetVUHistSeq() {
                return vuHistSeq.load(std::memory_order_relaxed);
            }
            static uint32_t GetMuteBlocks() {
                return muteBlocks.load(std::memory_order_relaxed);
            }
            static float GetBoundaryResidual() {
                return m_boundaryResidual;
            }
            // Millisecond timestamp of the last parameter edit (any
            // channel, any source). The link's 10 Hz on-change push
            // defers the full params JSON re-push (display-sync traffic,
            // NOT the audio path — parameter changes are applied to the
            // sound instantly) until edits have been quiet for a while:
            // the C3 already knows the values it is setting, and the
            // burst (regen + ~5 KB TX) measurably delayed the audio
            // task and audibly clicked the output.
            static uint32_t GetLastParamEditMs() {
                return lastParamEditMs.load(std::memory_order_relaxed);
            }
            // Incremental parameter sync: the last applied edit, read by
            // the link's 10 Hz tick to push ONE tiny pushParamValue frame
            // (the panel already knows its own edits; this keeps other
            // views truthful without ever re-pushing the whole params
            // JSON — the full push stays on plugin/preset loads + boot).
            static const char *GetLastEditId() { return lastEditId; }
            static int GetLastEditCh() { return lastEditCh.load(std::memory_order_relaxed); }
            static int GetLastEditVal() { return lastEditVal.load(std::memory_order_relaxed); }
            static uint32_t GetLastEditSeq() { return lastEditSeq.load(std::memory_order_relaxed); }
            static bool GetRingFrozen() {
                return ringFrozen.load(std::memory_order_relaxed);
            }
            static void ClearRingFrozen() {
                ringFrozen.store(false, std::memory_order_relaxed);
            }

            // Audio-loop period history (0.1 ms units, saturated at 6553.5
            // ms): the gap between consecutive loop iterations. Normal
            // pacing is ~232 (23.2 ms block); a spike means the task was
            // delayed past its deadline — the I2S DMA underruns and the
            // codec stutters even though the rendered buffer is clean.
            static constexpr int kLoopHistLen = 256;
            static uint16_t loopHist[kLoopHistLen];
            static atomic<uint32_t> loopHistSeq;
            static uint32_t GetLoopHistSeq() {
                return loopHistSeq.load(std::memory_order_relaxed);
            }
            static uint32_t GetVUPeak(int channel) {
                switch (channel) {
                    case 0: return vuInL.load(std::memory_order_relaxed);
                    case 1: return vuInR.load(std::memory_order_relaxed);
                    case 2: return vuOutL.load(std::memory_order_relaxed);
                    default: return vuOutR.load(std::memory_order_relaxed);
                }
            }

        private:
            static void audio_task(void *pvParams);

            static void updateConfiguration();

            static TaskHandle_t audioTaskH;
            static ctagSoundProcessor *sp[2];
            static std::unique_ptr<SPManagerDataModel> model;
            static SemaphoreHandle_t processMutex;
            static atomic<uint32_t> cpuOverload;
            static atomic<uint32_t> lastAudioTaskCycles;
            static atomic<uint32_t> lastParamEditMs;
            static char lastEditId[64];
            static atomic<int32_t> lastEditCh;
            static atomic<int32_t> lastEditVal;
            static atomic<uint32_t> lastEditSeq;

            // Click forensics: the output ring freezes itself when a block
            // shows a sample-to-sample step far above the running norm —
            // the frozen ring then holds the actual click waveform for
            // /debug/getAudioCapture. Cleared via /debug/clearFreeze.
            static atomic<bool> ringFrozen;
            static float m_prevBlockLast;      // last sample of the previous block (L)
            static float m_boundaryResidual;   // boundary step minus local slope at freeze

            static atomic<uint32_t> noiseGateCfg;
            static atomic<uint32_t> ch01Daisy;
            static atomic<uint32_t> toStereoCH0;
            static atomic<uint32_t> toStereoCH1;
            static atomic<uint32_t> runAudioTask;
            static atomic<uint32_t> ch0_outputSoftClip;
            static atomic<uint32_t> ch1_outputSoftClip;
            static atomic<bool> meteringEnabled;
            static atomic<uint32_t> vuInL;
            static atomic<uint32_t> vuInR;
            static atomic<uint32_t> vuOutL;
            static atomic<uint32_t> vuOutR;
        };
    }
}
