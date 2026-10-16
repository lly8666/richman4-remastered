// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from Iru Cai's asm/rich4_stocks.asm, original entries 0x428e23,
// 0x428ec5, 0x429040 and 0x4294d5. No Windows ABI or host pointer fields.
#include "rich4/stocks.hpp"
#include "rich4/mod_rules.hpp"
#include "rich4/legacy_float.hpp"
#include "rich4/random.hpp"
#include "rich4/world.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace rich4::stocks {
namespace {

using numeric::Extended;

// MOD accounting: all money arithmetic is signed 64-bit and checked before
// committing a trade. Never reproduce the original 32-bit wrap for large trades.
std::int64_t safe_add(std::int64_t a, std::int64_t b) {
    const auto result = __int128(a) + b;
    if (result > INT64_MAX || result < INT64_MIN)
        throw std::overflow_error("MOD finance balance exceeds signed 64-bit range");
    return static_cast<std::int64_t>(result);
}

// Exact positive IEEE754 binary32 quote times an integral share count,
// truncated towards zero (x87 FISTP). MAX: 1e9 shares * 999999 < 1e15.
std::int64_t sale_proceeds(float quote, std::int64_t amount) {
    if (!std::isfinite(quote) || quote < 0 || quote > mod::max_stock_price ||
        amount < 0 || amount > mod::max_holding_shares)
        throw std::invalid_argument("MOD stock quote or quantity outside supported domain");
    if (!quote || !amount)
        return 0;
    const auto raw = std::bit_cast<std::uint32_t>(quote);
    const auto exponent = (raw >> 23) & 255U;
    const auto significand = std::uint64_t((raw & 0x7fffffU) | (exponent == 0 ? 0U : 0x800000U));
    const auto product = significand * std::uint64_t(amount);
    const int shift = exponent == 0 ? -149 : int(exponent) - 150;
    const auto result = shift >= 0 ? __uint128_t(product) << shift
                                    : shift < -64 ? __uint128_t(0) : __uint128_t(product) >> -shift;
    if (result > INT64_MAX)
        throw std::overflow_error("MOD stock transaction value exceeds 64-bit");
    return static_cast<std::int64_t>(result);
}

Commercial *linked_commercial(World &world, std::size_t stock) {
    const auto id = world.stocks[stock].flags_4;
    if (id == 0) {
        return nullptr;
    }
    if (id >= world.commercials.size()) {
        throw std::invalid_argument("stock refers to an absent commercial");
    }
    auto &commercial = world.commercials[id];
    for (const auto owner : commercial.shareholder_order) {
        if (owner > world.players.size()) {
            throw std::invalid_argument("commercial shareholder rank refers to an absent player");
        }
    }
    return &commercial;
}

bool update_commercial_owner(World &world, Commercial *commercial, std::size_t player, std::size_t stock) {
    if (!commercial) {
        return false;
    }
    auto &order = commercial->shareholder_order;
    const auto owner_code = static_cast<std::uint8_t>(player + 1);
    // Original memcpy moves at most three bytes forward within this four-byte
    // list. Spell out the overlap-safe operation rather than copy raw structs.
    if (const auto found = std::find(order.begin(), order.end(), owner_code); found != order.end()) {
        std::move(found + 1, order.end(), found);
        order.back() = 0;
    }
    const auto holding = world.players[player].stocks[stock].amount;
    if (holding != 0) {
        std::size_t insertion = 0;
        for (int index = 2; index >= 0; --index) {
            const auto position = static_cast<std::size_t>(index);
            const auto other = order[position];
            if (other == 0) {
                continue;
            }
            if (world.players[other - 1].stocks[stock].amount < holding) {
                order[position + 1] = other;
            } else {
                insertion = position + 1;
                break;
            }
        }
        order[insertion] = owner_code;
    }
    const bool changed = commercial->owner_code != order.front();
    commercial->owner_code = order.front();
    return changed; // The application refreshes its minimap; no UI call here.
}

} // namespace

bool update_commercial_owner(World &world, std::size_t player, std::size_t stock) {
    if (world.players.size() > 4 || player >= world.players.size() || stock >= world.stocks.size()) {
        throw std::out_of_range("commercial shareholder player or stock index out of range");
    }
    return update_commercial_owner(world, linked_commercial(world, stock), player, stock);
}

