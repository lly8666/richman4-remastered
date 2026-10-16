// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "rich4/random.hpp"
#include "rich4/world.hpp"
#include <functional>
#include <string>
#include <vector>

namespace rich4::special_actors {

enum class EffectKind {
    points_taken,
    card_taken,
    tool_received,
    points_received,
    bank_taken,
    property_taken,
    blocked,
    hospitalized,
    returned_home
};
struct Effect {
    EffectKind kind;
    std::size_t actor = 4;    // Original actor slot, 4..7.
    std::int32_t target = -1; // Victim slot or encoded commercial payer where applicable.
    std::int64_t amount = 0;  // Wide MOD money or original small points/ID.
    std::uint16_t node = 0;
};
struct Result {
    bool supported = true;
    bool stop_movement = false;
    unsigned planned_steps = 0, moved_steps = 0;
    std::string message;
    std::vector<Effect> effects;
};
struct Callbacks {
    // Use the same finance routine as normal players (including bankruptcy).
    std::function<void(std::int32_t, std::int32_t, std::int64_t, std::uint32_t)> pay;
    // Original removal includes supply return and counterpart respawning.
    std::function<void(std::uint16_t)> remove_object;
    // These receive the special actor slot; do not call normal-player injury.
    std::function<void(std::size_t, std::uint8_t)> prison, hospital;
    std::function<void(std::size_t, std::uint8_t)> voice;
    std::function<bool()> game_over;
    std::function<void(std::size_t, std::uint16_t)> after_step;
};

// Called only for an active actor's own turn. Expiring 1 becomes 0x80 for this
// turn; an existing high bit clears the whole counter before decrementing.
void age_status(SpecialActor &) noexcept;
// Routes in original neighbor order, honoring blocked direction bits and the
// previous node. One RNG call whenever at least one forward route exists.
// Moves to the exact endpoint; pixel interpolation belongs to presentation.
std::uint16_t move_one_step(World &, Random &, std::size_t slot);
Result interact(World &, Random &, std::size_t slot, bool stopped, const Callbacks &);
// Complete one active slot 4..7 after the normal players and before the new day.
// Does not change header.current_player. Slot 8 (robot) is a separate routine.
Result turn(World &, Random &, std::size_t slot, const Callbacks &);

} // namespace rich4::special_actors
