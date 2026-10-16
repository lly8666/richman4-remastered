// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace rich4 {
class World;
class Random;

namespace stocks {

struct SaleResult {
    std::int64_t proceeds{};
    bool commercial_owner_changed{};
};

struct PurchaseResult {
    std::int64_t cost{};
    bool commercial_owner_changed{};
};

// Original 0x4294d5: reinsert one shareholder after an external transfer.
// Reorders only the linked company; does not touch money, holdings or quotas.
// Equal holdings retain the other incumbent's priority.
bool update_commercial_owner(World &world, std::size_t player, std::size_t stock);

enum class PriceTrend : std::uint8_t { rising, limit_up, falling, limit_down, unchanged };
// Original 0x4295ea, including current quotes exactly at the rounded daily cap.
[[nodiscard]] PriceTrend trend(const World &world, std::size_t stock);

// Original 0x428d2a. Market shares use current quote and bank; primary company
// shares use trunc(company valuation / 10000) and cash. The caller enforces UI
// quantity limits; this boundary rejects negative/zero quantities or shortages.
PurchaseResult buy_stock(World &world, std::size_t player, std::size_t stock, std::int32_t amount,
                         bool from_market);

// Original 0x428ec5: percentage movement, price-band tick rounding, and bounds
// [1,999999] in MOD. Finite input price domain [0,999999], movement domain [-10,10].
[[nodiscard]] float adjusted_price(float previous_price, float percent);

// Original 0x428e23. Player and stock indices are zero based; amount must be
// nonnegative and no greater than the holding. true credits the BANK account,
// false credits the public/liquidation fund (original 0x499080).
SaleResult sell_stock(World &world, std::size_t player, std::size_t stock, std::int32_t amount,
                      bool credit_bank);

// Original 0x429040: apply news/card limit-up or limit-down flags. Zero selects
// all twelve stocks; 1..12 selects one. Uses previous price, overwrites the last
// history sample, and does not advance the history cursor or consume randomness.
// This is NOT the daily random market update at 0x4291d6.
std::size_t update_stocks(World &world, unsigned one_based_or_zero_all);

// Original 0x4239b9: cash + bank - loan, stock value with the original per-stock
// binary32 accumulator conversion, and owned land/facility acquisition values.
// Does not multiply property value by the price index.
[[nodiscard]] std::int64_t player_wealth(const World &world, std::size_t player);

// Original 0x42915a, called at the start of each player's turn. Consumes one draw
// per stock whose available share count exceeds 1000.
void refresh_trade_limits(World &world, Random &random);

// Original 0x4291d6. The caller supplies its calendar holiday decision (Sunday or
// map-specific holiday); news_effect_days is checked here. Neither reseeds nor
// reads a host clock. Returns false when closed, preserving every stock/RNG bit.
bool daily_update(World &world, Random &random, bool calendar_holiday);

} // namespace stocks
} // namespace rich4
