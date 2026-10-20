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

// Needed for the CONFIG_* checks below: without it every codec test silently
// falls through to the wrong branch (the same trap UIMenuPageMix.hpp hit).
#include "sdkconfig.h"
#include "driver/spi_master.h"

#ifdef CONFIG_TBD_BBA_CODEC_ES8388
#include "es8388.hpp"
#endif

namespace CTAG {
    namespace DRIVERS {
        typedef unsigned char u8;
        typedef unsigned short u16;

        class Codec {
        public:
            // Highest value the installed codec accepts for the output level
            // setting. The ES8388 stops at 33 (0 dB); the AIC3254 maps 0..63
            // with 58 as 0 dB, and non-BBA codecs accept 0..63 as well. The UI
            // and the config clamp share this so an out-of-range value is
            // never offered or written back.
#ifdef CONFIG_TBD_BBA_CODEC_ES8388
            static constexpr int MAX_OUTPUT_LEVEL = es8388::OUTPUT_VOLUME_MAX;
            static constexpr int DEFAULT_OUTPUT_LEVEL = es8388::OUTPUT_VOLUME_MAX; // 0 dB
#else
            static constexpr int MAX_OUTPUT_LEVEL = 63;
            static constexpr int DEFAULT_OUTPUT_LEVEL = 58; // 0 dB on the AIC3254
#endif

            Codec() = delete;
            static void InitCodec();

            static void HighPassEnable();

            static void HighPassDisable();

            static void RecalibDCOffset();

            static void SetOutputLevels(const uint32_t left, const uint32_t right);

            static void ReadBuffer(float *buf, uint32_t sz);

            static void WriteBuffer(float *buf, uint32_t sz);

#ifdef CONFIG_TBD_PLATFORM_BBA
            static void SetInputSource(int sel);
            static void SetOutputSource(int sel);
            static void SetMixerMode(int mode);
            static void SetAnalogBypass(bool on);
            static void SetInputGain(int gain);
            static const uint8_t *DebugReadAllRegs();
            static bool DebugReadReg(uint8_t reg, uint8_t &val);
#else
            static void SetInputSource(int sel) {}
            static void SetOutputSource(int sel) {}
            static void SetMixerMode(int mode) {}
            static void SetAnalogBypass(bool on) {}
            static void SetInputGain(int gain) {}
            static const uint8_t *DebugReadAllRegs() { return nullptr; }
            static bool DebugReadReg(uint8_t reg, uint8_t &val) { return false; }
#endif

        private:

            static void initSPI();
            static void freeSPI();

            static void setupSPIWM8731();

            static void setupSPIWM8978();

            static void setupI2SWM8731();

            static void setupI2SWM8978();

            static spi_device_handle_t codec_h;
            static spi_transaction_t trans;
            static spi_bus_config_t buscfg;
            static spi_device_interface_config_t codec_cfg;
            static esp_err_t ret;
            static bool isReady;

            // this code is from someone great, i could not trace back the original copyright
            static u8 WM8978_Init(void);
            static u8 WM8974_Init(void);

            static void WM8978_ADDA_Cfg(u8 dacen, u8 adcen);

            static void WM8978_Input_Cfg(u8 micen, u8 lineinen, u8 auxen);

            static void WM8978_Output_Cfg(u8 dacen, u8 bpsen);

            static void WM8978_MIC_Gain(u8 gain);

            static void WM8978_LINEIN_Gain(u8 gain);

            static void WM8978_AUX_Gain(u8 gain);

            static u8 WM8978_Write_Reg(u8 reg, u16 val);

            static u16 WM8978_Read_Reg(u8 reg);

            static void WM8978_HPvol_Set(u8 voll, u8 volr);

            static void WM8978_SPKvol_Set(u8 volx);

            static void WM8978_I2S_Cfg(u8 fmt, u8 len);

            static void WM8978_3D_Set(u8 depth);

            static void WM8978_EQ_3D_Dir(u8 dir);

            static void WM8978_EQ1_Set(u8 cfreq, u8 gain);

            static void WM8978_EQ2_Set(u8 cfreq, u8 gain);

            static void WM8978_EQ3_Set(u8 cfreq, u8 gain);

            static void WM8978_EQ4_Set(u8 cfreq, u8 gain);

            static void WM8978_EQ5_Set(u8 cfreq, u8 gain);

            static void WM8978_Noise_Set(u8 enable, u8 gain);

            static void WM8978_ALC_Set(u8 enable, u8 maxgain, u8 mingain);

        };
    }
}
