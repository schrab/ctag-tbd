#include "wm8978.hpp"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"

// BBA2 rev A I2C pins: codec header hardwired to IO4/IO5.
static constexpr int WM8978_SDA_PIN = 4;
static constexpr int WM8978_SCL_PIN = 5;
static constexpr uint8_t WM8978_I2C_ADDR = 0x1A; // 7-bit, 2-wire mode
static constexpr uint32_t WM8978_I2C_SPEED_HZ = 400000;

static i2c_master_bus_handle_t s_bus = nullptr;
static i2c_master_dev_handle_t s_dev = nullptr;
static const char *TAG = "WM8978";

bool wm8978::ensure_bus() {
    if (s_bus != nullptr && s_dev != nullptr) return true;
    i2c_master_bus_config_t cfg = {};
    cfg.i2c_port = -1; // auto-select, same strategy as es8388.cpp
    cfg.sda_io_num = (gpio_num_t)WM8978_SDA_PIN;
    cfg.scl_io_num = (gpio_num_t)WM8978_SCL_PIN;
    cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    cfg.glitch_ignore_cnt = 7;
    cfg.intr_priority = 1;
    cfg.flags.enable_internal_pullup = 1;
    if (i2c_new_master_bus(&cfg, &s_bus) != ESP_OK) {
        ESP_LOGE(TAG, "i2c bus init failed");
        return false;
    }
    i2c_device_config_t dev = {};
    dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev.device_address = WM8978_I2C_ADDR;
    dev.scl_speed_hz = WM8978_I2C_SPEED_HZ;
    if (i2c_master_bus_add_device(s_bus, &dev, &s_dev) != ESP_OK) {
        ESP_LOGE(TAG, "i2c device add failed");
        return false;
    }
    return true;
}

bool wm8978::write_reg(uint8_t reg, uint16_t val) {
    if (reg > 57) return false;
    if (!ensure_bus()) return false;
    uint8_t buf[2] = {
        (uint8_t)((reg << 1) | ((val >> 8) & 0x01)),
        (uint8_t)(val & 0xFF),
    };
    // Bounded retries: control calls can originate off the audio task while
    // the bus is busy (same reasoning as es8388.cpp).
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (i2c_master_transmit(s_dev, buf, sizeof(buf), 100 /*ms*/) == ESP_OK) {
            _regs[reg] = val & 0x1FF; // 9-bit registers
            return true;
        }
        esp_rom_delay_us(1000);
    }
    ESP_LOGW(TAG, "write R%u=0x%03X failed", reg, val);
    return false;
}

bool wm8978::identify() {
    if (!ensure_bus()) return false;
    // R0 write = soft reset; an ACK is the only presence probe the
    // write-only interface offers.
    return write_reg(0, 0);
}

bool wm8978::init() {
    if (!identify()) {
        ESP_LOGE(TAG, "no ACK on reset write — codec missing?");
        return false;
    }

    // Power-up + path bring-up. Values ported from the legacy
    // components/drivers/codec.cpp WM8978_Init / helpers.
    write_reg(1, 0x00F);   // R1: BUFIOEN, VMIDSEL=11, BIASEN
    write_reg(2, 0x1BF);   // R2: ROUT1/LOUT1 en, BOOST en, ADC en, PGA en
    write_reg(3, 0x00F);   // R3: LMIX/RMIX en, LOUT2/ROUT2 (speaker) en
    write_reg(4, 0x010);   // R4: I2S format (fmt=2 <<3), 16-bit, slave
    write_reg(6, 0x000);   // R6: MCLK direct from S3, no PLL, no divide
    write_reg(10, 0x008);  // R10: DAC OSR 128x, soft mute off
    write_reg(14, 0x108);  // R14: ADC OSR 128x, HPF on
    write_reg(43, 0x010);  // R43: INVROUT2 — bridge OUT2 for the mono speaker
    write_reg(49, 0x002);  // R49: thermal shutdown on
    write_reg(50, 0x001);  // R50/51: L/R output mixer = DAC, no bypass
    write_reg(51, 0x001);

    // Default paths: line in (L2/R2, 0 dB), OUT1 at 0 dB, speaker at 0 dB.
    inputSelect(Input::LINE);
    setOutputVolume(OUTPUT_VOLUME_0DB, OUTPUT_VOLUME_0DB);
    setSpkVolume(OUTPUT_VOLUME_0DB);
    ESP_LOGI(TAG, "WM8978 ready (line in, OUT1+OUT2 at 0 dB)");
    return true;
}

