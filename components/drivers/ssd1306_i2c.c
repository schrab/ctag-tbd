/*
MIT License

Copyright (c) 2020 nopnop2002

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
 */

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "ssd1306.h"

#define tag "SSD1309"
#define CONFIG_OFFSETX 0

static i2c_master_bus_handle_t bus_handle = NULL;
static i2c_master_dev_handle_t dev_handle = NULL;

void i2c_master_init(SSD1306_t * dev, int16_t sda, int16_t scl, int16_t reset)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = -1,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 1,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = I2CAddress,
        .scl_speed_hz = 1000000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_config, &dev_handle));

    if (reset >= 0) {
        gpio_config_t io_conf = {
            .pin_bit_mask = 1ULL << reset,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
        gpio_set_level(reset, 0);
        vTaskDelay(50 / portTICK_PERIOD_MS);
        gpio_set_level(reset, 1);
    }
    dev->_address = I2CAddress;
    dev->_flip = false;
}

static esp_err_t i2c_write_cmd(uint8_t *cmd, size_t len)
{
    uint8_t buf[128];
    if (len + 1 > sizeof(buf)) return ESP_ERR_INVALID_SIZE;
    buf[0] = OLED_CONTROL_BYTE_CMD_STREAM;
    memcpy(buf + 1, cmd, len);
    return i2c_master_transmit(dev_handle, buf, len + 1, 100);
}

static esp_err_t i2c_write_data(uint8_t *data, size_t len)
{
    uint8_t buf[256];
    if (len + 1 > sizeof(buf)) return ESP_ERR_INVALID_SIZE;
    buf[0] = OLED_CONTROL_BYTE_DATA_STREAM;
    memcpy(buf + 1, data, len);
    return i2c_master_transmit(dev_handle, buf, len + 1, 100);
}

void i2c_init(SSD1306_t * dev, int width, int height) {
    dev->_width = width;
    dev->_height = height;
    dev->_pages = 8;
    if (dev->_height == 32) dev->_pages = 4;

    uint8_t init_seq[] = {
        OLED_CMD_DISPLAY_OFF,
        OLED_CMD_SET_MUX_RATIO,
        (uint8_t)(dev->_height == 64 ? 0x3F : 0x1F),
        OLED_CMD_SET_DISPLAY_OFFSET,
        0x00,
        (uint8_t)(dev->_flip ? OLED_CMD_SET_SEGMENT_REMAP_0 : OLED_CMD_SET_SEGMENT_REMAP_1),
        OLED_CMD_SET_COM_SCAN_MODE,
        OLED_CMD_SET_DISPLAY_CLK_DIV,
        0x80,
        OLED_CMD_SET_COM_PIN_MAP,
        (uint8_t)(dev->_height == 64 ? 0x12 : 0x02),
        OLED_CMD_SET_CONTRAST,
        0xFF,
        OLED_CMD_DISPLAY_RAM,
        OLED_CMD_SET_VCOMH_DESELCT,
        0x40,
        OLED_CMD_SET_MEMORY_ADDR_MODE,
        OLED_CMD_SET_PAGE_ADDR_MODE,
        0x00,
        0x10,
        OLED_CMD_SET_CHARGE_PUMP,
        0x14,
        OLED_CMD_DEACTIVE_SCROLL,
        OLED_CMD_DISPLAY_NORMAL,
        OLED_CMD_DISPLAY_ON
    };

    esp_err_t espRc = i2c_write_cmd(init_seq, sizeof(init_seq));
    if (espRc == ESP_OK) {
        ESP_LOGI(tag, "OLED configured successfully");
    } else {
        ESP_LOGE(tag, "OLED configuration failed. code: 0x%.2X", espRc);
    }
}

void i2c_display_image(SSD1306_t * dev, int page, int seg, uint8_t * images, int width) {
    if (page >= dev->_pages) return;
    if (seg >= dev->_width) return;

    int _seg = seg + CONFIG_OFFSETX;
    int _page = page;
    if (dev->_flip) {
        _page = (dev->_pages - page) - 1;
    }

    uint8_t addr_cmd[] = {
        (uint8_t)(0x00 + (_seg & 0x0F)),
        (uint8_t)(0x10 + ((_seg >> 4) & 0x0F)),
        (uint8_t)(0xB0 | _page)
    };
    i2c_write_cmd(addr_cmd, sizeof(addr_cmd));
    i2c_write_data(images, width);
}

void i2c_contrast(SSD1306_t * dev, int contrast) {
    int _contrast = contrast;
    if (contrast < 0x0) _contrast = 0;
    if (contrast > 0xFF) _contrast = 0xFF;
    uint8_t cmd[] = { OLED_CMD_SET_CONTRAST, (uint8_t)_contrast };
    i2c_write_cmd(cmd, sizeof(cmd));
}

void i2c_display_on(SSD1306_t * dev) {
    uint8_t cmd[] = { OLED_CMD_DISPLAY_ON };
    i2c_write_cmd(cmd, sizeof(cmd));
}

void i2c_display_off(SSD1306_t * dev) {
    uint8_t cmd[] = { OLED_CMD_DISPLAY_OFF };
    i2c_write_cmd(cmd, sizeof(cmd));
}

void i2c_hardware_scroll(SSD1306_t * dev, ssd1306_scroll_type_t scroll) {
    uint8_t cmd_buf[16];
    int len = 0;

    if (scroll == SCROLL_RIGHT) {
        uint8_t s[] = {
            OLED_CMD_HORIZONTAL_RIGHT, 0x00, 0x00, 0x07, 0x07, 0x00, 0xFF,
            OLED_CMD_ACTIVE_SCROLL
        };
        memcpy(cmd_buf, s, sizeof(s));
        len = sizeof(s);
    } else if (scroll == SCROLL_LEFT) {
        uint8_t s[] = {
            OLED_CMD_HORIZONTAL_LEFT, 0x00, 0x00, 0x07, 0x07, 0x00, 0xFF,
            OLED_CMD_ACTIVE_SCROLL
        };
        memcpy(cmd_buf, s, sizeof(s));
        len = sizeof(s);
    } else if (scroll == SCROLL_DOWN) {
        uint8_t s[] = {
            OLED_CMD_CONTINUOUS_SCROLL, 0x00, 0x00, 0x07,
            0x00, 0x3F,
            OLED_CMD_VERTICAL, 0x00,
            (uint8_t)(dev->_height == 64 ? 0x40 : 0x20),
            OLED_CMD_ACTIVE_SCROLL
        };
        memcpy(cmd_buf, s, sizeof(s));
        len = sizeof(s);
    } else if (scroll == SCROLL_UP) {
        uint8_t s[] = {
            OLED_CMD_CONTINUOUS_SCROLL, 0x00, 0x00, 0x07,
            0x00, 0x01,
            OLED_CMD_VERTICAL, 0x00,
            (uint8_t)(dev->_height == 64 ? 0x40 : 0x20),
            OLED_CMD_ACTIVE_SCROLL
        };
        memcpy(cmd_buf, s, sizeof(s));
        len = sizeof(s);
    } else if (scroll == SCROLL_STOP) {
        cmd_buf[0] = OLED_CMD_DEACTIVE_SCROLL;
        len = 1;
    }

    esp_err_t espRc = i2c_write_cmd(cmd_buf, len);
    if (espRc == ESP_OK) {
        ESP_LOGD(tag, "Scroll command succeeded");
    } else {
        ESP_LOGE(tag, "Scroll command failed. code: 0x%.2X", espRc);
    }
}
