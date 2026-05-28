### 1 GENERAL DESCRIPTION

The **SSD1309** is a single-chip **CMOS OLED/PLED driver** with a controller designed for organic/polymer light-emitting diode dot-matrix graphic display systems. It supports a resolution of **128 segments and 64 commons** and is specifically designed for **Common Cathode** type OLED panels.

To reduce external component count and power consumption, the SSD1309 embeds the following features:
*   **Contrast Control:** Includes a 256-step brightness control.
*   **Display RAM:** Internal GDDRAM for storing display patterns.
*   **Oscillator:** An on-chip oscillator for internal timing.

Data and commands are communicated from a general MCU through various hardware-selectable interfaces, including **6800/8080 series compatible parallel interfaces**, the **I2C interface**, or the **Serial Peripheral Interface (SPI)**. Due to its integrated nature, it is suitable for compact portable applications such as mobile phone sub-displays, MP3 players, and calculators.

### 2 FEATURES
*   **Resolution:** 128 x 64 dot matrix panel.
*   **Power supply:**
    *   **VDD:** 1.65V ~ 3.3V for IC logic.
    *   **VCC:** 7.0V ~ 16.0V for Panel driving.
*   **For matrix display:**
    *   OLED driving output voltage: 16V maximum.
    *   Segment maximum source current: 320uA.
    *   Common maximum sink current: 40mA.
    *   256 step contrast brightness current control.
*   **Memory:** Embedded 128 x 64 bit SRAM display buffer.
*   **Pin selectable MCU Interfaces:**
    *   8-bit 6800/8080-series parallel interface.
    *   3/4 wire Serial Peripheral Interface.
    *   I2C Interface.
*   **Functionality:**
    *   Screen saving infinite content scrolling function.
    *   Programmable Frame Rate.
    *   Programmable Multiplexing Ratio.
    *   Row Re-mapping and Column Re-mapping.
    *   On-Chip Oscillator.
*   **Physical/Environmental:**
    *   Chip layout for COG, COF.
    *   Wide range of operating temperature: -40°C to 85°C.


# 8 FUNCTIONAL BLOCK DESCRIPTIONS

### 8.1 MCU Interface selection
The SSD1309 MCU interface consists of 8 data pins and 5 control pins. The pin assignment at different interface modes is determined by hardware selection on the **BS[2:0]** pins.

**Table 8-1: MCU interface assignment under different bus interface mode**
| Bus Interface | D7 | D6 | D5 | D4 | D3 | D2 | D1 | D0 | E | R/W# | CS# | D/C# | RES# |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **8-bit 8080** | D7 | D6 | D5 | D4 | D3 | D2 | D1 | D0 | RD# | WR# | CS# | D/C# | RES# |
| **8-bit 6800** | D7 | D6 | D5 | D4 | D3 | D2 | D1 | D0 | E | R/W# | CS# | D/C# | RES# |
| **3-wire SPI** | L | L | L | L | L | NC | SDIN | SCLK | L | L | CS# | L | RES# |
| **4-wire SPI** | L | L | L | L | L | NC | SDIN | SCLK | L | L | CS# | D/C# | RES# |
| **I2C** | L | L | L | L | L | SDAout| SDAin | SCL | L | L | SA0 | SA0 | RES# |

#### 8.1.1 MCU Parallel 6800-series Interface
The parallel interface consists of 8 bi-directional data pins (D[7:0]), R/W#, D/C#, E, and CS#. A LOW in R/W# indicates a WRITE operation; a HIGH indicates a READ operation. A LOW in D/C# indicates a COMMAND read/write; a HIGH indicates a DATA read/write. The E input serves as the data latch signal while CS# is LOW, with data latched at the falling edge of E. Pipeline processing requires a **dummy read** before the first actual display data read.

