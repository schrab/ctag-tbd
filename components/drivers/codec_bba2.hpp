#pragma once

// BBA2 codec facade (doc/bba2-esp32s3-sdd.md §5.2): WM8978 control + S3
// I2S std master. Same static-facade shape as codec_bba.hpp; codec.hpp's
// BBA branch (aliased on for BBA2 via the CMake alias define) declares the
// matching signatures that main/ links against.

#include <cstdint>
#include "sdkconfig.h"
#include "wm8978.hpp"

namespace CTAG {
    namespace DRIVERS {

        class Codec {
        public:
            Codec() = delete;
            static void InitCodec();
            static void HighPassEnable();
            static void HighPassDisable();
            static void RecalibDCOffset();
            static void SetOutputLevels(const uint32_t left, const uint32_t right);
            static void ReadBuffer(float *buf, uint32_t sz);
            static void WriteBuffer(float *buf, uint32_t sz);
            static void SetSampleRate(uint32_t sample_rate);
            static uint32_t GetSampleRate();
            static void SetInputSource(int sel);
            static void SetOutputSource(int sel);
            static void SetMixerMode(int mode);
            static void SetAnalogBypass(bool on);
            static void SetInputGain(int gain);
            static void SetInputGainNative(int gain); // 0..63 direct PGA
            static const uint8_t *DebugReadAllRegs();
            static bool DebugReadReg(uint8_t reg, uint8_t &val);
            // Bench register write (mic-routing sweeps): shadowed write +
            // full 9-bit shadow readback.
            static bool DebugWriteReg(uint8_t reg, uint16_t val, uint16_t &readback);
            static uint32_t DebugI2sReadErrors();
            static uint32_t DebugI2sWriteErrors();

        private:
            static wm8978 codec;
        };
    }
}
