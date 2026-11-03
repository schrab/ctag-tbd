#include "codec_bba2.hpp"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

using namespace CTAG::DRIVERS;

static const char *TAG = "CODEC_BBA2";

// BBA2 rev A pin map — as wired on the board (2026-11-04):
// I2S on the left-edge cluster, I2C on the hardwired codec header.
static constexpr gpio_num_t PIN_MCLK = GPIO_NUM_16;  // breakout MCK
static constexpr gpio_num_t PIN_BCLK = GPIO_NUM_15;  // breakout SCK
static constexpr gpio_num_t PIN_WS = GPIO_NUM_6;     // breakout FSA
static constexpr gpio_num_t PIN_DOUT = GPIO_NUM_17;  // S3 -> WM8978 DACDAT (breakout DAT)
static constexpr gpio_num_t PIN_DIN = GPIO_NUM_7;    // WM8978 ADCDAT -> S3 (breakout SDB)

static constexpr uint32_t kSampleRate = 44100; // locked on BBA2 (SDD §6.2)

static i2s_chan_handle_t s_tx = nullptr;
static i2s_chan_handle_t s_rx = nullptr;
static bool s_mic_mode = false; // mono mic: duplicate left into right

wm8978 Codec::codec;

void Codec::InitCodec() {
    ESP_LOGI(TAG, "Init WM8978 over I2C + I2S std master");

    if (!codec.init()) {
        ESP_LOGE(TAG, "WM8978 init failed — audio will be silent");
        // I2S still comes up so the pipeline never blocks.
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 4;   // hard rule: NEVER grow dma_desc_num (AGENTS.md)
    chan_cfg.dma_frame_num = 32; // 32-sample block, matches BUF_SZ
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_tx, &s_rx));

    i2s_std_config_t std_cfg = {};
    std_cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kSampleRate);
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256; // 11.2896 MHz @ 44.1k
    std_cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                           I2S_SLOT_MODE_STEREO);
    std_cfg.gpio_cfg.mclk = PIN_MCLK;
    std_cfg.gpio_cfg.bclk = PIN_BCLK;
    std_cfg.gpio_cfg.ws = PIN_WS;
    std_cfg.gpio_cfg.dout = PIN_DOUT;
    std_cfg.gpio_cfg.din = PIN_DIN;
    std_cfg.gpio_cfg.invert_flags.mclk_inv = false;
    std_cfg.gpio_cfg.invert_flags.bclk_inv = false;
    std_cfg.gpio_cfg.invert_flags.ws_inv = false;

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx));
    ESP_ERROR_CHECK(i2s_channel_enable(s_rx));
}

void Codec::HighPassEnable() { codec.highPass(true); }
void Codec::HighPassDisable() { codec.highPass(false); }

// WM8978 has no ES8388-style DC-offset register; the software DC-cut in
// audio_task remains the real filter (SDD §5.2).
void Codec::RecalibDCOffset() {}

void Codec::SetOutputLevels(const uint32_t left, const uint32_t right) {
    // The panel/CONFIG scale is the A1S convention 0..33 (ES8388: 33 = 0 dB);
    // WM8978's 0 dB sits at code 57 of 0..63. Rescale so 100% fader = 0 dB
    // (full-scale), matching MIX-page parity; 0 stays 0 = mute.
    constexpr uint32_t kEscarMax = 33;
    const auto to_wm = [](uint32_t v) {
        if (v > kEscarMax) v = kEscarMax;
        return (uint8_t)(v * wm8978::OUTPUT_VOLUME_0DB / kEscarMax);
    };
    codec.setOutputVolume(to_wm(left), to_wm(right));
}

