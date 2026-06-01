# ES8388 Low Power Stereo Audio CODEC Datasheet (Pages 6-36)

## 2. 28-PIN QFN AND PIN DESCRIPTIONS

ES8388 is pin and size compatible to WM8988.

| Pin | Name | I/O | Description |
|-----|------|-----|-------------|
| 1 | MCLK | I | Master clock |
| 2 | DVDD | Supply | Digital core supply |
| 3 | PVDD | Supply | Digital IO supply |
| 4 | DGND | Supply | Digital ground (return path for both DVDD and PVDD) |
| 5 | SCLK | I/O | Audio data bit clock |
| 6 | DSDIN | I | DAC audio data |
| 7 | LRCK | I/O | Audio data left and right clock |
| 8 | ASDOUT | O | ADC audio data |
| 9 | NC | - | No connect |
| 10 | VREFO | - | Decoupling capacitor |
| 11 | ROUT1 | O | Right output 1 (line or speaker/headphone) |
| 12 | LOUT1 | O | Left output 1 (line or speaker/headphone) |
| 13 | HPGND | Supply | Ground for analog output drivers (LOUT1/2, ROUT1/2) |
| 14 | ROUT2 | O | Right output 2 (line or speaker/headphone) |
| 15 | LOUT2 | O | Left output 2 (line or speaker/headphone) |
| 16 | HPVDD | Supply | Supply for analog output drivers (LOUT1/2, ROUT1/2) |
| 17 | AVDD | Supply | Analog supply |
| 18 | AGND | Supply | Analog ground |
| 19 | ADCVREFO | - | Decoupling capacitor |
| 20 | VMIDO | - | Decoupling capacitor |
| 21 | RIN2 | A | Right channel input 2 |
| 22 | LIN2 | A | Left channel input 2 |
| 23 | RIN1 | A | Right channel input 1 |
| 24 | LIN1 | A | Left channel input 1 |
| 25 | NC | - | No connect |
| 26 | CE | I | Control select or device address selection |
| 27 | CDATA | I/O | Control data input or output |
| 28 | CCLK | I | Control clock input |

## 3. TYPICAL APPLICATION CIRCUIT

*(Refer to original datasheet for circuit diagram)*

## 4. CLOCK MODES AND SAMPLING FREQUENCIES

According to the input serial audio data sampling frequency, the device can work in two speed modes: single speed or double speed. The ranges of the sampling frequency in these two modes are listed in Table 1. The device can work either in master clock mode or slave clock mode.

In slave mode, LRCK and SCLK are supplied externally. LRCK and SCLK must be synchronously derived from the system clock with specific rates. The device can auto detect MCLK/LRCK ratio according to Table 1. The device only supports the MCLK/LRCK ratios listed in Table 1. The LRCK/SCLK ratio is normally 64.

**Table 1 Slave Mode Sampling Frequencies and MCLK/LRCK Ratio**

| Speed Mode | Sampling Frequency | MCLK/LRCK Ratio |
|------------|--------------------|------------------|
| Single Speed | 8kHz – 50kHz | 256, 384, 512, 768, 1024 |
| Double Speed | 50kHz – 100kHz | 128, 192, 256, 384, 512 |

In master mode, LRCK and SCLK are derived internally from MCLK. The available MCLK/LRCK ratios and SCLK/LRCK ratios are listed in Table 2.

**Table 2 Master Mode Sampling Frequencies and MCLK/LRCK Ratio**

*(Refer to original datasheet for full master mode frequency table)*

Key master mode settings:
- MCLK = 12.288 MHz or 24.576 MHz for standard rates
- MCLK = 11.2896 MHz or 22.5792 MHz for 44.1k family
- USB mode supports 8kHz, 48kHz, 44.118kHz, etc.

## 5. MICRO-CONTROLLER CONFIGURATION INTERFACE

The device supports standard SPI and 2-wire micro-controller configuration interface. The same pins are used:
- SPI mode: CE (CSn), CCLK (SCLK), CDATA (DIN)
- 2-wire mode: CE (AD0), CCLK (SCL), CDATA (SDA)

To select SPI mode, apply high-to-low transition to CE pin. Otherwise the device operates in 2-wire interface mode.

### 5.1 SPI

- Unidirectional lines: SPI_CLK, SPI_DIN, SPI_CSn
- Each write procedure: 3 words of 8 bits each (Chip Address+R/W, Register Address, Data)
- Data sampled on rising edge of SPI_CLK, MSB first
- Transfer rate up to 10M bps

### 5.2 2-wire (I2C)

- Bi-directional serial bus: SDA and SCL
- Transfer rate up to 400 kbps
- 7-bit chip address: 001000x (where x = AD0, pin CE)
- R/W bit indicates transfer direction
- Supports both write and read operations