float adjusted_price(float previous, float percent) {
    if (!std::isfinite(previous) || previous < 0 || previous > mod::max_stock_price || !std::isfinite(percent) ||
        percent < -10 || percent > 10) {
        throw std::invalid_argument("stock price update is outside the finite original market domain");
    }
    // Explicit binary32 stores mirror the original stack slots. The tick is
    // selected using the unrounded candidate, and rounds the CHANGE relative
    // to the previous quote, not the absolute quote to a decimal grid.
    const float candidate =
        ((Extended::from(100) + Extended::from(percent)) / Extended::from(100) * Extended::from(previous))
            .single();
    const float change = (percent > 0 ? Extended::from(candidate) - Extended::from(previous)
                                      : Extended::from(previous) - Extended::from(candidate))
                             .single();
    const double tick = candidate < 5     ? 0.01
                        : candidate < 15  ? 0.05
                        : candidate < 50  ? 0.1
                        : candidate < 150 ? 0.5
                                          : 1.0;
    const auto delta = Extended::from(change);
    const auto quantum = Extended::from(tick);
    const auto quotient = (delta / quantum).integer();
    const auto remainder = delta - quantum * Extended::from(quotient);
    const float rounded =
        (percent > 0 ? Extended::from(candidate) - remainder : Extended::from(candidate) + remainder)
            .single();
    return std::clamp(rounded, 1.0F, mod::max_stock_price);
}

PriceTrend trend(const World &world, std::size_t stock) {
    const auto &value = world.stocks.at(stock);
    const auto previous = std::bit_cast<float>(value.float_bits[1]);
    const auto current = std::bit_cast<float>(value.float_bits[2]);
    if (current > previous) {
        return current >= adjusted_price(previous, 10) ? PriceTrend::limit_up : PriceTrend::rising;
    }
    if (current < previous) {
        return current <= adjusted_price(previous, -10) ? PriceTrend::limit_down : PriceTrend::falling;
    }
    return PriceTrend::unchanged;
}

PurchaseResult buy_stock(World &world, std::size_t player, std::size_t stock, std::int32_t amount,
                         bool from_market) {
    if (world.players.size() > 4 || player >= world.players.size() || stock >= world.stocks.size())
        throw std::out_of_range("MOD stock purchase player or index out of range");
    auto &account = world.players[player];
    auto &holding = account.stocks[stock];
    auto &company = world.stocks[stock];
    auto *commercial = linked_commercial(world, stock);
    if (amount <= 0 || holding.amount < 0 ||
        std::int64_t(amount) > mod::max_holding_shares - holding.amount)
        throw std::invalid_argument("MOD stock purchase quantity exceeds holding cap");
    std::int64_t cost;
    if (from_market) {
        if (std::uint64_t(amount) > company.shares_available ||
            std::uint64_t(amount) > company.flags_10)
            throw std::invalid_argument("MOD stock purchase exceeds market availability");
        cost = sale_proceeds(std::bit_cast<float>(company.float_bits[2]), amount);
        if (cost > account.state.bank_balance || account.state.bank_balance < 0)
            throw std::invalid_argument("MOD stock purchase exceeds available bank cash");
    } else {
        if (!commercial || commercial->valuation < 0 || amount > commercial->shares_remaining)
            throw std::invalid_argument("MOD primary share purchase exceeds company availability");
        const auto unit = commercial->valuation / 10000;
        cost = std::int64_t(unit) * amount;
        if (cost > account.state.cash || account.state.cash < 0)
            throw std::invalid_argument("MOD primary share purchase exceeds cash");
    }
    const auto previous_cost = sale_proceeds(std::bit_cast<float>(holding.raw_cost_basis), holding.amount);
    const auto total_cost = safe_add(previous_cost, cost);
    const auto new_quantity = holding.amount + amount;
    const auto basis = (Extended::from(static_cast<double>(total_cost)) /
                        Extended::from(static_cast<double>(new_quantity))).single();
    // All validations and arithmetic are complete before mutating ANY balances.
    if (from_market) {
        company.shares_available -= static_cast<std::uint32_t>(amount);
        company.flags_10 -= static_cast<std::uint32_t>(amount);
        account.state.bank_balance -= cost;
    } else {
        commercial->shares_remaining -= amount;
        account.state.cash -= cost;
    }
    holding.amount = new_quantity;
    holding.raw_cost_basis = std::bit_cast<std::uint32_t>(basis);
    return {cost, update_commercial_owner(world, commercial, player, stock)};
}

