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

#include "SDAudio.hpp"
#include "fs.hpp"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
#include <cstdio>
#include <cmath>

using namespace CTAG::AUDIO;

static const char *TAG = "SDA";

#define CHUNK_MAX 1024

float *SDAudio::ring = nullptr;
std::atomic<uint32_t> SDAudio::writePos{0};
std::atomic<uint32_t> SDAudio::readPos{0};
std::atomic<bool> SDAudio::playing{false};
std::atomic<bool> SDAudio::recording{false};
std::atomic<bool> SDAudio::underrun{false};
bool SDAudio::stopRequested = false;
std::atomic<uint32_t> SDAudio::route{0};      // Route::MAIN_OUT
std::atomic<uint32_t> SDAudio::levelPeak{0};
std::atomic<uint32_t> SDAudio::gain{100};     // unity

char SDAudio::filePath[64] = {};
FILE *SDAudio::file = nullptr;
int SDAudio::numChannels = 2;
uint32_t SDAudio::totalFrames = 0;
uint32_t SDAudio::bytePos = 0;
bool SDAudio::wavHeaderWritten = false;
uint32_t SDAudio::recordedBytes = 0;
long SDAudio::dataStart = 0;

void SDAudio::Init() {
    ring = (float *)heap_caps_malloc(RING_SZ * 2 * sizeof(float), MALLOC_CAP_SPIRAM);
    if (!ring) {
        ESP_LOGE(TAG, "Failed to allocate ring buffer in SPIRAM");
        return;
    }
    memset(ring, 0, RING_SZ * 2 * sizeof(float));
    ESP_LOGI(TAG, "SDAudio: %d KB ring, %d frames", (RING_SZ * 2 * (int)sizeof(float)) / 1024, RING_SZ);
}

void SDAudio::StartPlayback(const char *path) {
    if (recording) return;
    if (playing) {
        // Switch files while playing: ask the current worker to exit and
        // wait for it to release the shared file/ring state, then respawn.
        // The worker self-clears `playing` right before deleting itself.
        stopRequested = true;
        int waitMs = 100;
        while (playing && waitMs-- > 0) vTaskDelay(pdMS_TO_TICKS(1));
        if (playing) {
            ESP_LOGW(TAG, "Previous playback would not stop; keeping %s", filePath);
            return;
        }
    }
    snprintf(filePath, sizeof(filePath), "%s", path);
    writePos = 0;
    readPos = 0;
    bytePos = 0;
    totalFrames = 0;
    underrun = false;
    playing = true;
    stopRequested = false;
    // Priority 1 = same as the link task (link_ep, core 0): FreeRTOS time
    // slicing keeps heartbeats flowing during continuous SD traffic. At the
    // old prio 5 the worker starved link_ep while recording and the panel
    // dropped the link ("DSP link lost" after 1 s of silence). The ~3 s
    // ring absorbs any SD stalls.
    xTaskCreatePinnedToCore(workerTask, "sd_audio", 6144, (void *)0, 1, nullptr, 0);
}

void SDAudio::StartRecording(const char *path) {
    if (playing || recording) return;
    snprintf(filePath, sizeof(filePath), "%s", path);
    writePos = 0;
    readPos = 0;
    recordedBytes = 0;
    bytePos = 0;
    numChannels = 2; // recordings are always 2ch (GetPositionFrames divides by it)
    wavHeaderWritten = false;
    recording = true;
    stopRequested = false;
    xTaskCreatePinnedToCore(workerTask, "sd_audio", 6144, (void *)1, 1, nullptr, 0);
}

void SDAudio::Stop() {
    playing = false;
    recording = false;
    stopRequested = true;
}

bool SDAudio::IsPlaying() { return playing.load(); }
bool SDAudio::IsRecording() { return recording.load(); }

bool SDAudio::IsSDMounted() {
    return DRIVERS::FileSystem::IsSDMounted();
}