**Table 3 Write Data to Register in 2-wire Interface Mode**

| Start | Chip Address | R/W=0 | ACK | Register Address | ACK | Data | ACK | Stop |
|-------|--------------|-------|-----|------------------|-----|------|-----|------|

**Table 4 Read Data from Register in 2-wire Interface Mode**

| Start | Chip Address | R/W=0 | ACK | Register Address | ACK | Start | Chip Address | R/W=1 | ACK | Data | NACK | Stop |
|-------|--------------|-------|-----|------------------|-----|-------|--------------|-------|-----|------|------|------|

## 6. CONFIGURATION REGISTER DEFINITION

Total of 53 user programmable 8-bit registers. SPI and 2-wire share the same register map.

**Table 5 Bit Content of Register Address Map (Summary)**

| Reg | B7 | B6 | B5 | B4 | B3 | B2 | B1 | B0 |
|-----|----|----|----|----|----|----|----|----|
| 00 | SCPReset | LRCM | DACMCLK | SameFs | SeqEn | EnRef | VMIDSEL[1:0] |
| 01 | LPVcmMod | LPVrefBuf | PdnAna | PdnIbiasgen | VrefLo | PdnVrefbuf |
| 02 | adc_DigPDN | dac_DigPDN | adc_stm_rst | dac_stm_rst | ADCDLL_PDN | DACDLL_PDN | adcVref_PDN | dacVref_PDN |
| 03 | PdnAINL | PdnAINR | PdnADCL | PdnADCR | PdnMICB | PdnADCBiasgen | flashLP | int1LP |
| 04 | PdnDACL | PdnDACR | LOUT1 | ROUT1 | LOUT2 | ROUT2 | - | - |
| 05 | LPDACL | LPDACR | LPLOUT1 | - | LPLOUT2 | - | - | - |
| 06 | LPPGA | LPLMIX | - | - | - | - | LPADCvrp | LPDACvrp |
| 07 | - | VSEL[6:0] | - | - | - | - | - | - |
| 08 | MSC | MCLKDIV2 | BCLK_INV | BCLKDIV[4:0] |
| 09 | MicAmpL[3:0] | MicAmpR[3:0] |
| 10 | LINSEL[1:0] | RINSEL[1:0] | DSSEL | DSR |
| 11 | DS | - | MONOMIX[1:0] | TRI |
| 12 | DATSEL[1:0] | ADCLRP | ADCWL[2:0] | ADCFORMAT[1:0] |
| 13 | - | - | ADCFsMode | ADCFsRatio[4:0] |
| 14 | ADC_invL | ADC_invR | ADC_HPF_L | ADC_HPF_R | - | - | - | - |
| 15 | ADCRampRate[1:0] | ADCSoftRamp | - | ADCLer | ADCMute | - | - |
| 16 | LADCVOL[7:0] |
| 17 | RADCVOL[7:0] |
| 18 | ALCSEL[1:0] | MAXGAIN[2:0] | MINGAIN[2:0] |
| 19 | ALCLVL[3:0] | ALCHLD[3:0] |
| 20 | ALCDCY[3:0] | ALCATK[3:0] |
| 21 | ALCMODE | ALCZC | TIME_OUT | WIN_SIZE[4:0] |
| 22 | NGTH[4:0] | NGG[1:0] | NGAT |
| 23 | DACLRSWAP | DACLRP | - | DACWL[2:0] | DACFORMAT[1:0] |
| 24 | - | - | DACFsMode | DACFsRatio[4:0] |
| 25 | DACRampRate[1:0] | DACSoftRamp | - | DACLeR | DACMute | - | - |
| 26 | LDACVOL[7:0] |
| 27 | RDACVOL[7:0] |
| 28 | DeemphasisMode[1:0] | DAC_invL | DAC_invR | ClickFree | - | - | - |
| 29 | ZeroL | ZeroR | Mono | SE[2:0] | Vpp_scale[1:0] |
| 30 | Shelving_a[29:24] |
| 31 | Shelving_a[23:16] |
| 32 | Shelving_a[15:8] |
| 33 | Shelving_a[7:0] |
| 34 | Shelving_b[29:24] |
| 35 | Shelving_b[23:16] |
| 36 | Shelving_b[15:8] |
| 37 | Shelving_b[7:0] |
| 38 | - | - | LMIXSEL[2:0] | RMIXSEL[2:0] |
| 39 | LD2LO | LI2LO | LI2LOVOL[2:0] |
| 40 | (reserved) |
| 41 | (reserved) |
| 42 | RD2RO | RI2RO | RI2ROVOL[2:0] |
| 43 | slrck | lrck_sel | offset_dis | mclk_dis | adc_dll_pwd | dac_dll_pwd | - | - |
| 44 | offset[7:0] |
| 45 | - | - | - | VROI | - | - | - | - |
| 46 | LOUT1VOL[5:0] |
| 47 | ROUT1VOL[5:0] |
| 48 | LOUT2VOL[5:0] |
| 49 | ROUT2VOL[5:0] |
| 50 | (reserved) |
| 51 | hpLout1_ref1 | hpLout1_ref2 | - | - | - | - | - | - |
| 52 | spkLout2_ref1 | spkLout2_ref2 | mixer_ref1 | mixer_ref2 | MREF1 | MREF2 | - | - |

