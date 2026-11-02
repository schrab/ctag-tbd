#pragma once

// WM8978 codec control driver for BBA2 (doc/bba2-esp32s3-sdd.md §5.1).
//
// 2-wire (I2C) control at 7-bit address 0x1A. The WM8978 register interface
// is WRITE-ONLY over I2C: every register is 9 bits, transmitted as
//   byte0 = (reg << 1) | ((val >> 8) & 1)
//   byte1 = val & 0xFF
// so the driver keeps a 58-entry shadow file; reads come from RAM only.
// Register map/power sequence ported from the legacy in-repo SPI driver
// (components/drivers/codec.cpp WM8978_* helpers, register-for-register).
//
// Board facts (SDD §2): no shutdown/enable pin (power = R1-R3 registers),
// stereo line in on L2/R2, mono mic on LIP/LIN via the input PGA,
// stereo line out on OUT1 (R52/53), mono speaker = OUT2 bridged (R43 bit4
// INVROUT2 + R54/55 volumes).

#include <stdint.h>

class wm8978 final {
private:
    uint16_t _regs[58]; // shadow register file (write-only bus)
    bool write_reg(uint8_t reg, uint16_t val);
    static bool ensure_bus();

public:
    // OUT1 (headphone/line) volume registers top out at 63; 0 dB = 57.
    // Codec::MAX_OUTPUT_LEVEL resolves to 63 on BBA2 via codec.hpp's
    // non-ES8388 branch, so keep the ceiling identical.
    static constexpr uint8_t OUTPUT_VOLUME_MAX = 63;
    static constexpr uint8_t OUTPUT_VOLUME_0DB = 57;

    enum class Input : uint8_t { LINE = 0, MIC = 1 };

    wm8978() { for (auto &r : _regs) r = 0; }
    ~wm8978() = default;

    bool init();        // full power-up + path bring-up, line in default
    bool identify();    // ACK on the R0 soft-reset write
    uint16_t *readAllReg() { return _regs; }
    bool debugReadReg(uint8_t reg, uint16_t &val) {
        if (reg > 57) return false;
        val = _regs[reg];
        return true;
    }

    bool setOutputVolume(uint8_t lvol, uint8_t rvol); // OUT1 0..63, 0 mutes
    bool setSpkVolume(uint8_t vol);                   // OUT2 bridge 0..63, 0 mutes
    bool inputSelect(Input sel);                      // swaps L2/R2 <-> PGA path
    bool setInputGain(uint8_t gain);                  // mic PGA 0..63 (-12..+35.25 dB)
    bool setMicBoost(bool on);                        // PGA input boost (+20 dB, R47/48 bit8)
    bool softMute(bool mute);                         // R5 DAC soft mute
    bool highPass(bool on);                           // R14 ADC HPF
    bool writeReg(uint8_t reg, uint16_t val);         // raw shadowed write (facade bypass paths)
};
