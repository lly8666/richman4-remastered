// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from mytbk/rich4 asm/rich4.asm, original 0x42bf03 and 0x42c79f.
#include "rich4/stocks_ai.hpp"
#include "rich4/mod_rules.hpp"
#include "rich4/calendar.hpp"
#include "rich4/legacy_float.hpp"
#include "rich4/random.hpp"
#include "rich4/stocks.hpp"
#include "rich4/world.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace rich4::stocks_ai {
namespace {
using numeric::Extended;
Extended number(double value) {
    return Extended::from(value);
}

bool less(Extended a, Extended b) {
    if (!a.significand && !b.significand) {
        return false;
    }
    if (a.negative != b.negative) {
        return a.negative;
    }
    const bool magnitude_less =
        !a.significand || (b.significand && (a.exponent < b.exponent ||
                                             (a.exponent == b.exponent && a.significand < b.significand)));
    const bool equal = a.significand == b.significand && a.exponent == b.exponent;
    return a.negative ? !magnitude_less && !equal : magnitude_less;
}
std::int32_t add(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) + std::uint32_t(b));
}
std::int32_t subtract(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) - std::uint32_t(b));
}
std::int32_t multiply(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) * std::uint32_t(b));
}
float scalar(const StockState &stock, std::size_t index) {
    return std::bit_cast<float>(stock.float_bits[index]);
}
float quote(const StockState &stock) {
    return scalar(stock, 2);
}
float basis(const StockHolding &holding) {
    return std::bit_cast<float>(holding.raw_cost_basis);
}
std::int64_t liquidity(const WorldPlayer &player) {
    return mod::money_add(player.state.cash, player.state.bank_balance);
}

// Loan dates are produced by packed-month addition: e.g. Nov 31 is retained by
// the old game, whose day-index routine then treats it as Dec 1. Zero means -1.
std::int32_t original_day_index(std::uint32_t packed) {
    if (packed == 0) {
        return -1;
    }
    const auto date = calendar::unpack(packed);
    return add(calendar::days_from_1998({date.year, date.month, 1}), int(date.day) - 1);
}
std::int32_t days_until_loan(const World &world, const WorldPlayer &player) {
    const auto &date = world.header.date;
    const auto today = calendar::days_from_1998({date.year, date.month, date.day});
    return subtract(original_day_index(player.status.loan_due_date), today);
}

float history_average(const World &world, std::size_t stock, int periods) {
    int cursor = world.stock_history_cursor - periods;
    if (cursor < 0) {
        cursor += 144;
    }
    float sum = 0;
    unsigned count = 0;
    for (int i = 0; i < periods; ++i) {
        if (cursor == 144) {
            cursor = 0;
        }
        const auto bits = world.stock_history_bits[stock][std::size_t(cursor++)];
        if (bits & 0x7fffffffU) {
            sum = (number(sum) + number(std::bit_cast<float>(bits))).single();
            ++count;
        }
    }
    return count ? (number(sum) / number(count)).single() : sum;
}
float company_average_profit(const World &world, const Commercial &company) {
    const auto months = static_cast<std::int64_t>(world.month_counter);
    const auto value =
        months ? static_cast<std::int32_t>(company.funds_change / months) : company.funds_change;
    return number(value).single();
}
float company_unit_value(const Commercial &company) {
    return number(company.valuation / 10000).single();
}
const Commercial &company_for(const World &world, const StockState &stock) {
    if (!stock.flags_4 || stock.flags_4 >= world.commercials.size()) {
        throw std::invalid_argument("Stock AI references an absent commercial property");
    }
    return world.commercials[stock.flags_4];
}
std::int32_t holdings_value(const World &world, std::size_t actor) {
    std::int32_t total = 0;
    for (std::size_t i = 0; i < world.stocks.size(); ++i) {
        const auto quantity = world.players[actor].stocks[i].amount;
        if (quantity) {
            total = (number(quantity) * number(quote(world.stocks[i])) + number(number(total).single()))
                        .integer();
        }
    }
    return total;
}

