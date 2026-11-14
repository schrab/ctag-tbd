#include "codec_bba2.hpp"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <atomic>

using namespace CTAG::DRIVERS;

static const char *TAG = "CODEC_BBA2";

// BBA2 rev A pin map — as wired on the board (2026-11-04):
// I2S on the left-edge cluster, I2C on the hardwired codec header.
static constexpr gpio_num_t PIN_MCLK = GPIO_NUM_16;  // breakout MCK
static constexpr gpio_num_t PIN_BCLK = GPIO_NUM_15;  // breakout SCK
static constexpr gpio_num_t PIN_WS = GPIO_NUM_6;     // breakout FSA
static constexpr gpio_num_t PIN_DOUT = GPIO_NUM_17;  // S3 -> WM8978 DACDAT (breakout DAT)
static constexpr gpio_num_t PIN_DIN = GPIO_NUM_7;    // WM8978 ADCDAT -> S3 (breakout SDB)

static constexpr uint32_t kSampleRate = 44100; // default; Elements switches to 32 kHz

static i2s_chan_handle_t s_tx = nullptr;
static i2s_chan_handle_t s_rx = nullptr;
static bool s_mic_mode = false; // mono mic: duplicate left into right
static uint32_t s_sample_rate = kSampleRate;
// Bring-up diagnostics (REMOVE): failed/short I2S transfers. The audio task
// ignores WriteBuffer's return by design (it must never block forever); these
// counters make a silent write failure visible — a wedged TX DMA repeats its
// last buffer content = harsh noise while the render telemetry stays clean.
static std::atomic<uint32_t> s_readErrors{0};
static std::atomic<uint32_t> s_writeErrors{0};

wm8978 Codec::codec;

namespace {
// The GDMA ISR is allocated on the core that runs the I2S driver calls that
// install it (channel creation AND init/enable). The audio task lives on
// core 1 — run the I2S bring-up on core 1 so the GDMA ISR serves the audio
// core directly without cross-core interrupt hops.
struct I2sBringup {
    SemaphoreHandle_t done;
    bool ok;
};
void i2sBringupTask(void *arg) {
    auto *b = static_cast<I2sBringup *>(arg);
    i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    // 4 descriptors x 32 frames = 2.9 ms DMA horizon @ 44.1 kHz (4.0 ms @
    // 32 kHz), matching A1S latency parity (SDD §5.2).
    chan_cfg.dma_desc_num = 4;
    chan_cfg.dma_frame_num = 32; // 32-sample block, matches BUF_SZ
    b->ok = (i2s_new_channel(&chan_cfg, &s_tx, &s_rx) == ESP_OK);

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

    if (b->ok) {
        b->ok = (i2s_channel_init_std_mode(s_tx, &std_cfg) == ESP_OK &&
                 i2s_channel_init_std_mode(s_rx, &std_cfg) == ESP_OK &&
                 i2s_channel_enable(s_tx) == ESP_OK &&
                 i2s_channel_enable(s_rx) == ESP_OK);
    }
    xSemaphoreGive(b->done);
    vTaskDelete(nullptr);
}
} // namespace

void Codec::InitCodec() {
    ESP_LOGI(TAG, "Init WM8978 over I2C + I2S std master");

    if (!codec.init()) {
        ESP_LOGE(TAG, "WM8978 init failed — audio will be silent");
        // I2S still comes up so the pipeline never blocks.
    }

    // Run the I2S bring-up (and thus the GDMA ISR allocation) on core 1.
    I2sBringup bringup{};
    bringup.done = xSemaphoreCreateBinary();
    if (bringup.done != nullptr &&
        xTaskCreatePinnedToCore(i2sBringupTask, "i2s_init", 4096, &bringup,
                                5, nullptr, 1) == pdPASS) {
        xSemaphoreTake(bringup.done, portMAX_DELAY);
        vSemaphoreDelete(bringup.done);
    } else {
        ESP_LOGE(TAG, "core-1 I2S bringup task failed — creating on core 0");
        i2sBringupTask(&bringup); // fallback: run inline (should not happen)
    }
    ESP_ERROR_CHECK(bringup.ok ? ESP_OK : ESP_FAIL);
}

void Codec::HighPassEnable() { codec.highPass(true); }
void Codec::HighPassDisable() { codec.highPass(false); }

// WM8978 has no ES8388-style DC-offset register; the software DC-cut in
// audio_task remains the real filter (SDD §5.2).
void Codec::RecalibDCOffset() {}

