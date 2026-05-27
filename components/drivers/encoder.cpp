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

#include "encoder.hpp"
#include "driver/pcnt.h"
#include "driver/gpio.h"
#include "soc/pcnt_struct.h"

#define ENC_A_GPIO 5
#define ENC_B_GPIO 23
#define PCNT_UNIT PCNT_UNIT_0

namespace CTAG {
    namespace DRIVERS {
        encoder::encoder() {
            pcnt_config_t cfg = {};
            cfg.pulse_gpio_num = ENC_A_GPIO;
            cfg.ctrl_gpio_num = ENC_B_GPIO;
            cfg.channel = PCNT_CHANNEL_0;
            cfg.unit = PCNT_UNIT;
            cfg.pos_mode = PCNT_COUNT_INC;
            cfg.neg_mode = PCNT_COUNT_DEC;
            cfg.lctrl_mode = PCNT_MODE_KEEP;
            cfg.hctrl_mode = PCNT_MODE_REVERSE;
            cfg.counter_h_lim = INT16_MAX;
            cfg.counter_l_lim = INT16_MIN;
            pcnt_unit_config(&cfg);
            pcnt_counter_pause(PCNT_UNIT);
            pcnt_counter_clear(PCNT_UNIT);
            pcnt_counter_resume(PCNT_UNIT);
        }

        encoder::~encoder() {
            pcnt_counter_pause(PCNT_UNIT);
            pcnt_unit_config(nullptr);
        }

        int encoder::ReadDelta() {
            int16_t count;
            pcnt_get_counter_value(PCNT_UNIT, &count);
            if (count == 0) return 0;
            pcnt_counter_clear(PCNT_UNIT);
            // sensitivity threshold: ignore ±1 glitches
            if (count > 1) return count / 2;
            if (count < -1) return count / 2;
            return count;
        }
    }
}