### 6.1 Chip Control and Power Management

#### 6.1.1 Register 0 - Chip Control 1, Default 0000 0110

| Bit | Name | Description |
|-----|------|-------------|
| 7 | SCPReset | 0: normal, 1: reset control port registers to default |
| 6 | LRCM | 0: ALRCK disabled when both ADC disabled; DLRCK disabled when both DAC disabled (default); 1: ALRCK and DLRCK disabled when all ADC and DAC disabled |
| 5 | DACMCLK | 0: when SameFs=1, ADCMCLK is master clock source; 1: when SameFs=1, DACMCLK is master clock source |
| 4 | SameFs | 0: ADC Fs differs from DAC Fs; 1: ADC Fs same as DAC Fs |
| 3 | SeqEn | 0: internal power up/down sequence disable; 1: enable |
| 2 | EnRef | 0: disable reference; 1: enable reference (default) |
| 1:0 | VMIDSEL | 00: Vmid disabled; 01: 50kΩ divider; 10: 500kΩ divider (default); 11: 5kΩ divider |

#### 6.1.2 Register 1 - Chip Control 2, Default 0101 1100

| Bit | Name | Description |
|-----|------|-------------|
| 5 | LPVcmMod | 0: normal, 1: low power |
| 4 | LPVrefBuf | 0: normal, 1: low power (default) |
| 3 | PdnAna | 0: normal, 1: entire analog power down (default) |
| 2 | PdnIbiasgen | 0: normal, 1: ibiasgen power down (default) |
| 1 | VrefLo | 0: normal, 1: low power |
| 0 | PdnVrefbuf | 0: normal, 1: power down |

#### 6.1.3 Register 2 - Chip Power Management, Default 1100 0011

| Bit | Name | Description |
|-----|------|-------------|
| 7 | adc_DigPDN | 0: normal, 1: resets ADC DEM, filter and serial data port (default) |
| 6 | dac_DigPDN | 0: normal, 1: resets DAC DSM, DEM, filter and serial data port (default) |
| 5 | adc_stm_rst | 0: normal, 1: reset ADC state machine to power down state |
| 4 | dac_stm_rst | 0: normal, 1: reset DAC state machine to power down state |
| 3 | ADCDLL_PDN | 0: normal, 1: ADC DLL power down, stop ADC clock |
| 2 | DACDLL_PDN | 0: normal, 1: DAC DLL power down, stop DAC clock |
| 1 | adcVref_PDN | 0: ADC analog reference power up; 1: power down (default) |
| 0 | dacVref_PDN | 0: DAC analog reference power up; 1: power down (default) |

#### 6.1.4 Register 3 - ADC Power Management, Default 1111 1100

| Bit | Name | Description |
|-----|------|-------------|
| 7 | PdnAINL | 0: normal, 1: left analog input power down (default) |
| 6 | PdnAINR | 0: normal, 1: right analog input power down (default) |
| 5 | PdnADCL | 0: left ADC power up; 1: power down (default) |
| 4 | PdnADCR | 0: right ADC power up; 1: power down (default) |
| 3 | PdnMICB | 0: microphone bias on; 1: power down (default) |
| 2 | PdnADCBiasgen | 0: normal, 1: power down (default) |
| 1 | flashLP | 0: normal, 1: flash ADC low power |
| 0 | int1LP | 0: normal, 1: int1 low power |

#### 6.1.5 Register 4 - DAC Power Management, Default 1100 0000

| Bit | Name | Description |
|-----|------|-------------|
| 7 | PdnDACL | 0: left DAC power up; 1: power down (default) |
| 6 | PdnDACR | 0: right DAC power up; 1: power down (default) |
| 5 | LOUT1 | 0: disabled (default), 1: enabled |
| 4 | ROUT1 | 0: disabled (default), 1: enabled |
| 3 | LOUT2 | 0: disabled (default), 1: enabled |
| 2 | ROUT2 | 0: disabled (default), 1: enabled |

