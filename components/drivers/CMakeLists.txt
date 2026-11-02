if(CONFIG_TBD_PLATFORM_MK2)
    idf_component_register(SRCS fs.cpp led_rgb.cpp codec.cpp mk2.cpp ssd1306.c ssd1306_i2c.c
            INCLUDE_DIRS "."
            REQUIRES driver esp_driver_i2c
            PRIV_REQUIRES fatfs)
elseif (CONFIG_TBD_PLATFORM_BBA)
    if(CONFIG_TBD_BBA_CODEC_ES8388)
        set(codec_src es8388.cpp)
    else()
        set(codec_src aic3254.cpp)
    endif()

    # P6 cutover: no OLED (panel owns the display), no local encoder —
    # GPIO23 is the panel-link UART RX.
    idf_component_register(SRCS fs.cpp led_rgb_bba.cpp codec_bba.cpp ${codec_src} midiuart.cpp
            INCLUDE_DIRS "."
            REQUIRES driver esp-dsp esp_driver_i2c esp_driver_i2s esp_driver_gpio esp_driver_uart esp_driver_sdmmc esp_timer littlefs
            PRIV_REQUIRES fatfs sdmmc)
elseif (CONFIG_TBD_PLATFORM_BBA2)
    # BBA v2: ESP32-S3 + WM8978 (I2C control), SDMMC without card-detect,
    # no amp-enable pin, panel link on UART1 GPIO17/18. No OLED/encoder
    # (panel owns them), same as BBA.
    idf_component_register(SRCS fs.cpp led_rgb_bba.cpp codec_bba2.cpp wm8978.cpp midiuart.cpp
            INCLUDE_DIRS "."
            REQUIRES driver esp-dsp esp_driver_i2c esp_driver_i2s esp_driver_gpio esp_driver_uart esp_driver_sdmmc esp_timer littlefs
            PRIV_REQUIRES fatfs sdmmc)
else()
    idf_component_register(SRCS gpio.cpp adc.cpp fs.cpp led_rgb.cpp codec.cpp ssd1306.c ssd1306_i2c.c
            INCLUDE_DIRS "."
            REQUIRES ulp driver esp_driver_i2c
            PRIV_REQUIRES fatfs)

    set(ulp_app_name ulp_${COMPONENT_NAME})

    if(CONFIG_TBD_PLATFORM_STR)
        set(ulp_s_sources ulp/mcp3208.S)
    else()
        set(ulp_s_sources ulp/adc.S)
    endif()

    set(ulp_exp_dep_srcs "adc.cpp")

    ulp_embed_binary(${ulp_app_name} ${ulp_s_sources} ${ulp_exp_dep_srcs})
endif()


