#ifndef MAIN_ES8388_HPP
#define MAIN_ES8388_HPP

// from https://github.com/vanbwodonk/es8388arduino/blob/main/src/ES8388.h

#include <stdint.h>

typedef enum {
    MIXIN1,  // direct line 1
    MIXIN2,  // direct line 2
    MIXRES,  // reserverd es8388
    MIXADC   // Select from ADC/ALC
} mixsel_t;

typedef enum {
    OUT1,    // ES8388 LOUT1/ROUT1 (on the A1S module: speaker)
    OUT2,    // ES8388 LOUT2/ROUT2 (on the A1S module: headphones)
    OUTALL,  // Enable both output pairs
} outsel_t;

typedef enum {
    IN1,       // Single-ended LINE IN 1 (LIN1 on left, RIN1 on right)
    IN2,       // Single-ended LINE IN 2 (LIN2 on left, RIN2 on right)
    IN1DIFF,   // Differential IN1 on both ADCs (mono, duplicated L/R)
    IN2DIFF,   // Differential IN2 on both ADCs (mono, duplicated L/R)
    INSTEREO   // Differential stereo: IN1 -> left ADC, IN2 -> right ADC
} insel_t;

typedef enum {
    DACOUT,     // Select Sink From DAC
    SRCSELOUT,  // Select Sink From SourceSelect()
    MIXALL,     // Sink ALL DAC & SourceSelect()
} mixercontrol_t;

typedef enum {
    DISABLE,  // Disable ALC
    GENERIC,  // Generic Mode
    VOICE,    // Voice Mode
    MUSIC     // Music Mode
} alcmodesel_t;

class es8388 final{
private:
    outsel_t _outSel = OUTALL;
    insel_t _inSel = IN1;
    uint8_t _pinsda, _pinscl;
    uint32_t _i2cspeed;
    bool write_reg(uint8_t reg_add, uint8_t data);
    bool read_reg(uint8_t reg_add, uint8_t& data);

public:
    // LOUT1/2 and ROUT1/2 volume registers top out at 33 (0 dB); higher codes
    // are reserved. Codec::MAX_OUTPUT_LEVEL aliases this for the UI/config
    // clamping, so keep it the only definition of the ceiling.
    static constexpr uint8_t OUTPUT_VOLUME_MAX = 33;

    es8388();
    ~es8388();
    bool init();
    bool identify();
    uint8_t* readAllReg();
    bool debugReadReg(uint8_t reg, uint8_t &val) { return read_reg(reg, val); }
    bool outputSelect(outsel_t sel);
    bool inputSelect(insel_t sel);
    bool DACmute(bool mute);
    uint8_t getOutputVolume();
    bool setOutputVolume(uint8_t vol);
    bool setOutputVolume(uint8_t lvol, uint8_t rvol);
    uint8_t getInputGain();
    bool setInputGain(uint8_t gain);
    bool setALCmode(alcmodesel_t alc);
    bool mixerSourceSelect(mixsel_t LMIXSEL, mixsel_t RMIXSEL);
    bool mixerSourceControl(bool LD2LO, bool LI2LO, uint8_t LI2LOVOL, bool RD2RO,
                            bool RI2RO, uint8_t RI2LOVOL);
    bool mixerSourceControl(mixercontrol_t mix);
    bool analogBypass(bool bypass);
};


#endif //MAIN_ES8388_HPP