#### 6.1.6 Register 5 - Chip Low Power 1, Default 0000 0000

| Bit | Name | Description |
|-----|------|-------------|
| 7 | LPDACL | 0: normal, 1: low power |
| 6 | LPDACR | 0: normal, 1: low power |
| 5 | LPLOUT1 | 0: normal, 1: low power |
| 3 | LPLOUT2 | 0: normal, 1: low power |

#### 6.1.7 Register 6 - Chip Low Power 2, Default 0000 0000

| Bit | Name | Description |
|-----|------|-------------|
| 7 | LPPGA | 0: normal, 1: low power |
| 6 | LPLMIX | 0: normal, 1: low power |
| 1 | LPADCvrp | 0: normal, 1: low power |
| 0 | LPDACvrp | 0: normal, 1: low power |

#### 6.1.8 Register 7 - Analog Voltage Management, Default 0111 1100

| Bit | Name | Description |
|-----|------|-------------|
| 6:0 | VSEL | 1111100: normal (default) |

#### 6.1.9 Register 8 - Master Mode Control, Default 1000 0000

| Bit | Name | Description |
|-----|------|-------------|
| 7 | MSC | 0: slave serial port mode; 1: master serial port mode (default) |
| 6 | MCLKDIV2 | 0: MCLK not divide (default); 1: MCLK divide by 2 |
| 5 | BCLK_INV | 0: normal (default); 1: BCLK inverted |
| 4:0 | BCLKDIV | 00000: automatic (default); others: MCLK/1, /2, /3, /4, /6, /8, /9, /11, /12, /16, /18, /22, /24, /33, /36, /44, /48, /66, /72, /5, /10, /15, /17, /20, /25, /30, /32, /34, else /4 |

### 6.2 ADC Control

#### 6.2.1 Register 9 - ADC Control 1, Default 0000 0000

| Bit | Name | Description |
|-----|------|-------------|
| 7:4 | MicAmpL | Left PGA gain: 0000=0dB, 0001=+3dB, 0010=+6dB, 0011=+9dB, 0100=+12dB, 0101=+15dB, 0110=+18dB, 0111=+21dB, 1000=+24dB |
| 3:0 | MicAmpR | Right PGA gain: same encoding |

#### 6.2.2 Register 10 - ADC Control 2, Default 0000 0000

| Bit | Name | Description |
|-----|------|-------------|
| 7:6 | LINSEL | Left input select: 00=INPUT1, 01=INPUT2, 10=reserved, 11=L-R differential |
| 5:4 | RINSEL | Right input select: 00=INPUT1, 01=INPUT2, 10=reserved, 11=L-R differential |
| 3 | DSSEL | 0: use one DS (Reg11[7]), 1: DSL=Reg11[7], DSR=Reg10[2] |
| 2 | DSR | Differential input select: 0=INPUT1-RINPUT1, 1=INPUT2-RINPUT2 |

#### 6.2.3 Register 11 - ADC Control 3, Default 0000 0010

| Bit | Name | Description |
|-----|------|-------------|
| 7 | DS | Differential input select: 0=INPUT1-RINPUT1, 1=INPUT2-RINPUT2 |
| 4:3 | MONOMIX | 00=stereo, 01=analog mono mix to left ADC, 10=analog mono mix to right ADC, 11=reserved |
| 2 | TRI | 0=ASDOUT normal output, 1=ASDOUT tri-stated (ALRCK, DLRCK, SCLK inputs) |

#### 6.2.4 Register 12 - ADC Control 4, Default 0000 0000

| Bit | Name | Description |
|-----|------|-------------|
| 7:6 | DATSEL | 00: left data=left ADC, right data=right ADC; 01: left=left, right=left; 10: left=right, right=right; 11: left=right, right=left |
| 5 | ADCLRP | I2S/LJ/RJ: 0=normal polarity, 1=inverted; DSP/PCM: 0=MSB on 2nd BCLK after LRCK rise, 1=MSB on 1st BCLK after LRCK rise |
| 4:2 | ADCWL | 000=24-bit, 001=20-bit, 010=18-bit, 011=16-bit, 100=32-bit |
| 1:0 | ADCFORMAT | 00=I2S, 01=left justified, 10=right justified, 11=DSP/PCM |

#### 6.2.5 Register 13 - ADC Control 5, Default 0000 0110

| Bit | Name | Description |
|-----|------|-------------|
| 5 | ADCFsMode | 0=single speed, 1=double speed |
| 4:0 | ADCFsRatio | Master mode ADC MCLK to sampling frequency ratio (see datasheet for values) |

