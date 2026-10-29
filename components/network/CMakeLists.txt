if(CONFIG_TBD_PLATFORM_BBA)
    idf_component_register(INCLUDE_DIRS .)
else()
    idf_component_register(SRCS network.cpp
            INCLUDE_DIRS .
            PRIV_REQUIRES nvs_flash mdns esp_netif esp_wifi)
endif()