void Codec::ReadBuffer(float *buf, uint32_t sz) {
    // sz = frames (BUF_SZ=32); 16-bit stereo.
    static int16_t raw[32 * 2];
    const size_t bytes = sz * 2 * sizeof(int16_t);
    size_t bytes_read = 0;
    if (i2s_channel_read(s_rx, raw, bytes, &bytes_read, pdMS_TO_TICKS(100)) != ESP_OK ||
        bytes_read != bytes) {
        // Zero-fill on error/timeout — the audio task must never block
        // forever on a dead bus (A1S semantics, SDD §5.2).
        for (uint32_t i = 0; i < 2 * sz; ++i) buf[i] = 0.0f;
        return;
    }
    constexpr float kScale = 3.0518509476E-5f; // 1/32767
    for (uint32_t i = 0; i < sz; ++i) {
        const float l = raw[2 * i] * kScale;
        const float r = raw[2 * i + 1] * kScale;
        if (s_mic_mode) {
            // Mono mic: only the left ADC carries signal (LIP/LIN); duplicate
            // so meters and plugins see a centered stereo image.
            buf[2 * i] = l;
            buf[2 * i + 1] = l;
        } else {
            buf[2 * i] = l;
            buf[2 * i + 1] = r;
        }
    }
}

void Codec::WriteBuffer(float *buf, uint32_t sz) {
    static int16_t raw[32 * 2];
    for (uint32_t i = 0; i < 2 * sz; ++i) {
        float v = buf[i];
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        raw[i] = (int16_t)(v * 32767.0f);
    }
    i2s_channel_write(s_tx, raw, sz * 2 * sizeof(int16_t), nullptr, pdMS_TO_TICKS(100));
}

void Codec::SetSampleRate(uint32_t sample_rate) {
    // API kept for interface parity; BBA2 is locked at 44.1 kHz (SDD §6.2).
    if (sample_rate != kSampleRate) {
        ESP_LOGW(TAG, "sample rate %lu requested — BBA2 is locked at %lu, ignoring",
                 (unsigned long)sample_rate, (unsigned long)kSampleRate);
    }
}

uint32_t Codec::GetSampleRate() { return kSampleRate; }

void Codec::SetInputSource(int sel) {
    // A1S convention preserved: 0 = mic, 1 = line (InputSource vocabulary
    // order "mic,line" — same mapping codec_bba.cpp uses). Also toggles the
    // mono-mic L->R duplication in ReadBuffer.
    s_mic_mode = (sel == 0);
    codec.inputSelect(s_mic_mode ? wm8978::Input::MIC : wm8978::Input::LINE);
}

void Codec::SetOutputSource(int sel) {
    // WM8978: speaker follows line out at the codec level (SDD §1) — both
    // output stages are always driven; nothing to switch.
    (void)sel;
}

void Codec::SetMixerMode(int mode) { (void)mode; }

void Codec::SetAnalogBypass(bool on) {
    // R50/51 (L/R output mixer): bit0 = DAC into mixer, bit1 = bypass into
    // mixer, bits[4:2] = bypass gain (5 = 0 dB). Port of legacy Output_Cfg.
    const uint16_t v = on ? (uint16_t)(0x002 | (5 << 2)) : 0x001;
    codec.writeReg(50, v);
    codec.writeReg(51, v);
}

void Codec::SetInputGain(int gain) {
    // A1S convention 0..8, 3 dB/step (0..+24 dB) — the MIX-page IN fader
    // scale. WM8978 PGA: 0.75 dB/step, 0 dB at code 16 → code = 16 + 4×gain.
    if (gain < 0) gain = 0;
    if (gain > 8) gain = 8;
    codec.setInputGain((uint8_t)(16 + 4 * gain));
}

const uint8_t *Codec::DebugReadAllRegs() {
    // WM8978 registers are 9-bit and the interface is write-only; the ES8388
    // 53-byte dump layout does not apply. Per-register reads work:
    return nullptr; // DebugUI falls back to DebugReadReg per register.
}

bool Codec::DebugReadReg(uint8_t reg, uint8_t &val) {
    uint16_t v = 0;
    if (!codec.debugReadReg(reg, v)) return false;
    val = (uint8_t)(v & 0xFF); // low 8 bits; DebugUI displays hex — fine
    return true;
}
