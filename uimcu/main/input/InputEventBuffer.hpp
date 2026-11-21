#pragma once

#include <cstdint>
#include <climits>

namespace CTAG::CTRL {

struct InputEvent {
    enum Type : uint8_t {
        ENC_DELTA = 0, BTN1_SHORT = 1, BTN1_LONG = 2,
        BTN2_SHORT = 3, BTN2_LONG = 4, BTN1_DOUBLE = 5, ENC2_DELTA = 6,
    };
    Type type;
    int16_t delta;
};

// Caller supplies synchronization. Coalesce only adjacent turns: a button or
// the other encoder is an ordering boundary (list selection affects edits).
class InputEventBuffer {
public:
    static constexpr int Capacity = 64;

    bool Push(InputEvent event) {
        const bool encoder = IsEncoder(event);
        if (encoder && event.delta == 0) return true;
        if (encoder && count_ && At(count_ - 1).type == event.type) {
            int sum = At(count_ - 1).delta + event.delta;
            if (sum >= INT16_MIN && sum <= INT16_MAX) {
                At(count_ - 1).delta = sum;
                if (sum == 0) --count_;
                return true;
            }
        }
        if (count_ == Capacity) {
            int discard = 0;
            while (discard < count_ && !IsEncoder(At(discard))) ++discard;
            if (discard == count_) return false; // never discard a button
            for (int i = discard; i + 1 < count_; ++i) At(i) = At(i + 1);
            --count_;
        }
        At(count_++) = event;
        return true;
    }

    bool Pop(InputEvent &event) {
        if (!Peek(event)) return false;
        head_ = (head_ + 1) % Capacity;
        --count_;
        return true;
    }

    bool Peek(InputEvent &event) const {
        if (!count_) return false;
        event = events_[head_];
        return true;
    }

private:
    static bool IsEncoder(InputEvent event) {
        return event.type == InputEvent::ENC_DELTA || event.type == InputEvent::ENC2_DELTA;
    }
    InputEvent &At(int index) { return events_[(head_ + index) % Capacity]; }
    InputEvent events_[Capacity]{};
    int head_ = 0;
    int count_ = 0;
};

// Debounce the candidate, rather than imposing a minimum interval between
// accepted edges. Invalid ADC samples cancel an unconfirmed transition.
class ButtonDebouncer {
public:
    bool Update(int sample, uint32_t now, uint32_t stableTicks) {
        if (sample < 0) {
            candidate_ = state_;
            since_ = now;
            return false;
        }
        if (sample != candidate_) {
            candidate_ = sample;
            since_ = now;
        }
        if (candidate_ == state_ || now - since_ < stableTicks) return false;
        state_ = candidate_;
        return true;
    }
    int State() const { return state_; }
private:
    int state_ = 0;
    int candidate_ = 0;
    uint32_t since_ = 0;
};

} // namespace CTAG::CTRL