SaleResult sell_stock(World &world, std::size_t player, std::size_t stock, std::int32_t amount,
                      bool credit_bank) {
    if (world.players.size() > 4 || player >= world.players.size() || stock >= world.stocks.size())
        throw std::out_of_range("MOD stock sale player or index out of range");
    auto &holding = world.players[player].stocks[stock];
    auto &company = world.stocks[stock];
    if (amount < 0 || amount > holding.amount || holding.amount > mod::max_holding_shares)
        throw std::invalid_argument("MOD stock sale exceeds owned holdings");
    const auto proceeds = sale_proceeds(std::bit_cast<float>(company.float_bits[2]), amount);
    if (std::uint64_t(company.shares_available) + std::uint64_t(amount) > mod::max_market_shares ||
        std::uint64_t(company.flags_10) + std::uint64_t(amount) > mod::max_market_shares)
        throw std::invalid_argument("MOD stock return exceeds supported market supply");
    const auto credited = safe_add(credit_bank ? world.players[player].state.bank_balance : world.public_fund,
                                  proceeds);
    auto *commercial = linked_commercial(world, stock);
    // Commit after ALL checks: no partial credit, no silent share count rollover.
    holding.amount -= amount;
    if (holding.amount == 0)
        holding.raw_cost_basis = 0;
    company.shares_available += static_cast<std::uint32_t>(amount);
    company.flags_10 += static_cast<std::uint32_t>(amount);
    if (credit_bank)
        world.players[player].state.bank_balance = credited;
    else
        world.public_fund = credited;
    return {proceeds, update_commercial_owner(world, commercial, player, stock)};
}

std::size_t update_stocks(World &world, unsigned selection) {
    if (selection > world.stocks.size() || world.stock_history_cursor < 0 ||
        world.stock_history_cursor >= 144) {
        throw std::out_of_range("forced stock selection or history cursor out of range");
    }
    const auto previous_sample = world.stock_history_cursor == 0 ? 143 : world.stock_history_cursor - 1;
    const std::size_t first = selection == 0 ? 0 : selection - 1;
    const std::size_t end = selection == 0 ? world.stocks.size() : selection;
    std::size_t updated = 0;
    for (std::size_t index = first; index < end; ++index) {
        auto &stock = world.stocks[index];
        if (stock.news_movement == 0) {
            continue;
        }
        const float change = (stock.news_movement & 0xf0U) != 0 ? 10.0F : -10.0F;
        const auto quote = adjusted_price(std::bit_cast<float>(stock.float_bits[1]), change);
        stock.float_bits[4] = std::bit_cast<std::uint32_t>(change);
        stock.float_bits[2] = std::bit_cast<std::uint32_t>(quote);
        world.stock_history_bits[index][static_cast<std::size_t>(previous_sample)] = stock.float_bits[2];
        ++updated;
    }
    return updated;
}

std::int64_t player_wealth(const World &world, std::size_t player) {
    if (player >= world.players.size())
        throw std::out_of_range("MOD wealth player out of range");
    const auto &account = world.players[player];
    // A stock worth 999999 x 100m = ~100 trillion. Never clip the result to
    // INT32_MAX; all intermediate additions remain checked signed 64-bit.
    auto total = safe_add(safe_add(account.state.cash, account.state.bank_balance), -account.state.loan);
    for (std::size_t stock = 0; stock < world.stocks.size(); ++stock) {
        const auto quote = std::bit_cast<float>(world.stocks[stock].float_bits[2]);
        total = safe_add(total, sale_proceeds(quote, account.stocks[stock].amount));
    }
    const auto owner = player + 1;
    for (std::size_t id = 1; id < world.lands.size(); ++id) {
        const auto &land = world.lands[id];
        if (land.economy.owner_code == owner) {
            const auto levels = land.economy.chain_store ? 1 : land.economy.level;
            total = safe_add(total, std::int64_t(land.land_price) + land.price_per_level * levels);
        }
    }
    for (std::size_t id = 1; id < world.facilities.size(); ++id) {
        const auto &facility = world.facilities[id];
        if (facility.owner_code == owner)
            total = safe_add(total, std::int64_t(facility.land_price) +
                                   facility.price_per_level() * facility.level);
    }
    return total;
}

