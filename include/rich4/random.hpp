// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace rich4 {

// Rich4 v3.11 uses Watcom's generator, not the MSVCRT rand linked by the
// reconstructed Windows build. Each game owns a stream; no process globals.
class Random {
  public:
    explicit Random(std::uint32_t seed) noexcept : state_(seed) {}
    void seed(std::uint32_t value) noexcept {
        state_ = value;
    }
    [[nodiscard]] std::uint16_t next() noexcept;
    [[nodiscard]] std::uint32_t state() const noexcept {
        return state_;
    }

  private:
    std::uint32_t state_;
};

// Original Windows Rich4 calls srand(GetTickCount()) after loading a save. A
// DAT does not contain the CRT RNG state, so interactive reloads should use a
// fresh monotonic-clock seed instead of replaying a native .rng sidecar.
// The increment ensures even two loads in the same millisecond get new seeds.
// Fixed-seed automated replays bypass this helper and restore their sidecar.
class LoadSeedSequence {
  public:
    [[nodiscard]] std::uint32_t next(std::uint32_t monotonic_milliseconds) noexcept {
        return monotonic_milliseconds + ++sequence_;
    }

  private:
    std::uint32_t sequence_ = 0;
};

} // namespace rich4
