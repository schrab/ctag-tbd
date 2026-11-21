#pragma once
#include "../../panel_shim/freertos/FreeRTOS.h"
using TaskHandle_t = void *;
using BaseType_t = int;
constexpr int pdTRUE = 1, pdFALSE = 0, pdPASS = 1, tskIDLE_PRIORITY = 0;
constexpr TickType_t portMAX_DELAY = UINT32_MAX;
