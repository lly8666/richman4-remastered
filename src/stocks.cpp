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

std::int32_t add_wrap(std::int32_t first, std::int32_t second) {
    return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(first) +
                                       std::bit_cast<std::uint32_t>(second));
}

std::int32_t subtract_wrap(std::int32_t first, std::int32_t second) {
    return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(first) -
                                       std::bit_cast<std::uint32_t>(second));
}

// Exact positive binary32 price times signed32 nonnegative share count, then
// x87 FISTP's truncation to int32. Integer arithmetic avoids host FP precision
// differences. An overflowing conversion returns x87's integer-indefinite.
std::int32_t sale_proceeds(float quote, std::int32_t amount) {
    if (!std::isfinite(quote) || quote < 0 || amount < 0) {
        throw std::invalid_argument("stock sale requires a finite nonnegative quote and amount");
    }
    const auto raw = std::bit_cast<std::uint32_t>(quote);
    const auto exponent = (raw >> 23) & 255U;
    const std::uint64_t significand = (raw & 0x7fffffU) | (exponent == 0 ? 0 : 0x800000U);
    const auto product = significand * static_cast<std::uint32_t>(amount);
    const int shift = exponent == 0 ? -149 : int(exponent) - 150;
    std::uint64_t integer;
    if (product == 0) {
        return 0;
    }
    if (shift >= 0) {
        if (shift >= 31 || product > (std::uint64_t(INT32_MAX) >> shift)) {
            return INT32_MIN;
        }
        integer = product << shift;
    } else {
        integer = shift <= -64 ? 0 : product >> -shift;
    }
    return integer > INT32_MAX ? INT32_MIN : static_cast<std::int32_t>(integer);
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
    if (world.players.size() > 4 || player >= world.players.size() || stock >= world.stocks.size()) {
        throw std::out_of_range("stock purchase player or stock index out of range");
    }
    auto &account = world.players[player];
    auto &holding = account.stocks[stock];
    auto &company = world.stocks[stock];
    auto *commercial = linked_commercial(world, stock);
    if (amount <= 0 || holding.amount < 0 || amount > INT32_MAX - holding.amount) {
        throw std::invalid_argument("stock purchase requires a positive nonoverflowing quantity");
    }
    std::int32_t cost;
    if (from_market) {
        cost = sale_proceeds(std::bit_cast<float>(company.float_bits[2]), amount);
        if (cost < 0 || cost > account.state.bank_balance || amount > company.shares_available ||
            amount > company.flags_10) {
            throw std::invalid_argument("stock purchase exceeds market shares or bank balance");
        }
    } else {
        if (!commercial || commercial->valuation < 0 || amount > commercial->shares_remaining) {
            throw std::invalid_argument("company purchase exceeds primary shares");
        }
        const auto value = std::int64_t(commercial->valuation / 10000) * amount;
        if (value > account.state.cash || value > INT32_MAX) {
            throw std::invalid_argument("company purchase exceeds cash balance");
        }
        cost = static_cast<std::int32_t>(value);
    }
    const auto previous_cost = sale_proceeds(std::bit_cast<float>(holding.raw_cost_basis), holding.amount);
    const auto total_cost = add_wrap(previous_cost, cost);
    const auto new_quantity = holding.amount + amount;
    const auto basis = (Extended::from(total_cost) / Extended::from(new_quantity)).single();
    if (from_market) {
        company.shares_available = static_cast<std::uint16_t>(company.shares_available - amount);
        company.flags_10 = static_cast<std::uint16_t>(company.flags_10 - amount);
        account.state.bank_balance = subtract_wrap(account.state.bank_balance, cost);
    } else {
        commercial->shares_remaining -= amount;
        account.state.cash = subtract_wrap(account.state.cash, cost);
    }
    holding.amount = new_quantity;
    holding.raw_cost_basis = std::bit_cast<std::uint32_t>(basis);
    return {cost, update_commercial_owner(world, commercial, player, stock)};
}

