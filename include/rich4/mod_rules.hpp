// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace rich4::mod {
inline constexpr std::uint16_t max_tool_amount = 999;
inline constexpr float max_stock_price = 999999.0F;
inline constexpr std::int32_t new_starting_fund = 30000000;
inline constexpr std::int32_t max_starting_fund = 900000000;
inline constexpr std::uint32_t share_supply_multiplier = 10000;
inline constexpr std::uint32_t max_market_shares = 100000000;
inline constexpr std::int64_t max_holding_shares = 1000000000LL;
// Largest total of 12 x 100m x 999999 is only 1.2e15; INT64 leaves margin.
inline constexpr std::int32_t fixed_price_index = 1;

// All MOD financial state uses signed 64-bit arithmetic with a checked
// intermediate, never wrapped legacy 32-bit results.
inline std::int64_t money_add(std::int64_t a, std::int64_t b) {
    const auto result = __int128(a) + b;
    if (result > INT64_MAX || result < INT64_MIN)
        throw std::overflow_error("MOD financial amount exceeds signed 64-bit range");
    return static_cast<std::int64_t>(result);
}
inline std::int64_t money_sub(std::int64_t a, std::int64_t b) {
    // Negating INT64_MIN is undefined before money_add can check it.
    const auto result = __int128(a) - b;
    if (result > INT64_MAX || result < INT64_MIN)
        throw std::overflow_error("MOD financial amount exceeds signed 64-bit range");
    return static_cast<std::int64_t>(result);
}
inline std::int32_t display_i32(std::int64_t a) noexcept {
    return static_cast<std::int32_t>(a < INT32_MIN ? INT32_MIN : a > INT32_MAX ? INT32_MAX : a);
}

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