#### 8.1.2 MCU Parallel 8080-series Interface
The parallel interface consists of 8 bi-directional data pins (D[7:0]), RD#, WR#, D/C#, and CS#. A LOW in D/C# indicates a COMMAND read/write; a HIGH indicates a DATA read/write. A rising edge of RD# serves as a data READ latch signal while CS# is LOW. A rising edge of WR# serves as a data/command WRITE latch signal while CS# is LOW. A **dummy read** is required before the first actual display data read.

#### 8.1.3 MCU Serial Interface (4-wire SPI)
The 4-wire serial interface consists of SCLK (D0), SDIN (D1), D/C#, and CS#. Unused data pins D3-D7, E, and R/W# should be connected to ground. SDIN is shifted into an 8-bit shift register on every rising edge of SCLK in the order D7, D6, ... D0. D/C# is sampled on every eighth clock to determine if the byte is GDDRAM data or a command. Only write operations are allowed.

#### 8.1.4 MCU Serial Interface (3-wire SPI)
The 3-wire serial interface consists of SCLK (D0), SDIN (D1), and CS#. It functions similarly to 4-wire SPI, but the D/C# pin is not used. Instead, 9 bits are shifted in: the **D/C# bit (first bit)** followed by D7 to D0. The D/C# bit determines if the following byte is written to the GDDRAM (1) or the command register (0). Only write operations are allowed.

#### 8.1.5 MCU I2C Interface
The I2C interface uses slave address bit SA0, data signal SDA (D2/D1), and clock signal SCL (D0).
*   **Slave Address (SA0):** The device responds to the address `011110` + `SA0` + `R/W#`. D/C# acts as SA0, allowing a slave address of **0x3C** (SA0=LOW) or **0x3D** (SA0=HIGH).
*   **Write Mode:** The master initiates data communication with a Start condition. After the slave address and R/W# bit (set to 0), an acknowledgement signal (ACK) is generated. 
*   **Control Byte:** Following the slave address, the master sends a control byte consisting of **Co** (Continuation bit), **D/C#**, and six "0"s.
    *   If **Co=0**, the following transmission contains data bytes only.
    *   If **D/C#=0**, the next data byte is a command; if **D/C#=1**, it is data stored in GDDRAM.
*   The GDDRAM column address pointer increases by one automatically after each data write.

### 8.2 Command Decoder
This module determines if input data is interpreted as a command or display data based on the D/C# pin. If D/C# is HIGH, data is written to GDDRAM; if LOW, it is decoded and written to the corresponding command register.

### 8.3 Oscillator Circuit and Display Time Generator
This on-chip RC oscillator generates the operation clock (CLK). The CLS pin selects between the internal oscillator (CLS pulled HIGH) and an external clock on the CL pin (CLS pulled LOW). When internal, the frequency **Fosc** can be changed via command `D5h` A[7:4]. The display clock (DCLK) is derived from CLK using a programmable division factor "D" (1 to 16).

### 8.4 Reset Circuit
When **RES#** is LOW, the chip initializes to the following status:
1. Display is OFF.
2. 128 x 64 Display Mode.
3. Normal segment and display data mapping (SEG0 to 00h, COM0 to 00h).
4. Shift register data clear (serial interface).
5. Display start line set at RAM address 0.
6. Column address counter set at 0.
7. Normal scan direction of COM outputs.
8. Contrast control register set at 7Fh.
9. Normal display mode (Equivalent to `A4h` command).

### 8.5 Segment Drivers / Common Drivers
Segment drivers provide 128 current sources (0 to 320µA in 256 steps). The driving waveform has three phases:
*   **Phase 1:** Discharge pixels to prepare for the next image.
*   **Phase 2:** Drive pixels to targeted voltage; period is programmable from 1 to 15 DCLKs.
*   **Phase 3:** Switch to current source drive stage.

### 8.6 Graphic Display Data RAM (GDDRAM)
The GDDRAM is a bit-mapped static RAM of 128 x 64 bits, divided into **eight pages** (PAGE0 to PAGE7). Writing one data byte fills all rows of the same page in the current column (8 bits). Data bit **D0 is the top row** and **D7 is the bottom row**. Re-mapping of Segment and Common outputs is software-selectable.

