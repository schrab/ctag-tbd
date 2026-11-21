#pragma once
#include "../../shim/freertos/FreeRTOS.h"
#include <mutex>
#undef portTICK_PERIOD_MS
#define portTICK_PERIOD_MS 10
#define pdMS_TO_TICKS(ms) ((ms) / portTICK_PERIOD_MS)
using portMUX_TYPE = std::recursive_mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
#define taskENTER_CRITICAL(mux) (mux)->lock()
#define taskEXIT_CRITICAL(mux) (mux)->unlock()
