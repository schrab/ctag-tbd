/*
 * Host shim for the single FreeRTOS API ctagTempo calls: xTaskGetTickCount.
 *
 * Returns the fake tick counter from FreeRTOS.h so tap-tempo tests are
 * deterministic and do not depend on wall-clock time.
 */
#pragma once

#include "freertos/FreeRTOS.h"

inline TickType_t xTaskGetTickCount(void) { return TestHostTickRef(); }