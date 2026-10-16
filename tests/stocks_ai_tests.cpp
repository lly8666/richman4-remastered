// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/calendar.hpp"
#include "rich4/random.hpp"
#include "rich4/stocks_ai.hpp"
#include "rich4/world.hpp"

#include <algorithm>
#include <bit>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace rich4;
namespace {
void require(bool condition, const std::string &message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
std::uint32_t bits(float value) {
    return std::bit_cast<std::uint32_t>(value);
}
World fixture(unsigned scenario, unsigned mode) {
    World world;
    world.players.resize(4);
    world.commercials.resize(5);
    world.header.date = {static_cast<std::uint8_t>(1 + scenario % 28), 4, 1998};
    world.header.price_index = 1 + int(scenario % 4);
    world.month_counter = scenario % 4;
    world.stock_history_cursor = int(scenario % 144);
    for (unsigned p = 0; p < 4; ++p) {
        auto &player = world.players[p];
        player.state.controller_flags = 2;
        player.state.cash = 10000 + int(scenario * 17);
        player.state.bank_balance = 1000000 + int(scenario * 103);
        player.status.stock_investment_ratio = static_cast<std::uint8_t>(scenario % 13 == 0 ? 0 : 80);
        if (scenario % 7 != 0) {
            const auto date = calendar::Date{1998, 4, world.header.date.day};
            player.status.loan_due_date =
                calendar::pack(calendar::add_days(date, scenario % 3 == 0 ? 5 : 30));
        }
        if (mode == 1 && scenario % 4 == 0) {
            player.state.bank_balance = 20000;
            player.state.loan = 300000;
            player.status.loan_due_date = calendar::pack({1998, 4, world.header.date.day});
        }
    }
    for (unsigned i = 0; i < 12; ++i) {
        auto &stock = world.stocks[i];
        stock.flags_4 = static_cast<std::uint16_t>(i < 4 ? i + 1 : 0);
        stock.flags_10 = static_cast<std::uint16_t>(100 + (scenario * 37 + i * 13) % 1000);
        stock.suspension_days = scenario % 17 == i ? 2 : 0;
        float price = 20 + float((scenario * 7 + i * 11) % 201) / 2;
        float previous = price + float(int(scenario % 3) - 1);
        if (scenario % 11 == 0 && i == scenario % 12) {
            previous = 100;
            price = mode == 0 ? 110 : 90;
        }
        stock.float_bits = {bits(100), bits(previous), bits(price), bits(0),
                            bits(float(int((scenario + i) % 15) - 7))};
        int total = 0;
        for (unsigned p = 0; p < 4; ++p) {
            const int amount = int((i + p + scenario) % 5) * 100;
            total += amount;
            world.players[p].stocks[i] = {amount, bits(20 + float((scenario * 7 + i * 11) % 101) / 2)};
        }
        stock.shares_available = static_cast<std::uint16_t>(9000 - total);
        for (unsigned j = 0; j < 144; ++j) {
            world.stock_history_bits[i][j] =
                j % 19 == 0 ? 0 : bits(float(15 + i * 5 + (j % 24) * (scenario % 5 + 1)));
        }
        if (i < 4) {
            auto &company = world.commercials[i + 1];
            company.id = i + 1;
            company.stock_index = static_cast<std::uint8_t>(i);
            company.valuation = int(200000 + (scenario * 19 + i * 7) % 100 * 10000);
            company.funds = int((scenario * 29 + i * 13) % 81) * 1000 - 40000;
            company.funds_change = int((scenario * 13 + i * 31) % 61) * 1000 - 20000;
            company.shares_remaining = 1000;
            std::array<unsigned, 4> order{0, 1, 2, 3};
            std::stable_sort(order.begin(), order.end(), [&](unsigned a, unsigned b) {
                return world.players[a].stocks[i].amount > world.players[b].stocks[i].amount;
            });
            unsigned cursor = 0;
            for (const auto p : order) {
                if (world.players[p].stocks[i].amount) {
                    company.shareholder_order[cursor++] = static_cast<std::uint8_t>(p + 1);
                }
            }
            company.owner_code = company.shareholder_order[0];
        }
    }
    return world;
}
std::vector<std::uint32_t> result_words(const World &world, const Random &random,
                                        const std::vector<stocks_ai::Trade> &trades) {
    std::vector<std::uint32_t> result{random.state(), static_cast<std::uint32_t>(trades.size())};
    for (std::size_t i = 0; i < 12; ++i) {
        result.push_back(i < trades.size() ? static_cast<std::uint32_t>(trades[i].stock + 1) : 0);
        result.push_back(i < trades.size() ? std::uint32_t(trades[i].quantity) : 0);
    }
    result.push_back(std::uint32_t(world.players[0].state.cash));
    result.push_back(std::uint32_t(world.players[0].state.bank_balance));
    for (std::size_t i = 0; i < 12; ++i) {
        const auto &holding = world.players[0].stocks[i];
        result.insert(result.end(), {std::uint32_t(holding.amount), holding.raw_cost_basis,
                                     world.stocks[i].shares_available, world.stocks[i].flags_10});
    }
    for (std::size_t i = 1; i <= 4; ++i) {
        const auto &company = world.commercials[i];
        std::uint32_t order = 0;
        for (unsigned j = 0; j < 4; ++j) {
            order |= std::uint32_t(company.shareholder_order[j]) << (j * 8);
        }
        result.push_back(company.owner_code);
        result.push_back(order);
    }
    return result;
}
void unit_tests() {
    auto world = fixture(1, 0);
    Random actual(9), expected(9);
    (void)expected.next();
    require(stocks_ai::buy(world, actual, 0, true).empty() && actual.state() == expected.state(),
            "A closed buy visit still consumes its initial random gate");
    world = fixture(4, 1);
    actual.seed(9);
    require(stocks_ai::sell(world, actual, 0, true).empty() && actual.state() == 9,
            "An urgent sale bypasses the random gate even when the exchange is closed");
    world = fixture(1, 1);
    actual.seed(9);
    require(stocks_ai::sell(world, actual, 0, true).empty() && actual.state() == expected.state(),
            "A normal closed sale retains the initial draw");
}
void mod_wide_portfolio_with_narrow_quota() {
    auto w = fixture(1, 0);
    w.players[0].state.bank_balance = 1'000'000'000;
    w.players[0].state.cash = 0;
    w.players[0].status.stock_investment_ratio = 80;
    w.players[0].status.loan_due_date = 0;
    w.players[0].stocks[0].amount = 50000;
    for (auto &stock : w.stocks) {
        stock.flags_10 = 100;
        stock.shares_available = 1'000'000;
        stock.float_bits[2] = bits(999999.F);
        stock.float_bits[1] = bits(999999.F);
    }
    w.stocks[0].flags_4 = 0;
    Random random(0);
    const auto old_loan = w.players[0].state.loan;
    const auto before_bank = w.players[0].state.bank_balance;
    const auto before_holdings = w.players[0].stocks[0].amount;
    (void)stocks_ai::buy(w, random, 0, false);
    require(w.players[0].state.loan == old_loan &&
                w.players[0].state.bank_balance >= 0 &&
                w.players[0].state.bank_balance <= before_bank &&
                w.players[0].stocks[0].amount >= before_holdings,
            "small current quota must not route 50b portfolio through 32-bit AI budget");
}
void golden(const char *path) {
    std::ifstream file(path);
    require(bool(file), "Cannot open stock AI oracle");
    std::string line;
    std::getline(file, line);
    unsigned cases = 0, purchases = 0, sales = 0, repeated = 0;
    while (std::getline(file, line)) {
        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream row(line);
        unsigned scenario, mode, seed, closed;
        require(bool(row >> scenario >> mode >> seed >> closed), "Malformed stock AI oracle row");
        auto world = fixture(scenario, mode);
        Random random(seed);
        const auto trades = mode == 0 ? stocks_ai::buy(world, random, 0, closed != 0)
                                      : stocks_ai::sell(world, random, 0, closed != 0);
        const auto words = result_words(world, random, trades);
        for (std::size_t i = 0; i < words.size(); ++i) {
            std::uint32_t expected;
            require(bool(row >> expected), "Truncated stock AI oracle row");
            if (words[i] != expected) {
                throw std::runtime_error("Stock AI mismatch scenario=" + std::to_string(scenario) +
                                         " mode=" + std::to_string(mode) + " word=" + std::to_string(i) +
                                         " actual=" + std::to_string(words[i]) +
                                         " original=" + std::to_string(expected));
            }
        }
        purchases += mode == 0 ? static_cast<unsigned>(trades.size()) : 0;
        sales += mode == 1 ? static_cast<unsigned>(trades.size()) : 0;
        repeated += trades.size() > 1 ? 1U : 0U;
        ++cases;
    }
    require(cases > 0, "Empty stock AI oracle");
    std::cout << cases << " original stock AI visits matched; " << purchases << " purchases, " << sales
              << " sales, " << repeated << " repeated-sale visits\n";
}
} // namespace
int main(int argc, char **argv) {
    try {
        unit_tests();
        mod_wide_portfolio_with_narrow_quota();
        if (argc > 1) {
            golden(argv[1]);
        }
        std::cout << "Stock AI random-gate/closure tests passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
