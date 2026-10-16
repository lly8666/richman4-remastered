// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace rich4::mod {
inline constexpr std::uint16_t max_tool_amount = 999;
inline constexpr float max_stock_price = 999999.0F;
inline constexpr std::int32_t new_starting_fund = 30000000;
inline constexpr std::int32_t max_starting_fund = 900000000;
inline constexpr std::uint32_t share_supply_multiplier = 10000;
inline constexpr std::uint32_t max_market_shares = 100000000;
// Largest total of 12 x 100m x 999999 is only 1.2e15; INT64 leaves margin.
inline constexpr std::int32_t fixed_price_index = 1;

// Equipping, dismounting, curses and magic can add tools without using the
// normal receive_tool path. Keep the same maximum at all these boundaries.
template <typename Count> inline void grant_equipped_tool(Count &count) noexcept {
    if (count < max_tool_amount)
        ++count;
}
inline void credit_points(std::uint16_t &points, std::uint32_t amount) noexcept {
    const auto credited = std::uint64_t(points) + amount;
    points = static_cast<std::uint16_t>(credited > UINT16_MAX ? UINT16_MAX : credited);
}
}