bool wm8978::setOutputVolume(uint8_t lvol, uint8_t rvol) {
    lvol &= 0x3F;
    rvol &= 0x3F;
    if (lvol == 0) lvol |= 0x40; // 0 = mute
    if (rvol == 0) rvol |= 0x40;
    const bool ok = write_reg(52, lvol);
    // R53 bit8 (HPVU) latches both channels simultaneously.
    return write_reg(53, rvol | 0x100) && ok;
}

bool wm8978::setSpkVolume(uint8_t vol) {
    vol &= 0x3F;
    if (vol == 0) vol |= 0x40;
    const bool ok = write_reg(54, vol);
    return write_reg(55, vol | 0x100) && ok; // SPKVU latch
}

bool wm8978::inputSelect(Input sel) {
    // R47/48: bits[6:4] = L2/R2 -> boost (line in), bit8 = PGA input boost,
    //         bits[2:0] = AUXL/AUXR (unused on BBA2).
    // R44:    bits 0/4 = LIP/LIN -> PGA (mic path).
    // R2:     bits 3:2 = INPPGAENR/L.
    // Mask 0x170 clears the boost bit AND the line-in nibble — leaving the
    // nibble set (0x0F3 bug) would keep L2/R2 connected at 0 dB into the
    // boost mixer and bleed line-in into mic captures.
    uint16_t r47 = _regs[47] & (uint16_t)~0x170;
    uint16_t r48 = _regs[48] & (uint16_t)~0x170;
    uint16_t r44 = _regs[44] & ~0x33;
    uint16_t r2 = _regs[2] & ~0x00C;
    if (sel == Input::LINE) {
        // Line in: L2/R2 at 0 dB (gain 5 = 0 dB per legacy LINEIN_Gain).
        r47 |= (5 << 4);
        r48 |= (5 << 4);
    } else {
        // Mic: LIP/LIN into the PGA, PGA enabled, boost on (mono mic needs it).
        r44 |= 0x11; // LIP2INPPGA | RIP2INPPGA (bits 4 and 0)
        r2 |= 0x00C; // both PGAs enabled
        r47 |= 0x100;
        r48 |= 0x100;
        setInputGain(40); // sane default ~+18 dB, MIX page adjusts from here
    }
    bool ok = write_reg(44, r44);
    ok = write_reg(2, r2) && ok;
    ok = write_reg(47, r47) && ok;
    return write_reg(48, r48) && ok;
}

bool wm8978::setInputGain(uint8_t gain) {
    gain &= 0x3F;
    const bool ok = write_reg(45, gain);             // LPGA
    return write_reg(46, gain | 0x100) && ok;        // RPGA + update latch
}

bool wm8978::setMicBoost(bool on) {
    uint16_t r47 = _regs[47], r48 = _regs[48];
    if (on) { r47 |= 0x100; r48 |= 0x100; }
    else    { r47 &= (uint16_t)~0x100; r48 &= (uint16_t)~0x100; }
    const bool ok = write_reg(47, r47);
    return write_reg(48, r48) && ok;
}

bool wm8978::softMute(bool mute) {
    uint16_t r5 = _regs[5] & (uint16_t)~0x001;
    if (mute) r5 |= 0x001;
    return write_reg(5, r5);
}

bool wm8978::highPass(bool on) {
    uint16_t r14 = (_regs[14] & (uint16_t)~0x001) | (on ? 0x001 : 0);
    return write_reg(14, r14);
}

bool wm8978::writeReg(uint8_t reg, uint16_t val) {
    return write_reg(reg, val);
}