std::int32_t company_buy_score(const World &world, std::size_t actor, std::size_t index) {
    const auto &stock = world.stocks[index];
    const auto &company = company_for(world, stock);
    const auto &holding = world.players[actor].stocks[index];
    const auto price = number(quote(stock));
    const auto valuation = number(company_unit_value(company));
    const auto profit = number(company_average_profit(world, company));
    const auto low = number(multiply(5000, world.header.price_index));
    const auto high = number(multiply(10000, world.header.price_index));
    std::int32_t score = 0;
    if (less(number(0), profit) && less(profit, low)) {
        ++score;
    }
    if (!less(profit, low) && less(profit, high)) {
        score += 2;
    }
    if (!less(profit, high)) {
        score += 3;
    }
    if (company.funds > 0 && holding.amount < 5000 && !less(valuation * number(1.2), price)) {
        ++score;
        if (company.owner_code && company.owner_code != actor + 1) {
            const auto owner_quantity = world.players.at(company.owner_code - 1).stocks[index].amount;
            if (add(add(stock.shares_available, holding.amount), company.shares_remaining) > owner_quantity) {
                ++score;
            }
            if (add(stock.flags_10, holding.amount) > owner_quantity) {
                score += 2;
            }
        }
    }
    if (less(price, valuation * number(0.85))) {
        score += 3;
    }
    if (less(price, valuation * number(0.7))) {
        score += 5;
    }
    return score;
}

std::int32_t market_buy_score(const World &world, std::size_t index) {
    const auto &stock = world.stocks[index];
    const auto price = number(quote(stock));
    const auto baseline = number(scalar(stock, 0));
    const auto long_average = number(history_average(world, index, 24));
    const auto short_average = number(history_average(world, index, 6));
    std::int32_t score = 0;
    if (less(price, baseline * number(2.5)) &&
        std::bit_cast<std::int32_t>(stock.float_bits[4]) > 0x40000000 && less(long_average, short_average)) {
        score += 2;
    }
    if (less(price, baseline * number(0.6)) && less(long_average, short_average)) {
        score += 4;
    }
    if (less(short_average, long_average * number(0.5))) {
        score += 2;
    }
    return score;
}

struct RatedStock {
    std::size_t index = 0;
    std::int32_t score = 0;
};
void original_sort(std::array<RatedStock, 12> &entries) {
    // Watcom qsort's <16 branch at 0x457ecc: gap=3 then gap=1, advancing
    // the outer insertion position by the gap too. Preserve ties explicitly.
    for (const std::size_t gap : {3U, 1U}) {
        for (std::size_t i = gap; i < entries.size(); i += gap) {
            for (auto j = i; j >= gap && entries[j - gap].score < entries[j].score; j -= gap) {
                std::swap(entries[j - gap], entries[j]);
            }
        }
    }
}

std::int32_t company_sell_score(const World &world, std::size_t actor, std::size_t index, bool urgent) {
    const auto &stock = world.stocks[index];
    const auto &company = company_for(world, stock);
    const auto &holding = world.players[actor].stocks[index];
    const auto price = number(quote(stock));
    const auto cost = number(basis(holding));
    const auto valuation = number(company_unit_value(company));
    const auto profit = number(company_average_profit(world, company));
    std::int32_t total_quantity = 0;
    for (const auto &player : world.players) {
        total_quantity = add(total_quantity, player.stocks[index].amount);
    }
    // Original DE F1 is FDIVRP: total / own, not the intuitive ownership fraction.
    const auto share = (number(total_quantity) / number(holding.amount)).single();
    const bool owns_company = company.owner_code == actor + 1;
    const auto day = world.header.date.day;
    std::int32_t score = 0;
    if (company.funds <= multiply(-10000, world.header.price_index) && less(number(0.6), number(share)) &&
        day > 10 && day < 15) {
        score += 3;
    }
    if (company.funds <= multiply(-6000, world.header.price_index) && less(cost * number(1.2), price) &&
        !owns_company && day > 8 && day < 15) {
        score += 2;
    }
    if (!less(number(multiply(10000, world.header.price_index)), profit) &&
        !less(price, valuation * number(2)) && less(cost * number(1.3), price) &&
        less(number(share), number(0.4))) {
        ++score;
    }
    if (company.owner_code && !owns_company) {
        const auto owner_quantity = world.players.at(company.owner_code - 1).stocks[index].amount;
        if (add(add(holding.amount, stock.shares_available), company.shares_remaining) < owner_quantity &&
            company.funds <= 0 && !less(price, cost * number(1.5))) {
            ++score;
        }
    }
    if (less(valuation * number(2), price) && less(cost * number(2), price) &&
        quote(stock) < scalar(stock, 1) && !owns_company) {
        score += 2;
    }
    if (less(valuation * number(3), price) && less(cost * number(3), price) &&
        quote(stock) < scalar(stock, 1) && owns_company) {
        score += 2;
    }
    return score + (urgent ? 1 : 0);
}

