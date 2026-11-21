#include "test_framework.hpp"

#include "ModTransport.hpp"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <thread>

TEST(transport_cross_task_stress_conserves_ticks_and_publishes_state) {
    using CTAG::DRIVERS::ModTransport;

    ModTransport::Init();
    constexpr int kClockEdges = 120000;
    std::atomic<bool> callbacksDone{false};
    std::atomic<uint64_t> clockCallbacks{0};
    std::atomic<int> invalidUiSnapshot{0};
    std::atomic<int> observedStartReset{0};

    std::thread midiTask([&] {
        for (int i = 0; i < kClockEdges; ++i) {
            ModTransport::OnMidiClockAt(1000000LL + static_cast<int64_t>(i) * 20833LL);
            clockCallbacks.fetch_add(1, std::memory_order_relaxed);
            if ((i % 4096) == 0) ModTransport::OnMidiStart();
            else if ((i % 4096) == 2048) ModTransport::OnMidiStop();
        }
        // Leave a deterministic final running state while exercising a Start
        // racing the audio block's pending-reset exchange.
        ModTransport::OnMidiStop();
        ModTransport::OnMidiStart();
        callbacksDone.store(true, std::memory_order_release);
    });

    std::thread uiTask([&] {
        for (int i = 0; i < kClockEdges; ++i) {
            const float requestedBpm = static_cast<float>(20 + (i % 461));
            ModTransport::SetBPM(requestedBpm);
            const float bpm = ModTransport::GetBPM();
            const float measured = ModTransport::GetMeasuredBPM();
            if (!std::isfinite(bpm) || bpm < 20.0f || bpm > 480.0f ||
                !std::isfinite(measured) ||
                (measured != -1.0f && (measured < 2.5f || measured > 1000.0f))) {
                invalidUiSnapshot.fetch_add(1, std::memory_order_relaxed);
            }
            ModTransport::SetGlobalClockMidi((i & 1) != 0);
            (void)ModTransport::GetGlobalClockMidi();
        }
    });

    // Model independent consumers observing the block-published fields while
    // the audio task and MIDI callback task are active.
    std::thread stateObserver([&] {
        while (!callbacksDone.load(std::memory_order_acquire)) {
            const int ticks = ModTransport::TicksThisBlock();
            const float bpm = ModTransport::GetBPM();
            const float measured = ModTransport::GetMeasuredBPM();
            if (ticks < 0 || !std::isfinite(bpm) || bpm < 20.0f || bpm > 480.0f ||
                !std::isfinite(measured) ||
                (measured != -1.0f && (measured < 2.5f || measured > 1000.0f))) {
                invalidUiSnapshot.fetch_add(1, std::memory_order_relaxed);
            }
            (void)ModTransport::IsRunning();
            (void)ModTransport::TakeStartReset();
            (void)ModTransport::ExternalGateFlags();
            (void)ModTransport::GetGlobalClockMidi();
        }
    });

    uint64_t drainedClockEdges = 0;
    while (!callbacksDone.load(std::memory_order_acquire)) {
        ModTransport::Process(1.0f / 1000.0f);
        const int ticks = ModTransport::TicksThisBlock();
        if (ticks < 0) invalidUiSnapshot.fetch_add(1, std::memory_order_relaxed);
        else drainedClockEdges += static_cast<uint64_t>(ticks);
        if (ModTransport::TakeStartReset())
            observedStartReset.fetch_add(1, std::memory_order_relaxed);
    }

    midiTask.join();
    uiTask.join();
    stateObserver.join();

    // The last callback may have raced the last exchange; one more audio block
    // must drain every remaining edge exactly once.
    ModTransport::Process(1.0f / 1000.0f);
    const int finalTicks = ModTransport::TicksThisBlock();
    if (finalTicks >= 0) drainedClockEdges += static_cast<uint64_t>(finalTicks);
    const bool resetInFinalBlock = ModTransport::TakeStartReset();
    const bool sameResetInFinalBlock = ModTransport::TakeStartReset();

    CHECK_EQ(drainedClockEdges, clockCallbacks.load(std::memory_order_relaxed));
    CHECK_EQ(invalidUiSnapshot.load(std::memory_order_relaxed), 0);
    CHECK(ModTransport::IsRunning());
    CHECK(resetInFinalBlock == sameResetInFinalBlock);
    CHECK(observedStartReset.load(std::memory_order_relaxed) > 0 || resetInFinalBlock);

    ModTransport::OnMidiStop();
    CHECK(!ModTransport::IsRunning());
    ModTransport::OnMidiStart();
    ModTransport::Process(1.0f / 1000.0f);
    CHECK(ModTransport::IsRunning());
    CHECK(ModTransport::TakeStartReset());
}
