// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "rich4/random.hpp"
#include "rich4/world.hpp"
#include <functional>
#include <string>

namespace rich4::gods {
struct Callbacks {
    std::function<void(std::uint16_t)>
        remove_object; // Full original removal/respawn; owns modifier subtraction.
    std::function<std::int32_t(std::uint8_t)>
        choose_money; // God kind1/2/5/6; original decimal slot machine, unindexed.
    std::function<void(std::int32_t, std::int32_t, std::int32_t, std::uint32_t)> pay;
    std::function<bool()> game_over;
    std::function<void(std::size_t, std::uint8_t)>
        hospital; // Includes injury first, then confinement/insurance.
    std::function<void(std::size_t, std::uint8_t)> voice;
    std::function<std::uint32_t(std::uint16_t)> upgrade_property; // Original40b110 result bits.
    std::function<void(std::uint16_t)> downgrade_property;
    std::function<void(std::size_t, std::size_t, std::int32_t)> hostility;
    // Original god effects display card-name result boxes after the appearance
    // movie and arrival caption. arg1/arg2 are awarded/lost card IDs (0 if
    // absent), removed is number of cards discarded by half/all effects.
    // Pure presentation: must not consume RNG or change the original inventory.
    std::function<void(std::size_t, std::uint8_t, std::uint8_t, std::uint8_t, unsigned)>
        cards_changed;
    // Optional, presentation only: the god (kind) just attached to the player, before its effect
    // runs (rich4_gods.asm:472 plays the appearance FLC here). Must not draw randomness.
    std::function<void(std::size_t, std::uint8_t)> appeared;
    // Presentation only: the god's arrival caption (object kind), after any bad-god speech.
    std::function<void(std::size_t, std::uint8_t)> arrival;
    // Presentation only: the carried god (object kind) is leaving the player (rising animation).
    std::function<void(std::size_t, std::uint8_t)> leaving;
    // Optional, presentation only: Effect.mkf cue for the player (54 god leaves, 35/36 gift pickup).
    std::function<void(std::size_t, std::uint8_t)> sound;
    // Presentation only: the dog (kind 11) was met, on foot (fight, then hospital) or riding.
    std::function<void(std::size_t, bool)> dog;
};
struct Result {
    bool supported = true, applied = false, stop_movement = false;
    std::string message;
};

// Exact original decimal-spinner frame rules. One frame is nominally30ms;
// the caller supplies time/input. tick consumes RNG only at original refreshes.
class MoneySpinner {
  public:
    explicit MoneySpinner(std::uint8_t god_kind);
    void tick(Random &, bool computer = false);
    void stop() noexcept;
    [[nodiscard]] bool finished() const noexcept {
        return phase_ == 10;
    }
    [[nodiscard]] std::int32_t amount() const noexcept;
    [[nodiscard]] unsigned phase() const noexcept {
        return phase_;
    }
    // Reel positions 0..19 (displayed digit * 2, odd = between digits), for presentation.
    [[nodiscard]] const std::array<std::uint8_t, 4> &digits() const noexcept {
        return digits_;
    }

  private:
    bool advance_digits(unsigned mask, unsigned slowing_digit);
    std::array<std::uint8_t, 4> digits_{};
    unsigned omitted_digit_ = 0, phase_ = 1, frames_ = 0;
    unsigned slowing_period_ = 0, slowing_elapsed_ = 0, last_slowing_digit_ = 0;
};
[[nodiscard]] std::int32_t auto_money(std::uint8_t god_kind, Random &);

// The original predicate: types1..10,12,15 can attach; object0 never can.
[[nodiscard]] bool attachable(const World &, std::uint16_t object);
Result attach(World &, Random &, std::size_t player, std::uint16_t object, std::uint16_t source_node = 0,
              const Callbacks &callbacks = {});
Result detach(World &, std::size_t player, const Callbacks &callbacks = {});
// Normal-player object encounters. Attachment,11/13/14/17/18 require a stopped
// player; barriers16 stop movement on passage. Kind15 is not picked up by walking.
Result touch(World &, Random &, std::size_t player, std::uint16_t object, bool stopped,
             const Callbacks &callbacks = {});
// Original40f381, after the normal property landing action. Original40f8be is
// invoked separately only by the normal successful purchase/upgrade path.
Result after_property(World &, std::size_t player, std::uint16_t node, const Callbacks &callbacks = {});
Result after_successful_purchase(World &, Random &, std::size_t player, std::uint16_t node,
                                 const Callbacks &callbacks = {});
} // namespace rich4::gods