#### 6.2.6 Register 14 - ADC Control 6, Default 0011 0000

| Bit | Name | Description |
|-----|------|-------------|
| 7 | ADC_invL | 0=normal, 1=left channel polarity inverted |
| 6 | ADC_invR | 0=normal, 1=right channel polarity inverted |
| 5 | ADC_HPF_L | 0=disable, 1=enable left HPF (default) |
| 4 | ADC_HPF_R | 0=disable, 1=enable right HPF (default) |

#### 6.2.7 Register 15 - ADC Control 7, Default 0010 0000

| Bit | Name | Description |
|-----|------|-------------|
| 7:6 | ADCRampRate | 00=0.5dB/4 LRCK, 01=0.5dB/8 LRCK, 10=0.5dB/16 LRCK, 11=0.5dB/32 LRCK |
| 5 | ADCSoftRamp | 0=disable, 1=enable soft ramp (default) |
| 3 | ADCLer | 0=normal, 1=both channel gain set by left gain register |
| 2 | ADCMute | 0=normal, 1=mute ADC digital output |

#### 6.2.8 Register 16 - ADC Control 8, Default 1100 0000

| Bit | Name | Description |
|-----|------|-------------|
| 7:0 | LADCVOL | Left digital volume: 0dB (0x00) to -96dB (0xC0) in 0.5dB steps. Default 0xC0 = -96dB |

#### 6.2.9 Register 17 - ADC Control 9, Default 1100 0000

| Bit | Name | Description |
|-----|------|-------------|
| 7:0 | RADCVOL | Right digital volume: 0dB to -96dB in 0.5dB steps. Default 0xC0 = -96dB |

#### 6.2.10 Register 18 - ADC Control 10, Default 0011 1000

| Bit | Name | Description |
|-----|------|-------------|
| 7:6 | ALCSEL | 00=ALC off, 01=ALC right only, 10=ALC left only, 11=ALC stereo |
| 5:3 | MAXGAIN | Maximum PGA gain: 000=-6.5dB, 001=-0.5dB, 010=5.5dB, 011=11.5dB, 100=17.5dB, 101=23.5dB, 110=29.5dB, 111=35.5dB |
| 2:0 | MINGAIN | Minimum PGA gain: 000=-12dB, 001=-6dB, 010=0dB, 011=+6dB, 100=+12dB, 101=+18dB, 110=+24dB, 111=+30dB |

#### 6.2.11 Register 19 - ADC Control 11, Default 1011 0000

| Bit | Name | Description |
|-----|------|-------------|
| 7:4 | ALCLVL | ALC target: 0000=-16.5dB, 0001=-15dB, 0010=-13.5dB, ..., 0111=-6dB, 1000=-4.5dB, 1001=-3dB, 1010-1111=-1.5dB |
| 3:0 | ALCHLD | ALC hold time: 0000=0ms, 0001=2.67ms, 0010=5.33ms, ... (doubles each step), 1001=0.68s, 1010+=1.36s |

#### 6.2.12 Register 20 - ADC Control 12, Default 0011 0010

| Bit | Name | Description |
|-----|------|-------------|
| 7:4 | ALCDCY | ALC decay (gain ramp up) time: 0000=410us/90.8us, 0001=820us/182us, 0010=1.64ms/363us, ... (doubles), 1001=210ms/46.5ms, 1010+=420ms/93ms |
| 3:0 | ALCATK | ALC attack (gain ramp down) time: 0000=104us/22.7us, 0001=208us/45.4us, 0010=416us/90.8us, ..., 1001=53.2ms/11.6ms, 1010+=106ms/23.2ms |

#### 6.2.13 Register 21 - ADC Control 13, Default 0000 0110

| Bit | Name | Description |
|-----|------|-------------|
| 7 | ALCMODE | 0=ALC mode, 1=Limiter mode |
| 6 | ALCZC | 0=disable zero cross (recommended), 1=enable |
| 5 | TIME_OUT | 0=disable (default), 1=enable zero cross timeout |
| 4:0 | WIN_SIZE | Window size for peak detector: N*16 samples. Default 00110=96 samples |

#### 6.2.14 Register 22 - ADC Control 14, Default 0000 0000

| Bit | Name | Description |
|-----|------|-------------|
| 7:3 | NGTH | Noise gate threshold: 00000=-76.5dBFS to 11111=-30dBFS |
| 2:1 | NGG | Noise gate type: x0=PGA gain held constant, 01=mute ADC output, 11=reserved |
| 0 | NGAT | 0=disable, 1=enable noise gate |

### 6.3 DAC Control

#### 6.3.1 Register 23 - DAC Control 1, Default 0000 0000