std::int32_t ratio_points(float ratio, float offset) {
    if (!std::isfinite(ratio)) {
        return INT32_MIN; // x87 FISTP indefinite integer for infinity/NaN.
    }
    return ((number(ratio) + number(offset)) / number(0.5) + number(1)).integer();
}
std::int32_t market_sell_score(const World &world, std::size_t actor, std::size_t index, bool urgent) {
    const auto &stock = world.stocks[index];
    const auto &holding = world.players[actor].stocks[index];
    const auto price = quote(stock);
    const auto cost = basis(holding);
    const auto ratio =
        cost == 0 ? std::numeric_limits<float>::infinity() : (number(price) / number(cost)).single();
    float minimum = 10000;
    for (auto bits : world.stock_history_bits[index]) {
        const auto value = std::bit_cast<float>(bits);
        if ((bits & 0x7fffffffU) && minimum > value) {
            minimum = value;
        }
    }
    const auto long_average = history_average(world, index, 24);
    const auto short_average = history_average(world, index, 6);
    std::int32_t score = 0;
    if (double(ratio) > 1.6 && std::bit_cast<std::int32_t>(stock.float_bits[4]) < 0x3f800000) {
        score += 2;
    }
    const auto threshold = (number(minimum) * number(8)).single();
    if (price > threshold && less(number(cost) * number(1.25), number(threshold))) {
        score += 2;
    }
    if (long_average > short_average && price < scalar(stock, 1)) {
        score += 2;
    }
    if (ratio >= 2) {
        score = add(score, ratio_points(ratio, -2));
    }
    const auto available = liquidity(world.players[actor]);
    if (available < multiply(30000, world.header.price_index) && ratio > 0) {
        score = add(score, ratio_points(ratio, 0));
    }
    if (available < multiply(16000, world.header.price_index) && ratio > 0) {
        score = add(score, ratio_points(ratio, 0));
    }
    return urgent ? add(score, score) : score;
}

void validate(const World &world, std::size_t actor) {
    if (actor >= world.players.size() || world.stock_history_cursor < 0 ||
        world.stock_history_cursor >= 144) {
        throw std::invalid_argument("Stock AI actor or history cursor is outside original bounds");
    }
}
} // namespace

