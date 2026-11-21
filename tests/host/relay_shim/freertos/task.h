#pragma once
#include "freertos/FreeRTOS.h"
#include <thread>
#include <chrono>
inline TickType_t xTaskGetTickCount() { return TestHostTickRef(); }
inline void vTaskDelay(TickType_t ticks) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ticks * portTICK_PERIOD_MS));
}
inline int xTaskCreate(void (*)(void *), const char *, int, void *, int, TaskHandle_t *task) {
    *task = reinterpret_cast<void *>(1);
    return pdPASS;
}