| Bit | Name | Description |
|-----|------|-------------|
| 7 | DACLRSWAP | 0=normal, 1=left and right channel data swap |
| 6 | DACLRP | I2S/LJ/RJ: 0=normal polarity, 1=inverted; DSP/PCM: 0=MSB on 2nd BCLK after LRCK rise, 1=MSB on 1st BCLK after LRCK rise |
| 5 | LRCK Polarity | (Polarity control) |
| 4:2 | DACWL | 000=24-bit, 001=20-bit, 010=18-bit, 011=16-bit, 100=32-bit |
| 1:0 | DACFORMAT | 00=I2S, 01=left justified, 10=right justified, 11=DSP/PCM |

#### 6.3.2 Register 24 - DAC Control 2, Default 0000 0110

| Bit | Name | Description |
|-----|------|-------------|
| 5 | DACFsMode | 0=single speed, 1=double speed |
| 4:0 | DACFsRatio | Master mode DAC MCLK to sampling frequency ratio (see datasheet) |

#### 6.3.3 Register 25 - DAC Control 3, Default 0010 0010

| Bit | Name | Description |
|-----|------|-------------|
| 7:6 | DACRampRate | 00=0.5dB/4 LRCK, 01=0.5dB/32 LRCK, 10=0.5dB/64 LRCK, 11=0.5dB/128 LRCK |
| 5 | DACSoftRamp | 0=disable, 1=enable soft ramp (default) |
| 3 | DACLeR | 0=normal, 1=both channels set by left gain register |

#### 6.3.4 Register 26 - DAC Control 4, Default 1100 0000

| Bit | Name | Description |
|-----|------|-------------|
| 7:0 | LDACVOL | Left DAC digital volume: 0dB (0x00) to -96dB (0xC0) in 0.5dB steps |

#### 6.3.5 Register 27 - DAC Control 5, Default 1100 0000

| Bit | Name | Description |
|-----|------|-------------|
| 7:0 | RDACVOL | Right DAC digital volume: 0dB to -96dB in 0.5dB steps |

#### 6.3.6 Register 28 - DAC Control 6, Default 0000 1000

| Bit | Name | Description |
|-----|------|-------------|
| 7:6 | DeemphasisMode | 00=disabled, 01=32kHz, 10=44.1kHz, 11=48kHz (single speed only) |
| 5 | DAC_invL | 0=normal, 1=180° phase inversion left |
| 4 | DAC_invR | 0=normal, 1=180° phase inversion right |
| 3 | ClickFree | 0=disable, 1=enable click-free power up/down (default) |

#### 6.3.7 Register 29 - DAC Control 7, Default 0000 0000

| Bit | Name | Description |
|-----|------|-------------|
| 7 | ZeroL | 0=normal, 1=set left DAC output all zero |
| 6 | ZeroR | 0=normal, 1=set right DAC output all zero |
| 5 | Mono | 0=stereo, 1=mono (L+R)/2 into both DACs |
| 4:2 | SE | SE strength: 000=0 (default) to 111=7 |
| 1:0 | Vpp_scale | 00=3.5V (0.7 modulation index, default), 01=4.0V, 10=3.0V, 11=2.5V |

#### 6.3.8 - 6.3.15 Registers 30-37: Shelving Filter Coefficients

Registers 30-37 store 30-bit coefficients for shelving filter (split across registers). Default values are `{5'h0f, 5'h1f, 5'h0f, 5'h1f, 5'h0f, 5'h1f}`.

#### 6.3.16 Register 38 - DAC Control 16, Default 0000 0000

| Bit | Name | Description |
|-----|------|-------------|
| 5:3 | LMIXSEL | Left input for output mix: 000=LIN1, 001=LIN2, 010=reserved, 011=left ADC input |
| 2:0 | RMIXSEL | Right input for output mix: 000=RIN1, 001=RIN2, 010=reserved, 011=right ADC input |

#### 6.3.17 Register 39 - DAC Control 17, Default 0011 1000

| Bit | Name | Description |
|-----|------|-------------|
| 7 | LD2LO | 0=disable, 1=left DAC to left mixer enable |
| 6 | LI2LO | 0=disable, 1=LIN signal to left mixer enable |
| 5:3 | LI2LOVOL | LIN to left mixer gain: 000=6dB, 001=3dB, 010=0dB, 011=-3dB, 100=-6dB, 101=-9dB, 110=-12dB, 111=-15dB (default) |

#### 6.3.18-19 Registers 40-41 - Reserved

#### 6.3.20 Register 42 - DAC Control 20, Default 0011 1000