std::vector<Trade> buy(World &world, Random &random, std::size_t actor, bool market_closed) {
    validate(world, actor);
    auto &player = world.players[actor];
    if (random.next() % 3 != 0 || !player.status.stock_investment_ratio || market_closed) {
        return {};
    }
    if (player.status.loan_due_date && days_until_loan(world, player) < 15) {
        return {};
    }
    // Enlarged stock universes cannot be evaluated through the original 32-bit
    // AI budget (which silently wraps above ~2 billion). Preserve original AI
    // precisely for legacy-sized games; use a bounded 64-bit trade planner when
    // any quota or cash exceeds the legacy domain.
    if (player.state.bank_balance > INT32_MAX ||
        std::any_of(world.stocks.begin(), world.stocks.end(),
                    [](const auto &stock) { return stock.flags_10 > UINT16_MAX; })) {
        const auto available = std::max<std::int64_t>(0, player.state.bank_balance);
        auto budget = static_cast<std::int64_t>((__int128(available) * player.status.stock_investment_ratio) / 100);
        budget = std::min(budget, available);
        // Do not double-spend: purchases debit the bank immediately. Invest a
        // bounded slice per decision, keeping cash available for ordinary turns.
        budget = budget / 3;
        for (std::size_t i = 0; i < world.stocks.size(); ++i) {
            const auto &stock = world.stocks[i];
            if (stock.suspension_days || !stock.flags_10 || !stock.shares_available ||
                stocks::trend(world, i) == stocks::PriceTrend::limit_up)
                continue;
            const auto price = std::bit_cast<float>(stock.float_bits[2]);
            if (!std::isfinite(price) || price < 1 || price > mod::max_stock_price)
                continue;
            const auto room = mod::max_holding_shares - player.stocks[i].amount;
            if (room <= 0)
                continue;
            const auto affordable = static_cast<std::int64_t>(std::floor(static_cast<long double>(budget) / price));
            const auto quantity = std::min<std::int64_t>({affordable, stock.flags_10,
                                           stock.shares_available, room, INT32_MAX});
            if (quantity <= 0)
                continue;
            try {
                const auto trade = stocks::buy_stock(world, actor, i, static_cast<std::int32_t>(quantity), true);
                return {{i, static_cast<std::int32_t>(quantity), trade.cost,
                         trade.commercial_owner_changed}};
            } catch (const std::invalid_argument &) {
                continue; // insufficient for rounding of final share
            }
        }
        return {};
    }
    const auto value = holdings_value(world, actor);
    auto budget = multiply(add(value, liquidity(player)), player.status.stock_investment_ratio) / 100;
    if (value >= budget) {
        return {};
    }
    budget = mod::display_i32(std::min<std::int64_t>(subtract(budget, value), player.state.bank_balance));
    std::array<RatedStock, 12> candidates{};
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        auto &candidate = candidates[i];
        candidate.index = i;
        const auto &stock = world.stocks[i];
        if (stock.suspension_days || stocks::trend(world, i) == stocks::PriceTrend::limit_up ||
            !stock.flags_10) {
            continue;
        }
        if (stock.flags_4) {
            if (player.state.bank_balance > multiply(20000, world.header.price_index)) {
                candidate.score = company_buy_score(world, actor, i);
            }
        } else if (player.state.bank_balance > multiply(30000, world.header.price_index)) {
            candidate.score = market_buy_score(world, i);
        }
    }
    original_sort(candidates);
    for (std::size_t rank = 0; rank < candidates.size(); ++rank) {
        const auto candidate = candidates[rank];
        if (!candidate.score || random.next() % 24 > 12 - rank) {
            continue;
        }
        const auto &stock = world.stocks[candidate.index];
        const auto quantity = static_cast<std::int32_t>(std::min<std::int64_t>({
            (number(budget) / number(quote(stock))).integer(),
            std::int64_t(stock.flags_10), mod::max_holding_shares - player.stocks[candidate.index].amount}));
        if (!quantity) {
            return {};
        }
        const auto result = stocks::buy_stock(world, actor, candidate.index, quantity, true);
        return {{candidate.index, quantity, result.cost, result.commercial_owner_changed}};
    }
    return {};
}

std::vector<Trade> sell(World &world, Random &random, std::size_t actor, bool market_closed) {
    validate(world, actor);
    auto &player = world.players[actor];
    const bool urgent = days_until_loan(world, player) <= 6 && liquidity(player) < player.state.loan;
    if ((!urgent && random.next() % 3 != 0) || market_closed) {
        return {};
    }
    std::vector<Trade> trades;
    for (;;) {
        std::int32_t highest_score = 0;
        std::size_t selected = world.stocks.size();
        for (std::size_t i = 0; i < world.stocks.size(); ++i) {
            const auto &stock = world.stocks[i];
            if (!player.stocks[i].amount || stock.suspension_days ||
                stocks::trend(world, i) == stocks::PriceTrend::limit_down) {
                continue;
            }
            const auto score = stock.flags_4 ? company_sell_score(world, actor, i, urgent)
                                             : market_sell_score(world, actor, i, urgent);
            if (score > highest_score) {
                highest_score = score;
                selected = i;
            }
        }
        if (selected == world.stocks.size()) {
            break;
        }
        const auto quantity = player.stocks[selected].amount;
        const auto result = stocks::sell_stock(world, actor, selected, quantity, true);
        trades.push_back({selected, quantity, result.proceeds, result.commercial_owner_changed});
        if (!urgent || !less(number(liquidity(player)), number(player.state.loan) * number(1.1))) {
            break;
        }
    }
    return trades;
}
} // namespace rich4::stocks_ai