int SDAudio::MixPlayback(float *buf, int nFrames) {
    if (!playing || !ring) return 0;
    uint32_t rp = readPos.load(std::memory_order_acquire);
    uint32_t wp = writePos.load(std::memory_order_acquire);
    uint32_t avail = (wp - rp) & (RING_SZ - 1);
    if (avail == 0) {
        underrun = true;
        return 0;
    }
    int toRead = avail < (uint32_t)nFrames ? (int)avail : nFrames;
    const float g = (float)gain.load(std::memory_order_relaxed) / 100.0f;
    float peak = 0.f;
    for (int i = 0; i < toRead; i++) {
        uint32_t idx = (rp + i) & (RING_SZ - 1);
        float l = ring[idx * 2] * g;
        float r = ring[idx * 2 + 1] * g;
        float al = fabsf(l), ar = fabsf(r);
        if (al > peak) peak = al;
        if (ar > peak) peak = ar;
        buf[i * 2] += l;
        buf[i * 2 + 1] += r;
    }
    // Tape signal level for the MIX-page meter (0..1000); the 25 Hz link
    // stream read-and-clears it.
    levelPeak.store((uint32_t)(peak * 1000.f), std::memory_order_relaxed);
    readPos.store((rp + toRead) & (RING_SZ - 1), std::memory_order_release);
    return toRead;
}

int SDAudio::RecordSamples(float *buf, int nFrames) {
    if (!recording || !ring) return 0;
    uint32_t rp = readPos.load(std::memory_order_acquire);
    uint32_t wp = writePos.load(std::memory_order_acquire);
    uint32_t free = (rp - wp - 1) & (RING_SZ - 1);
    if (free < (uint32_t)nFrames) {
        return 0;
    }
    for (int i = 0; i < nFrames; i++) {
        uint32_t idx = (wp + i) & (RING_SZ - 1);
        ring[idx * 2] = buf[i * 2];
        ring[idx * 2 + 1] = buf[i * 2 + 1];
    }
    writePos.store((wp + nFrames) & (RING_SZ - 1), std::memory_order_release);
    return nFrames;
}

const char* SDAudio::GetFileName() { return filePath; }
uint32_t SDAudio::GetPositionFrames() { return bytePos / (numChannels * 2); }
uint32_t SDAudio::GetTotalFrames() { return totalFrames; }

void SDAudio::SetRoute(Route r) {
    route.store((uint32_t)r, std::memory_order_relaxed);
    ESP_LOGI(TAG, "Route: %s", r == Route::PLUGIN_IN ? "PLUGIN_IN" : "MAIN_OUT");
}

SDAudio::Route SDAudio::GetRoute() { return (Route)route.load(std::memory_order_relaxed); }

void SDAudio::SetGain(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    gain.store((uint32_t)percent, std::memory_order_relaxed);
}

int SDAudio::GetGain() { return (int)gain.load(std::memory_order_relaxed); }

uint32_t SDAudio::GetLevelPeak() { return levelPeak.exchange(0); }

bool SDAudio::ProbeWav(const char *path, WavInfo *out) {
    if (!path || !out) return false;
    memset(out, 0, sizeof(*out));
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    bool ok = false;
    do {
        uint8_t tag[12];
        if (fread(tag, 1, 12, f) != 12 || memcmp(tag, "RIFF", 4) != 0 ||
            memcmp(tag + 8, "WAVE", 4) != 0) {
            break;
        }
        // Chunk walk: fmt and data can sit behind LIST/bext metadata chunks,
        // and fmt may be 40 bytes (WAVE_FORMAT_EXTENSIBLE) — recorder/DAW
        // files routinely fail the canonical 44-byte-header assumption.
        bool foundData = false, fmtOk = false;
        uint32_t dataSz = 0;
        uint8_t chunk[8];
        while (fread(chunk, 1, 8, f) == 8) {
            uint32_t chunkSz = *(uint32_t *)(chunk + 4);
            if (memcmp(chunk, "fmt ", 4) == 0) {
                uint8_t fmtBuf[40];
                // Tolerate an undersized/zero fmt chunk size: the SDAudio
                // recorder used to leave the size field at 0 with the fields
                // at canonical offsets, so always read the canonical 16
                // bytes minimum (files already on cards rely on this).
                size_t toRead = chunkSz > 16 ? (chunkSz < sizeof(fmtBuf) ? chunkSz : sizeof(fmtBuf))
                                             : 16;
                if (fread(fmtBuf, 1, toRead, f) != toRead) break;
                if (chunkSz & 1) fseek(f, 1, SEEK_CUR);
                out->format = *(uint16_t *)(fmtBuf + 0);
                out->channels = *(uint16_t *)(fmtBuf + 2);
                out->sampleRate = *(uint16_t *)(fmtBuf + 4);
                out->bits = *(uint16_t *)(fmtBuf + 14);
                fmtOk = true;
                continue;
            }
            if (memcmp(chunk, "data", 4) == 0) {
                dataSz = chunkSz;
                out->dataStart = ftell(f);
                foundData = true;
                break;
            }
            fseek(f, (chunkSz + 1) & ~1u, SEEK_CUR); // skip (+ pad)
        }
        if (!foundData || !fmtOk) break;
        // The renderer reinterprets the stream as int16 frames: accept PCM
        // (0x01) and WAVE_FORMAT_EXTENSIBLE (0xFFFE), 16-bit, mono/stereo.
        if (out->bits != 16 || out->channels < 1 || out->channels > 2 ||
            (out->format != 1 && out->format != 0xFFFE)) {
            break;
        }
        if (dataSz == 0 || dataSz == 0xFFFFFFFFu) {
            // Streamed/miswritten data size: fall back to the file length.
            fseek(f, 0, SEEK_END);
            long fsz = ftell(f);
            dataSz = (uint32_t)(fsz - out->dataStart);
        }
        out->sizeBytes = (uint32_t)fseek(f, 0, SEEK_END) == 0 ? (uint32_t)ftell(f) : 0;
        out->frames = dataSz / (out->channels * 2);
        ok = true;
    } while (false);
    fclose(f);
    return ok;
}

