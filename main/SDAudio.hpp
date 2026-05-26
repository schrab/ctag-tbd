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
#include <atomic>
#include <cstdio>

namespace CTAG::AUDIO {

class SDAudio final {
public:
    static void Init();
    static void StartPlayback(const char *path);
    static void StartRecording(const char *path);
    static void Stop();
    static bool IsPlaying();
    static bool IsRecording();
    static bool IsSDMounted();

    // Called from audio task (IRAM safe, never blocks):
    // Returns actual frames mixed from SD into buf (stereo interleaved, -1..+1).
    static int MixPlayback(float *buf, int nFrames);
    // Returns actual frames written from buf to recording buffer.
    static int RecordSamples(float *buf, int nFrames);

    // UI status
    static const char* GetFileName();
    static uint32_t GetPositionFrames(); // current play/record position
    static uint32_t GetTotalFrames();

private:
    static void workerTask(void *param);

    // Ring buffer: 32K frames (~371ms @44100), 2 ch = 512 KB SPIRAM
    static const int RING_SZ = 32768;
    static float *ring;
    static std::atomic<uint32_t> writePos;  // worker writes here (mod RING_SZ)
    static std::atomic<uint32_t> readPos;   // audio reads here (mod RING_SZ)
    static std::atomic<bool> playing;
    static std::atomic<bool> recording;
    static std::atomic<bool> underrun;
    static bool stopRequested; // worker reads this

    static char filePath[64];
    static FILE *file;
    static int numChannels;
    static uint32_t totalFrames;
    static uint32_t bytePos;
    static bool wavHeaderWritten;
    static uint32_t recordedBytes;
};

}
