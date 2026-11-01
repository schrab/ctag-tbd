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
    // Where MixPlayback injects the tape signal: straight onto the main
    // output (post-plugins, what the codec emits), or into the plugin
    // input buffer (fbuf == pd.buf) BEFORE the processors run.
    enum class Route : int { MAIN_OUT = 0, PLUGIN_IN = 1 };

    // WAV header facts (ProbeWav); sizes/lengths as stored in the file.
    struct WavInfo {
        uint32_t sizeBytes;   // whole file size on the card
        uint32_t frames;      // data bytes / bytesPerFrame
        uint16_t sampleRate;
        uint16_t channels;
        uint16_t bits;
        uint16_t format;      // 1 = PCM, 0xFFFE = WAVE_FORMAT_EXTENSIBLE
        long dataStart;       // file offset of the first audio frame
    };

    static void Init();
    static void StartPlayback(const char *path);
    static void StartRecording(const char *path);
    static void Stop();
    static bool IsPlaying();
    static bool IsRecording();
    static bool IsSDMounted();

    // Tape signal routing (volatile; MAIN_OUT after boot).
    static void SetRoute(Route r);
    static Route GetRoute();

    static void SetRoute(int r) { SetRoute((Route)r); }

    // Tape playback level, 0..100 % (volatile; 100 = unity after boot).
    static void SetGain(int percent);
    static int GetGain();

    // Called from audio task (IRAM safe, never blocks):
    // Returns actual frames mixed from SD into buf (stereo interleaved, -1..+1).
    static int MixPlayback(float *buf, int nFrames);
    // Returns actual frames written from buf to recording buffer.
    static int RecordSamples(float *buf, int nFrames);

    // UI status
    static const char* GetFileName();
    static uint32_t GetPositionFrames(); // current play/record position
    static uint32_t GetTotalFrames();

    // Tape signal peak (0..1000) of the last mixed block; read-and-clear,
    // consumed by the 25 Hz link stream for the MIX-page meter.
    static uint32_t GetLevelPeak();

    // Monotonic bench counters (see the field comments above); the 25 Hz
    // link stream embeds them so clicks can be attributed live.
    static uint32_t GetUnderruns();
    static uint32_t GetRecDrops();

    // Header parse only (no streaming): chunk walk tolerating LIST/bext
    // metadata and WAVE_FORMAT_EXTENSIBLE fmt; false if not a playable
    // 16-bit mono/stereo PCM WAV.
    static bool ProbeWav(const char *path, WavInfo *out);

private:
    static void workerTask(void *param);

    // Ring buffer: 32K frames (~371ms @44100), 2 ch = 512 KB SPIRAM
    static const int RING_SZ = 32768;
    static float *ring;
    static std::atomic<uint32_t> writePos;  // worker writes here (mod RING_SZ)
    static std::atomic<uint32_t> readPos;   // audio reads here (mod RING_SZ)
    static std::atomic<bool> playing;
    static std::atomic<bool> recording;
    // Bench diagnostics, carried by the 25 Hz link stream ("u"/"d" in the
    // sd object). underruns: MixPlayback calls that found an empty ring —
    // a handful at playback start is normal, a GROWING count mid-playback
    // means the worker fell behind (SD stalls) and the tape signal gaps.
    // recDrops: audio-task blocks discarded while recording because the
    // ring was full — clicks IN A RECORDED FILE come from here, not from
    // the playback path.
    static std::atomic<uint32_t> underruns;
    static std::atomic<uint32_t> recDrops;
    static bool stopRequested; // worker reads this
    static std::atomic<uint32_t> route;     // Route as int
    static std::atomic<uint32_t> levelPeak; // 0..1000, read-and-clear
    static std::atomic<uint32_t> gain;      // playback level, 0..100 %

    static char filePath[64];
    static FILE *file;
    static int numChannels;
    static uint32_t totalFrames;
    static uint32_t bytePos;
    static bool wavHeaderWritten;
    static uint32_t recordedBytes;
    static long dataStart; // playback: file offset of the first audio frame
};

}