SaleResult sell_stock(World &world, std::size_t player, std::size_t stock, std::int32_t amount,
                      bool credit_bank) {
    if (world.players.size() > 4 || player >= world.players.size() || stock >= world.stocks.size()) {
        throw std::out_of_range("stock sale player or stock index out of range");
    }
    auto &holding = world.players[player].stocks[stock];
    if (amount < 0 || amount > holding.amount) {
        throw std::invalid_argument("stock sale amount exceeds available holdings");
    }
    auto &company = world.stocks[stock];
    const auto proceeds = sale_proceeds(std::bit_cast<float>(company.float_bits[2]), amount);
    auto *commercial = linked_commercial(world, stock);
    holding.amount -= amount;
    if (holding.amount == 0) {
        holding.raw_cost_basis = 0;
    }
    // ADD word retains the original low 16 bits, independently of share count.
    company.shares_available =
        static_cast<std::uint16_t>(company.shares_available + static_cast<std::uint32_t>(amount));
    company.flags_10 = static_cast<std::uint16_t>(company.flags_10 + static_cast<std::uint32_t>(amount));
    if (credit_bank) {
        auto &bank = world.players[player].state.bank_balance;
        bank = add_wrap(bank, proceeds);
    } else {
        world.public_fund = add_wrap(world.public_fund, proceeds);
    }
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

std::int32_t player_wealth(const World &world, std::size_t player) {
    if (player >= world.players.size()) {
        throw std::out_of_range("wealth player index out of range");
    }
    const auto &account = world.players[player];
    auto total = add_wrap(account.state.cash, account.state.bank_balance);
    total = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(total) -
                                        std::bit_cast<std::uint32_t>(account.state.loan));
    for (std::size_t stock = 0; stock < world.stocks.size(); ++stock) {
        const auto quote = std::bit_cast<float>(world.stocks[stock].float_bits[2]);
        const auto amount = account.stocks[stock].amount;
        if (!std::isfinite(quote) || quote < 0 || quote > mod::max_stock_price || amount < 0 || (quote != 0 && quote < 1)) {
            throw std::invalid_argument("wealth requires valid market quotes and nonnegative holdings");
        }
        // In the original FILD total / FSTP float loses low integer bits before
        // each stock addition, even for a zero holding. Preserve that sequence.
        const auto rounded_total = static_cast<std::int64_t>(Extended::from(total).single());
        const auto raw = std::bit_cast<std::uint32_t>(quote);
        const auto exponent = (raw >> 23) & 255U;
        const auto significand = (raw & 0x7fffffU) | (exponent == 0 ? 0 : 0x800000U);
        const auto product = std::uint64_t(significand) * static_cast<std::uint32_t>(amount);
        // Market quotes [1,10000] have 10..23 fraction bits. The integer product
        // is exact; split it so cancellation with negative wealth is also exact.
        const unsigned fraction_bits = quote == 0 ? 23U : 150U - exponent;
        const auto integer = product >> fraction_bits;
        const bool fraction = (product & ((std::uint64_t{1} << fraction_bits) - 1)) != 0;
        auto sum = rounded_total + static_cast<std::int64_t>(integer);
        if (sum < 0 && fraction) {
            ++sum; // Truncate the negative fractional sum toward zero.
        }
        total = sum < INT32_MIN || sum > INT32_MAX ? INT32_MIN : static_cast<std::int32_t>(sum);
    }
    const auto owner = player + 1;
    for (std::size_t id = 1; id < world.lands.size(); ++id) {
        const auto &land = world.lands[id];
        if (land.economy.owner_code == owner) {
            const auto levels = land.economy.chain_store != 0 ? 1 : land.economy.level;
            total = add_wrap(total, land.land_price + land.price_per_level * levels);
        }
    }
    for (std::size_t id = 1; id < world.facilities.size(); ++id) {
        const auto &facility = world.facilities[id];
        if (facility.owner_code == owner) {
            total = add_wrap(total, facility.land_price + facility.price_per_level() * facility.level);
        }
    }
    return total;
}

void refresh_trade_limits(World &world, Random &random) {
    for (auto &stock : world.stocks) {
        if (stock.shares_available <= 1000) {
            stock.flags_10 = stock.shares_available;
        } else {
            const auto fraction = Extended::from(random.next() % 2000 + 1000) / Extended::from(10000);
            stock.flags_10 =
                static_cast<std::uint16_t>((fraction * Extended::from(stock.shares_available)).integer());
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