### 8.8 Power ON and OFF sequence
*   **Power ON:** 1. VDD ON $\rightarrow$ 2. Reset (RES# LOW $\ge$ 3µs) $\rightarrow$ 3. VCC ON $\rightarrow$ 4. Send `AFh` (Display ON).
*   **Power OFF:** 1. Send `AEh` (Display OFF) $\rightarrow$ 2. VCC OFF $\rightarrow$ 3. VDD OFF.

---

# 9 COMMAND TABLE

### 9.1 Fundamental Command Table
| D/C# | Hex | Command | Description |
| :---: | :---: | :--- | :--- |
| 0 | **81h** | **Set Contrast** | Double byte command (00h-FFh). Contrast increases with value. (RESET = 7Fh). |
| 0 | **A4h/A5h**| **Entire Display ON**| `A4h`: Output follows RAM content; `A5h`: Output ignores RAM (all pixels ON). |
| 0 | **A6h/A7h**| **Normal/Inverse** | `A6h`: Normal (1=ON); `A7h`: Inverse (0=ON). |
| 0 | **AEh/AFh**| **Display ON/OFF** | `AEh`: Sleep mode (OFF); `AFh`: Normal mode (ON). |
| 0 | **FDh** | **Command Lock** | `FDh 16h`: Lock MCU interface; `FDh 12h`: Unlock (RESET). |

### 9.2 Scrolling Command Table
| D/C# | Hex | Command | Description |
| :---: | :---: | :--- | :--- |
| 0 | **26h/27h**| **H-Scroll Setup** | 7-byte command to set Right (`26h`) or Left (`27h`) scroll, pages, interval, and columns. |
| 0 | **29h/2Ah**| **V & H Scroll** | 7-byte command for continuous vertical and diagonal scrolling. |
| 0 | **2Eh** | **Deactivate Scroll**| Stop scrolling. **Note:** RAM data must be rewritten after this. |
| 0 | **2Fh** | **Activate Scroll** | Start scrolling using defined parameters. |
| 0 | **A3h** | **Set V-Scroll Area**| 3-byte command to define fixed and scrolling rows. |
| 0 | **2Ch/2Dh**| **Content Scroll** | Scroll and update RAM contents by one column. Requires delay of `FrameFreq * 2` between consecutive sends. |

### 9.3 Addressing Setting Command Table
| D/C# | Hex | Command | Description |
| :---: | :---: | :--- | :--- |
| 0 | **00h~0Fh** | **Lower Column** | Set lower nibble of column start address (Page Mode only). |
| 0 | **10h~1Fh** | **Higher Column** | Set higher nibble of column start address (Page Mode only). |
| 0 | **20h** | **Memory Mode** | `00b`: Horizontal; `01b`: Vertical; `10b`: Page (RESET). |
| 0 | **21h** | **Set Column Addr**| Triple byte: Start (0-127) and End (0-127) column. |
| 0 | **22h** | **Set Page Addr** | Triple byte: Start (0-7) and End (0-7) page. |
| 0 | **B0h~B7h** | **Page Start Addr** | Set GDDRAM Page Start Address (0-7) for Page Addressing Mode. |

### 9.4 Hardware Configuration Command Table
| D/C# | Hex | Command | Description |
| :---: | :---: | :--- | :--- |
| 0 | **40h~7Fh** | **Start Line** | Set display RAM start line (0-63). |
| 0 | **A0h/A1h** | **Segment Re-map** | `A0h`: Col 0 is SEG0; `A1h`: Col 127 is SEG0. |
| 0 | **A8h** | **MUX Ratio** | Set MUX ratio (16 to 64). RESET = 64MUX. |
| 0 | **C0h/C8h** | **COM Scan Dir** | `C0h`: Normal (COM0 to N-1); `C8h`: Remapped (COM N-1 to 0). |
| 0 | **D3h** | **Display Offset** | Set vertical shift by COM (0-63). |
| 0 | **DAh** | **COM Pin Config** | `A=0`: Sequential; `A=1`: Alternative (RESET). `A`: COM L/R remap. |
| 0 | **DCh** | **Set GPIO** | Set GPIO state (HiZ, Input, Output LOW/HIGH). |

### 9.5 Timing & Driving Scheme Setting Command Table
| D/C# | Hex | Command | Description |
| :---: | :---: | :--- | :--- |
| 0 | **D5h** | **Clock Divide/Osc**| `A[3:0]`: Divide ratio (D=A+1); `A[7:4]`: Oscillator frequency. |
| 0 | **D9h** | **Pre-charge** | `A[3:0]`: Phase 1; `A[7:4]`: Phase 2 period in DCLKs. |
| 0 | **DBh** | **VCOMH Level** | Set deselect level (~0.64, 0.78, or 0.84 x VCC). |

---

# 10 COMMAND DESCRIPTIONS

### 10.3 Set Memory Addressing Mode (20h)
*   **Page Addressing Mode (10b):** After access, the column pointer increments automatically. At the end of the column, it resets to the start of the same page. Page address must be set manually via `B0h-B7h`.
*   **Horizontal Addressing Mode (00b):** Column pointer increments. At the end of the column, it resets to the start column and the page address increments. Pointers reset to (0,0) after the last page.
*   **Vertical Addressing Mode (01b):** Page pointer increments. At the end of the page, it resets to the start page and the column address increments.

### 10.4 Set Column Address (21h)
Triple-byte command to specify column start and end addresses for GDDRAM. It also sets the current column address pointer to the start address.

### 10.5 Set Page Address (22h)
Triple-byte command to specify page start and end addresses. It also sets the current page address pointer to the start address.

### 10.14 Set COM Output Scan Direction (C0h/C8h)
Immediately flips the display vertically when issued. `C0h` is normal (COM0 to N-1); `C8h` is remapped.

### 10.18 Set COM Pins Hardware Configuration (DAh)
*   **Sequential (A=0):** COM outputs are in sequence.
*   **Alternative (A=1):** COM outputs are interleaved (Odd/Even).

### 10.22 Set Command Lock (FDh)
*   `FDh 16h`: Locks the MCU interface; ignores all commands except unlock.
*   `FDh 12h`: Unlocks the interface for normal operation.

### 10.28 Content Scroll Setup (2Ch/2Dh)
RAM contents are scrolled and updated by one column horizontally. Sending consecutive commands requires a delay of `1 / FrameFrequency`. In Page Addressing mode, updating the updated column's data allows for infinite content scrolling.


### Table 13-6: I2C Interface Timing Characteristics
**Conditions:** $V_{DD} - V_{SS} = 1.65V \text{ to } 3.3V, T_A = 25^\circ C$

| Symbol | Parameter | Min | Typ | Max | Unit |
| :--- | :--- | :---: | :---: | :---: | :---: |
| $t_{cycle}$ | Clock Cycle Time | 2.5 | - | - | us |
| $t_{HSTART}$ | Start condition Hold Time | 0.6 | - | - | us |
| $t_{HD}$ | Data Hold Time (for “SDAOUT” pin) | 0 | - | - | ns |
| $t_{HD}$ | Data Hold Time (for “SDAIN” pin) | 300 | - | - | ns |
| $t_{SD}$ | Data Setup Time | 100 | - | - | ns |
| $t_{SSTART}$ | Start condition Setup Time (Only relevant for a repeated Start condition) | 0.6 | - | - | us |
| $t_{SSTOP}$ | Stop condition Setup Time | 0.6 | - | - | us |
| $t_{R}$ | Rise Time for data and clock pin | - | - | 300 | ns |
| $t_{F}$ | Fall Time for data and clock pin | - | - | 300 | ns |
| $t_{IDLE}$ | Idle Time before a new transmission can start | 1.3 | - | - | us |

