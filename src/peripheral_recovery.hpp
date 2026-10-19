#pragma once
#include <cstdint>

namespace cheeky::foveated_dlss {
// Bounded workaround, not a claim that initialization's NGX Reset is absent.
// Lifetime is the game's DLSS view; resource/scale changes must not rearm it.
struct PeripheralStartupRecovery {
    static constexpr unsigned warmup_evaluations = 120;
    unsigned successes{}, eye{}, preset{}, flags{};
    std::uint64_t session{};
    bool eligible{}, consumed{};

    bool begin(bool ready, std::uint64_t next_session, unsigned next_eye,
               unsigned next_preset, unsigned next_flags, bool compatible) noexcept {
        if (!ready || !compatible || !eligible || session != next_session ||
            eye != next_eye || preset != next_preset || flags != next_flags)
            successes = 0;
        eligible = ready;
        session = next_session; eye = next_eye;
        preset = next_preset; flags = next_flags;
        if (!consumed && eligible && successes >= warmup_evaluations) {
            consumed = true;
            return true;
        }
        return false;
    }
    void completed(bool success) noexcept {
        if (!success) successes = 0;
        else if (eligible && !consumed && successes < warmup_evaluations) ++successes;
    }
};
}
