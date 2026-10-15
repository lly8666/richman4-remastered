// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace rich4 {

using CardSlots = std::array<std::uint8_t, 15>;
using CardSupply = std::array<std::uint8_t, 30>;
using CardPrices = std::array<std::uint8_t, 30>;
using ToolAmounts = std::array<std::uint16_t, 15>;
using ToolSupply = std::array<std::uint8_t, 8>;
using ToolPrices = std::array<std::uint8_t, 13>;

// Slot order is significant. Zero means an empty slot; card IDs are 1..30.
// has_card deliberately accepts zero, matching the original empty-slot query.
[[nodiscard]] std::size_t card_count(const CardSlots &cards) noexcept;
[[nodiscard]] bool has_card(const CardSlots &cards, std::uint8_t card_id) noexcept;
[[nodiscard]] std::uint8_t cheapest_card(const CardSlots &cards, const CardPrices &prices);

// Preserve original byte wrapping in supply counts. Receiving a card does not
// check supply availability; its callers made that decision in the original.
// A full hand discards its first cheapest card before inserting the new one.
void receive_card(CardSlots &cards, CardSupply &supply, const CardPrices &prices, std::uint8_t card_id);
// Removes the first matching ID, shifts all later slots left, and returns it to
// supply. Return value is a new explicit success result (original routine void).
bool consume_card(CardSlots &cards, CardSupply &supply, std::uint8_t card_id);
// Requires packed valid cards followed by zeroes. Rejects malformed hands before
// mutation, rather than reproducing the original consume(0) out-of-bounds write.
bool drop_half_cards(CardSlots &cards, CardSupply &supply);
// Returns the original points value; does not credit cash/points or invoke UI.
[[nodiscard]] std::uint32_t sell_all_cards(CardSlots &cards, CardSupply &supply, const CardPrices &prices);

// Valid tool IDs are 1..13. The two remaining serialized slots are preserved.
// First 8 tools draw from shared supply. Receive caps each tool count at MOD limit 999.
bool receive_tool(ToolAmounts &amounts, ToolSupply &supply, std::uint8_t tool_id);
bool consume_tool(ToolAmounts &amounts, ToolSupply &supply, std::uint8_t tool_id);
// Only the inventory part of original sell_all_tools with traffic_method==0.
// Equipped transport removal, dice reset and sprite refresh are outside this API.
[[nodiscard]] std::uint32_t sell_stored_tools(ToolAmounts &amounts, ToolSupply &supply,
                                              const ToolPrices &prices) noexcept;

} // namespace rich4