| Bit | Name | Description |
|-----|------|-------------|
| 7 | RD2RO | 0=disable, 1=right DAC to right mixer enable |
| 6 | RI2RO | 0=disable, 1=RIN signal to right mixer enable |
| 5:3 | RI2ROVOL | RIN to right mixer gain: same encoding as LI2LOVOL |

#### 6.3.21 Register 43 - DAC Control 21, Default 0000 0000

| Bit | Name | Description |
|-----|------|-------------|
| 7 | slrck | 0=DACLRC and ADCLRC separate, 1=same |
| 6 | lrck_sel | Master mode when slrck=1: 0=use DAC LRC, 1=use ADC LRC |
| 5 | offset_dis | 0=disable offset, 1=enable offset |
| 4 | mclk_dis | 0=normal, 1=disable MCLK input from PAD |
| 3 | adc_dll_pwd | 0=normal, 1=ADC DLL power down |
| 2 | dac_dll_pwd | 0=normal, 1=DAC DLL power down |

#### 6.3.22 Register 44 - DAC Control 22, Default 0000 0000

| Bit | Name | Description |
|-----|------|-------------|
| 7:0 | offset | DC offset |

#### 6.3.23 Register 45 - DAC Control 23, Default 0000 0000

| Bit | Name | Description |
|-----|------|-------------|
| 4 | VROI | 0=1.5k VREF to analog output resistance (default), 1=40k |

#### 6.3.24 Register 46 - DAC Control 24, Default 0000 0000

| Bit | Name | Description |
|-----|------|-------------|
| 5:0 | LOUT1VOL | LOUT1 volume: 000000=-45dB (default), 000001=-43.5dB, 000010=-42dB, ..., 011110=0dB, 011111=1.5dB, ..., 100001=4.5dB |

#### 6.3.25 Register 47 - DAC Control 25, Default 0000 0000

| Bit | Name | Description |
|-----|------|-------------|
| 5:0 | ROUT1VOL | ROUT1 volume: same encoding as LOUT1VOL |

#### 6.3.26 Register 48 - DAC Control 26, Default 0000 0000

| Bit | Name | Description |
|-----|------|-------------|
| 5:0 | LOUT2VOL | LOUT2 volume: same encoding as LOUT1VOL |

#### 6.3.27 Register 49 - DAC Control 27, Default 0000 0000

| Bit | Name | Description |
|-----|------|-------------|
| 5:0 | ROUT2VOL | ROUT2 volume: same encoding as LOUT1VOL |

#### 6.3.28 Register 50 - Reserved

#### 6.3.29 Register 51 - DAC Control 29, Default 1010 1010

| Bit | Name | Description |
|-----|------|-------------|
| 7 | hpLout1_ref1 | Reserved |
| 6 | hpLout1_ref2 | Reserved |

#### 6.3.30 Register 52 - DAC Control 30, Default 1010 1010

| Bit | Name | Description |
|-----|------|-------------|
| 7 | spkLout2_ref1 | Reserved |
| 6 | spkLout2_ref2 | Reserved |
| 3 | mixer_ref1 | Reserved |
| 2 | mixer_ref2 | Reserved |
| 1 | MREF1 | Reserved |
| 0 | MREF2 | Reserved |

## 7. DIGITAL AUDIO INTERFACE

The device provides four formats of serial audio data interface: I2S, left justified, right justified, and DSP/PCM mode.

- DAC input (DSDIN) sampled on rising edge of DSCLK
- ADC output (ASDOUT) changes on falling edge of ASCLK
- See Figures 3-7 in original datasheet for timing diagrams

## 8. ELECTRICAL CHARACTERISTICS

### 8.1 Absolute Maximum Ratings

| Parameter | Min | Max |
|-----------|-----|-----|
| Analog Supply Voltage Level | -0.3V | +5.0V |
| Digital Supply Voltage Level | -0.3V | +5.0V |

### 8.2 Recommended Operating Conditions

| Parameter | Min | Typ | Max | Unit |
|-----------|-----|-----|-----|------|
| Analog Supply Voltage Level | 1.7 | 3.3 | 3.6 | V |
| Digital Supply Voltage Level | 1.5 | 1.8 | 3.6 | V |

### 8.3 ADC Analog and Filter Characteristics

Conditions: AVDD=3.3V, DVDD=1.8V, FS=48kHz, MCLK/LRCK=256, T=25°C

| Parameter | Min | Typ | Max | Unit |
|-----------|-----|-----|-----|------|
| Dynamic Range (A-weighted) | 85 | 95 | 98 | dB |
| THD+N | -88 | -85 | -75 | dB |
| Channel Separation (1kHz) | 80 | 85 | 90 | dB |
| SNR | 85 | 95 | 98 | dB |
| Interchannel Gain Mismatch | | 0.1 | | dB |
| Gain Error | | ±5 | | % |
| Full Scale Input Level | | AVDD/3.3 | | Vrms |
| Input Impedance | | 20 | | kΩ |

