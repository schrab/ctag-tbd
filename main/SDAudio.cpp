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

char SDAudio::filePath[64] = {};
FILE *SDAudio::file = nullptr;
int SDAudio::numChannels = 2;
uint32_t SDAudio::totalFrames = 0;
uint32_t SDAudio::bytePos = 0;
bool SDAudio::wavHeaderWritten = false;
uint32_t SDAudio::recordedBytes = 0;

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
    if (playing || recording) return;
    snprintf(filePath, sizeof(filePath), "%s", path);
    writePos = 0;
    readPos = 0;
    bytePos = 0;
    totalFrames = 0;
    underrun = false;
    playing = true;
    stopRequested = false;
    xTaskCreatePinnedToCore(workerTask, "sd_audio", 4096, (void *)0, 5, nullptr, 0);
}

void SDAudio::StartRecording(const char *path) {
    if (playing || recording) return;
    snprintf(filePath, sizeof(filePath), "%s", path);
    writePos = 0;
    readPos = 0;
    recordedBytes = 0;
    wavHeaderWritten = false;
    recording = true;
    stopRequested = false;
    xTaskCreatePinnedToCore(workerTask, "sd_audio", 4096, (void *)1, 5, nullptr, 0);
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
    for (int i = 0; i < toRead; i++) {
        uint32_t idx = (rp + i) & (RING_SZ - 1);
        buf[i * 2] += ring[idx * 2];
        buf[i * 2 + 1] += ring[idx * 2 + 1];
    }
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

void SDAudio::workerTask(void *param) {
    bool isRecordingMode = (bool)param;
    int16_t tmp[CHUNK_MAX * 2];

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
            readPos.store((rp + toWrite) & (RING_SZ - 1), std::memory_order_release);
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
        uint8_t hdrBuf[44];
        size_t n = fread(hdrBuf, 1, 44, file);
        if (n != 44 || memcmp(hdrBuf, "RIFF", 4) != 0 || memcmp(hdrBuf + 8, "WAVE", 4) != 0) {
            ESP_LOGE(TAG, "Not WAV: %s", filePath);
            fclose(file);
            playing = false;
            vTaskDelete(nullptr);
            return;
        }
        numChannels = *(uint16_t *)(hdrBuf + 22);
        // Find data chunk
        totalFrames = 0;
        bool foundData = false;
        while (1) {
            uint8_t chunk[8];
            if (fread(chunk, 1, 8, file) != 8) break;
            uint32_t chunkSz = *(uint32_t *)(chunk + 4);
            if (memcmp(chunk, "data", 4) == 0) {
                int bytesPerFrame = numChannels * 2;
                totalFrames = chunkSz / bytesPerFrame;
                bytePos = 0;
                foundData = true;
                break;
            }
            chunkSz = (chunkSz + 1) & ~1; // pad
            fseek(file, chunkSz, SEEK_CUR);
        }
        if (!foundData) {
            ESP_LOGE(TAG, "No data chunk");
            fclose(file);
            playing = false;
            vTaskDelete(nullptr);
            return;
        }
        ESP_LOGI(TAG, "Play: %s, %dch, %lu frames", filePath, numChannels, (unsigned long)totalFrames);

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
            int bytesPerFrame = numChannels * 2;
            size_t framesRead = fread(tmp, bytesPerFrame, toWrite, file);
            if (framesRead == 0) break;

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
        }

        fclose(file);
        file = nullptr;
        playing = false;
        ESP_LOGI(TAG, "Playback done: %s", filePath);
    }

    vTaskDelete(nullptr);
}