void Codec::SetOutputLevels(const uint32_t left, const uint32_t right) {
    // NATIVE scale, no rescaling: the panel/CONFIG range is the WM8978's own
    // 0..63 (0 dB at 57, OUTPUT_VOLUME_0DB) — Codec::MAX_OUTPUT_LEVEL mirrors
    // it so every source clamps before we get here. 0 stays 0 = mute.
    uint32_t l = left > 63 ? 63 : left;
    uint32_t r = right > 63 ? 63 : right;
    codec.setOutputVolume((uint8_t)l, (uint8_t)r);
}

void Codec::ReadBuffer(float *buf, uint32_t sz) {
    // sz = frames (BUF_SZ=32); 16-bit stereo.
    static int16_t raw[32 * 2];
    const size_t bytes = sz * 2 * sizeof(int16_t);
    size_t bytes_read = 0;
    // NOTE: i2s_channel_read/write take timeout_ms in MILLISECONDS and call
    // pdMS_TO_TICKS(timeout_ms) internally. NEVER pass pdMS_TO_TICKS(100) here:
    // at CONFIG_FREERTOS_HZ=100 that double-converts to 1 tick (10 ms), which
    // expires on Core 0's SysTick and removes audio_task from msg_queue's wait
    // list without sending a cross-core yield IPI, stalling audio_task for 5 ms
    // until Core 1's half-period-shifted SysTick fires.
    if (i2s_channel_read(s_rx, raw, bytes, &bytes_read, 100) != ESP_OK ||
        bytes_read != bytes) {
        // Zero-fill on error/timeout — the audio task must never block
        // forever on a dead bus (A1S semantics, SDD §5.2).
        s_readErrors.fetch_add(1, std::memory_order_relaxed);
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
    size_t bytes_written = 0;
    const size_t want = sz * 2 * sizeof(int16_t);
    if (i2s_channel_write(s_tx, raw, want, &bytes_written, 100) != ESP_OK ||
        bytes_written != want) {
        s_writeErrors.fetch_add(1, std::memory_order_relaxed);
    }
}

void Codec::SetSampleRate(uint32_t sample_rate) {
    // Elements runs at 32 kHz (native rate — the struck-block render worst
    // case fits the 240k @ 32 kHz block budget; hardware-measured 2026-11-04,
    // SDD §6.2 revised). Everything else stays at 44.1 kHz. Reconfigure the
    // I2S clocks live, mirroring codec_bba.cpp's SetSampleRate.
    if (sample_rate != 32000 && sample_rate != 44100) {
        ESP_LOGW(TAG, "sample rate %lu unsupported — ignoring",
                 (unsigned long)sample_rate);
        return;
    }
    if (sample_rate == s_sample_rate || s_tx == nullptr) return;

    esp_err_t dis_tx = i2s_channel_disable(s_tx);
    esp_err_t dis_rx = i2s_channel_disable(s_rx);

    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256; // 11.2896 MHz @ 44.1k / 8.192 MHz @ 32k
    ESP_ERROR_CHECK(i2s_channel_reconfig_std_clock(s_tx, &clk_cfg));
    ESP_ERROR_CHECK(i2s_channel_reconfig_std_clock(s_rx, &clk_cfg));

    esp_err_t en_tx = i2s_channel_enable(s_tx);
    esp_err_t en_rx = i2s_channel_enable(s_rx);
    if (dis_tx == ESP_OK && dis_rx == ESP_OK && en_tx == ESP_OK && en_rx == ESP_OK) {
        s_sample_rate = sample_rate;
        ESP_LOGI(TAG, "sample rate switched to %lu", (unsigned long)sample_rate);
    } else {
        ESP_LOGE(TAG, "sample rate switch failed: dis=%d/%d en=%d/%d",
                 dis_tx, dis_rx, en_tx, en_rx);
    }
}

uint32_t Codec::GetSampleRate() { return s_sample_rate; }

uint32_t Codec::DebugI2sReadErrors() { return s_readErrors.load(std::memory_order_relaxed); }
uint32_t Codec::DebugI2sWriteErrors() { return s_writeErrors.load(std::memory_order_relaxed); }

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

void Codec::SetInputGainNative(int gain) {
    // NATIVE WM8978 PGA scale 0..63 (-12..+35.25 dB, 0 dB at 16) — the
    // io-caps "inlvlmax" contract; the MIX fader steps this directly.
    if (gain < 0) gain = 0;
    if (gain > 63) gain = 63;
    codec.setInputGain((uint8_t)gain);
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

bool Codec::DebugWriteReg(uint8_t reg, uint16_t val, uint16_t &readback) {
    if (!codec.writeReg(reg, val)) return false;
    readback = 0;
    codec.debugReadReg(reg, readback); // full 9-bit shadow echo
    return true;
}
