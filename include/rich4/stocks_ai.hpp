// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rich4 {
class World;
class Random;

namespace stocks_ai {
struct Trade {
    std::size_t stock = 0;
    std::int32_t quantity = 0;
    std::int64_t value = 0; // Purchase cost or sale proceeds, credited/debited to bank.
    bool commercial_owner_changed = false;
};

// Original 0x42bf03 and 0x42c79f. These include their own probability/profile/
// loan gates, so callers must not prefilter them or consume an extra RNG draw.
// market_closed is the original news/calendar result, not a UI availability flag.
[[nodiscard]] std::vector<Trade> buy(World &, Random &, std::size_t player, bool market_closed);
[[nodiscard]] std::vector<Trade> sell(World &, Random &, std::size_t player, bool market_closed);
} // namespace stocks_ai
} // namespace rich4