void SDAudio::workerTask(void *param) {
    bool isRecordingMode = (bool)param;
    // STATIC, not on the stack: 4 KB would consume the whole 4 KB task
    // stack (the WAV parse/write below would smash the TCB). Only one
    // worker instance can run at a time (StartPlayback/StartRecording
    // guard), so a single static buffer is safe.
    static int16_t tmp[CHUNK_MAX * 2];

    if (isRecordingMode) {
        file = fopen(filePath, "wb");
        if (!file) {
            ESP_LOGE(TAG, "Can't create %s", filePath);
            recording = false;
            vTaskDelete(nullptr);
            return;
        }
        // Write WAV placeholder header
        uint8_t hdr[44];
        memset(hdr, 0, 44);
        memcpy(hdr, "RIFF", 4);
        memcpy(hdr + 8, "WAVE", 4);
        memcpy(hdr + 12, "fmt ", 4);
        *(uint32_t *)(hdr + 16) = 16; // fmt chunk size — was left ZERO, which
                                      // made the chunk walker skip the fields
        *(uint16_t *)(hdr + 20) = 1;
        *(uint16_t *)(hdr + 22) = 2;  // 2 channels
        *(uint32_t *)(hdr + 24) = 44100;
        *(uint32_t *)(hdr + 28) = 176400;
        *(uint16_t *)(hdr + 32) = 4;
        *(uint16_t *)(hdr + 34) = 16;
        memcpy(hdr + 36, "data", 4);
        fwrite(hdr, 1, 44, file);
        fflush(file);
        recordedBytes = 0;
        readPos = 0;
        writePos = 0;

        while (recording && !stopRequested) {
            uint32_t wp = writePos.load(std::memory_order_acquire);
            uint32_t rp = readPos.load(std::memory_order_acquire);
            uint32_t avail = (wp - rp) & (RING_SZ - 1);
            if (avail == 0) {
                vTaskDelay(pdMS_TO_TICKS(5));
                continue;
            }
            uint32_t toWrite = avail > CHUNK_MAX ? CHUNK_MAX : avail;
            for (uint32_t i = 0; i < toWrite; i++) {
                uint32_t idx = (rp + i) & (RING_SZ - 1);
                float sl = ring[idx * 2];
                float sr = ring[idx * 2 + 1];
                if (sl > 1.0f) sl = 1.0f;
                if (sl < -1.0f) sl = -1.0f;
                if (sr > 1.0f) sr = 1.0f;
                if (sr < -1.0f) sr = -1.0f;
                tmp[i * 2] = (int16_t)(sl * 32767.0f);
                tmp[i * 2 + 1] = (int16_t)(sr * 32767.0f);
            }
            fwrite(tmp, sizeof(int16_t), toWrite * 2, file);
            recordedBytes += toWrite * 2 * sizeof(int16_t);
            // Recording timecode: GetPositionFrames() = bytePos / (2ch*2B).
            bytePos = recordedBytes;
            readPos.store((rp + toWrite) & (RING_SZ - 1), std::memory_order_release);
            // One tick per chunk keeps the SD bus breathable: continuous
            // fwrite at this priority starved FATFS ops from other tasks
            // (the link task's directory scans timed out -> "No card or
            // empty" while the tape ran). ~85 chunks/s still sustains
            // ~2x the record bitrate; the ring absorbs the pauses.
            vTaskDelay(pdMS_TO_TICKS(1));
        }

        // Finalize WAV header
        fseek(file, 4, SEEK_SET);
        uint32_t fileSz = recordedBytes + 36;
        fwrite(&fileSz, 4, 1, file);
        fseek(file, 40, SEEK_SET);
        fwrite(&recordedBytes, 4, 1, file);
        fclose(file);
        file = nullptr;
        recordedBytes += 44; // include header for UI display
        ESP_LOGI(TAG, "Recorded: %s, %lu bytes", filePath, (unsigned long)recordedBytes);
        recording = false;
    } else {
        // Playback
        file = fopen(filePath, "rb");
        if (!file) {
            ESP_LOGE(TAG, "Can't open %s", filePath);
            playing = false;
            vTaskDelete(nullptr);
            return;
        }
        fclose(file);
        file = nullptr;
        // Header parse via the shared walker (tolerates LIST/bext chunks
        // and extensible fmt; validates 16-bit mono/stereo PCM).
        WavInfo wi;
        if (!ProbeWav(filePath, &wi)) {
            ESP_LOGE(TAG, "Unsupported or broken WAV: %s", filePath);
            playing = false;
            vTaskDelete(nullptr);
            return;
        }
        numChannels = wi.channels;
        totalFrames = wi.frames;
        dataStart = wi.dataStart;
        bytePos = 0;
        ESP_LOGI(TAG, "Play: %s, %dch %uHz, %lu frames%s", filePath, numChannels,
                 wi.sampleRate, (unsigned long)totalFrames,
                 wi.sampleRate != 44100 ? " (rate mismatch, no resampler)" : "");

        file = fopen(filePath, "rb");
        if (!file) {
            ESP_LOGE(TAG, "Can't reopen %s", filePath);
            playing = false;
            vTaskDelete(nullptr);
            return;
        }
        if (fseek(file, dataStart, SEEK_SET) != 0) {
            fclose(file);
            file = nullptr;
            playing = false;
            vTaskDelete(nullptr);
            return;
        }
        int bytesPerFrame = numChannels * 2;
        writePos = 0;
        readPos = 0;

        while (playing && !stopRequested) {
            uint32_t wp = writePos.load(std::memory_order_acquire);
            uint32_t rp = readPos.load(std::memory_order_acquire);
            uint32_t free = (rp - wp - 1) & (RING_SZ - 1);
            if (free == 0) {
                vTaskDelay(pdMS_TO_TICKS(2));
                continue;
            }
            uint32_t toWrite = free > CHUNK_MAX ? CHUNK_MAX : free;
            size_t framesRead = fread(tmp, bytesPerFrame, toWrite, file);
            if (framesRead == 0) {
                // End of file: loop until stopped.
                if (playing && !stopRequested && fseek(file, dataStart, SEEK_SET) == 0) {
                    bytePos = 0;
                    continue;
                }
                break;
            }

            for (uint32_t i = 0; i < framesRead; i++) {
                uint32_t idx = (wp + i) & (RING_SZ - 1);
                float sl = tmp[i * 2] * (1.0f / 32768.0f);
                if (numChannels == 1) {
                    ring[idx * 2] = sl;
                    ring[idx * 2 + 1] = sl;
                } else {
                    ring[idx * 2] = sl;
                    ring[idx * 2 + 1] = tmp[i * 2 + 1] * (1.0f / 32768.0f);
                }
            }
            bytePos += framesRead * bytesPerFrame;
            writePos.store((wp + framesRead) & (RING_SZ - 1), std::memory_order_release);
            // One tick per chunk keeps the SD bus breathable (same reason as
            // the record loop): without the pause, continuous fread starved
            // the link task's directory scans -> "No card or empty" while
            // the tape played. ~85 chunks/s ≈ 2x the playback bitrate.
            vTaskDelay(pdMS_TO_TICKS(1));
        }

        fclose(file);
        file = nullptr;
        playing = false;
        ESP_LOGI(TAG, "Playback done: %s", filePath);
    }

    vTaskDelete(nullptr);
}
