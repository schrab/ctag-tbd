#pragma once
#include "freertos/FreeRTOS.h"
#include <condition_variable>
#include <memory>
#include <vector>

struct RelaySemaphore {
    std::mutex mutex;
    std::condition_variable ready;
    bool available = false;
};
using SemaphoreHandle_t = RelaySemaphore *;
inline SemaphoreHandle_t RelaySemaphoreCreate(bool available) {
    static std::vector<std::unique_ptr<RelaySemaphore>> semaphores;
    semaphores.push_back(std::make_unique<RelaySemaphore>());
    semaphores.back()->available = available;
    return semaphores.back().get();
}
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return RelaySemaphoreCreate(true); }
inline SemaphoreHandle_t xSemaphoreCreateBinary() { return RelaySemaphoreCreate(false); }
inline int xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks) {
    std::unique_lock<std::mutex> lock(sem->mutex);
    if (!sem->ready.wait_for(lock, std::chrono::milliseconds(ticks * portTICK_PERIOD_MS),
                             [&] { return sem->available; })) return pdFALSE;
    sem->available = false;
    return pdTRUE;
}
inline int xSemaphoreGive(SemaphoreHandle_t sem) {
    std::lock_guard<std::mutex> lock(sem->mutex);
    sem->available = true;
    sem->ready.notify_one();
    return pdTRUE;
}