**Filter Response - Single Speed**
- Passband: 0 to 0.4535 Fs
- Stopband: 0.5465 Fs
- Passband Ripple: ±0.05 dB
- Stopband Attenuation: 50 dB

**Filter Response - Double Speed**
- Passband: 0 to 0.4167 Fs
- Stopband: 0.5833 Fs
- Passband Ripple: ±0.005 dB
- Stopband Attenuation: 50 dB

### 8.4 DAC Analog and Filter Characteristics

| Parameter | Min | Typ | Max | Unit |
|-----------|-----|-----|-----|------|
| Dynamic Range (A-weighted) | 83 | 96 | 98 | dB |
| THD+N | -85 | -83 | -75 | dB |
| Channel Separation (1kHz) | 80 | 85 | 90 | dB |
| SNR | 83 | 96 | 98 | dB |
| Interchannel Gain Mismatch | | 0.05 | | dB |
| Full Scale Output Level | | AVDD/3.3 | | Vrms |

**Filter Response - Single Speed**
- Passband: 0 to 0.4535 Fs
- Stopband: 0.5465 Fs
- Passband Ripple: ±0.05 dB
- Stopband Attenuation: 40 dB

**Filter Response - Double Speed**
- Passband: 0 to 0.4167 Fs
- Stopband: 0.5833 Fs
- Passband Ripple: ±0.005 dB
- Stopband Attenuation: 40 dB

**De-emphasis Error (Single Speed Only)**
- Fs=32kHz: 0.002 dB
- Fs=44.1kHz: 0.013 dB
- Fs=48kHz: 0.0009 dB

### 8.5 Power Consumption Characteristics

| Mode | Condition | Typ | Unit |
|------|-----------|-----|------|
| Playback | DVDD=1.8V, PVDD=1.8V, AVDD=1.8V | 7 | mW |
| Playback+Record | same | 16 | mW |
| Playback | DVDD=3.3V, PVDD=3.3V, AVDD=3.3V | 31 | mW |
| Playback+Record | same | 59 | mW |
| Power Down | DVDD=1.8V | 0.3 | mW |
| Power Down | DVDD=3.3V | 1.9 | mW |

### 8.6 Serial Audio Port Switching Specifications

| Parameter | Symbol | Min | Max | Unit |
|-----------|--------|-----|-----|------|
| MCLK frequency | | | 51.2 | MHz |
| MCLK duty cycle | | 40 | 60 | % |
| LRCK frequency | | | 200 | kHz |
| LRCK duty cycle | | 40 | 60 | % |
| SCLK frequency | | | 26 | MHz |
| SCLK pulse width low | TSCLKL | 15 | | ns |
| SCLK pulse width high | TSCLKH | 15 | | ns |

### 8.7 Serial Control Port Switching Specifications

**SPI Mode**

| Parameter | Symbol | Min | Max | Unit |
|-----------|--------|-----|-----|------|
| SPI_CLK frequency | | | 10 | MHz |
| SPI_CLK edge to CSn falling | TSPICS | 5 | | ns |
| CSn high time between transmissions | TSPISH | 500 | | ns |
| CSn falling to SPI_CLK edge | TSPISC | 10 | | ns |
| SPI_CLK low time | TSPICL | 45 | | ns |
| SPI_CLK high time | TSPICH | 45 | | ns |
| SPI_DIN to CLK rising setup | TSPIDS | 10 | | ns |
| CLK rising to DATA hold | TSPIDH | 15 | | ns |

**2-wire Mode (I2C)**

| Parameter | Symbol | Min | Max | Unit |
|-----------|--------|-----|-----|------|
| SCL clock frequency | FSCL | | 400 | kHz |
| Bus free time between transmissions | TWTID | 1.3 | | us |
| Start condition hold time | TWTSTH | 0.6 | | us |
| Clock low time | TWTCL | 1.3 | | us |
| Clock high time | TWTCH | 0.4 | | us |
| Setup time for repeated start | TWTSTS | 0.6 | | us |
| SDA hold time from SCL falling | TWTDH | | 900 | ns |
| SDA setup time to SCL rising | TWTDS | 100 | | ns |
| Rise time of SCL | TTRR | | 300 | ns |
| Fall time of SCL | TTRF | | 300 | ns |



## 10. CORPORATION INFORMATION

Everest Semiconductor Co., Ltd.
苏州工业园区机场路328号，国际科技园区科技广场6A，邮编215028
Email: info@everest-semi.com