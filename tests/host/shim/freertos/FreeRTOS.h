/*
 * Host shim for FreeRTOS types used by the modulation helpers.
 *
 * ctagTempo.hpp includes "freertos/FreeRTOS.h" and uses TickType_t and
 * portTICK_PERIOD_MS. On the host there is no scheduler, so the tick counter
 * is a plain variable the tests drive explicitly via TestHostSetTick().
 * Keeping the shim this thin lets the helpers under test compile and run
 * unmodified -- the tests exercise production code, not a copy.
 */
#pragma once

#include <cstdint>

// Matches the firmware default (configTICK_RATE_HZ = 1000), so one tick = 1ms
// and the tap-tempo thresholds in ctagTempo mean what they mean on device.
typedef uint32_t TickType_t;

#define portTICK_PERIOD_MS 1

inline TickType_t &TestHostTickRef() {
    static TickType_t tick = 0;
    return tick;
}

// Advance the fake tick counter. Call this instead of sleeping.
inline void TestHostSetTick(TickType_t tick) { TestHostTickRef() = tick; }
inline void TestHostAdvanceTick(TickType_t delta) { TestHostTickRef() += delta; }