void refresh_trade_limits(World &world, Random &random) {
    for (auto &stock : world.stocks) {
        if (stock.shares_available <= 1000U * mod::share_supply_multiplier) {
            stock.flags_10 = stock.shares_available;
        } else {
            const auto fraction = Extended::from(random.next() % 2000 + 1000) / Extended::from(10000);
            stock.flags_10 =
                static_cast<std::uint32_t>((fraction * Extended::from(stock.shares_available)).integer());
        }
    }
}

bool daily_update(World &world, Random &random, bool calendar_holiday) {
    if (calendar_holiday || world.news_effect_days != 0) {
        return false;
    }
    if (world.stock_history_cursor < 0 || world.stock_history_cursor >= 144) {
        throw std::out_of_range("daily stock history cursor out of range");
    }
    // Explicit binary32 stores match the original globals and stock fields.
    const auto market = (Extended::from(int(random.next()) - 16384) / Extended::from(4097)).single();
    world.stock_market_movement_bits = std::bit_cast<std::uint32_t>(market);
    float total = 0;
    for (std::size_t index = 0; index < world.stocks.size(); ++index) {
        auto &stock = world.stocks[index];
        stock.float_bits[1] = stock.float_bits[2];
        const auto previous = std::bit_cast<float>(stock.float_bits[1]);
        float movement;
        if (stock.suspension_days != 0) {
            movement = 0;
        } else if (stock.news_movement != 0) {
            movement = (stock.news_movement & 0xf0U) != 0 ? 10.0F : -10.0F;
        } else {
            const auto impulse = Extended::from(int(random.next()) - 16384) / Extended::from(1171);
            stock.raw_32 = std::bit_cast<std::uint32_t>(impulse.single());
            movement = (impulse * Extended::from(std::bit_cast<float>(stock.float_bits[3])) +
                        Extended::from(std::bit_cast<float>(stock.float_bits[4])))
                           .single();
            movement = (Extended::from(movement) + Extended::from(market)).single();
            // Excessive prices damp positive moves and amplify negative moves;
            // undervalued prices do the opposite. Thresholds differ for linked
            // companies and standalone stocks, exactly as in 0x4291d6.
            float reference;
            float upper_multiple;
            double lower_multiple;
            if (const auto *commercial = linked_commercial(world, index)) {
                reference = (Extended::from(commercial->valuation) / Extended::from(10000)).single();
                upper_multiple = 3;
                lower_multiple = 0.85; // The original constant is binary64.
            } else {
                reference = std::bit_cast<float>(stock.float_bits[0]);
                upper_multiple = 8;
                lower_multiple = 0.5;
            }
            if (previous > reference) {
                const auto upper = (Extended::from(reference) * Extended::from(upper_multiple)).single();
                if (previous > upper) {
                    movement = (Extended::from(movement) * Extended::from(movement > 0 ? 0.5 : 2)).single();
                }
            } else {
                const auto lower = (Extended::from(reference) * Extended::from(lower_multiple)).single();
                if (previous < lower) {
                    movement = (Extended::from(movement) * Extended::from(movement > 0 ? 2 : 0.5)).single();
                }
            }
            movement = std::clamp(movement, -10.0F, 10.0F);
        }
        stock.float_bits[4] = std::bit_cast<std::uint32_t>(movement);
        const auto current = adjusted_price(previous, movement);
        stock.float_bits[2] = std::bit_cast<std::uint32_t>(current);
        world.stock_history_bits[index][static_cast<std::size_t>(world.stock_history_cursor)] =
            stock.float_bits[2];
        total = (Extended::from(total) + Extended::from(current)).single();
    }
    world.stock_history_cursor = (world.stock_history_cursor + 1) % 144;
    world.stock_market_index = (Extended::from(total) * Extended::from(10)).integer();
    return true;
}

} // namespace rich4::stocks
