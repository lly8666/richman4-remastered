// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "rich4/random.hpp"
#include "rich4/world.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace rich4::lottery {

inline constexpr std::int32_t ticket_price = 1000;
enum class Purchase { purchased, already_owned, insufficient_cash, inactive_player };

// Numbers shown to players are 1..36; owner values remain slot+1 in World.
[[nodiscard]] std::vector<std::uint8_t> available_numbers(const World &world);
[[nodiscard]] Purchase buy_ticket(World &world, std::size_t actor, std::uint32_t number);
// Original computer-player rule: cash must be STRICTLY greater than1000.
// Uses one outcome draw among free numbers; no draw when no purchase is possible.
[[nodiscard]] std::optional<std::uint8_t> buy_random_ticket(World &world, Random &random, std::size_t actor);
// Original bankruptcy cleanup: remove this player's tickets, without refund.
void discard_player_tickets(World &world, std::size_t actor);

struct Draw {
    bool held = false; // No drawing takes place when there are no sold tickets.
    std::uint8_t number = 0;
    std::optional<std::size_t> winner;
    std::int64_t payout = 0;
    bool sold_numbers_only = false; // Original rule when any player holds >10.
};

// Uses the original one-draw outcome rule. Timer-driven Win32 show animations
// are outside this portable state operation and do not consume gameplay RNG.
// A miss retains BOTH tickets and public_fund; a win clears both after payout.
[[nodiscard]] Draw draw(World &world, Random &random);
[[nodiscard]] bool draw_due(SaveDate date) noexcept;

} // namespace rich4::lottery
