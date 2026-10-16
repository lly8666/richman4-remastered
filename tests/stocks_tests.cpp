// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/random.hpp"
#include "rich4/stocks.hpp"
#include "rich4/world.hpp"

#include <bit>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {

void check(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::uint32_t bits(float value) {
    return std::bit_cast<std::uint32_t>(value);
}
float floating(std::uint32_t value) {
    return std::bit_cast<float>(value);
}

rich4::World fixture() {
    rich4::World world;
    world.players.resize(4);
    world.commercials.resize(2);
    world.stocks[0].flags_4 = 1;
    world.stocks[0].shares_available = 6600;
    world.stocks[0].flags_10 = 1500;
    world.stocks[0].float_bits[2] = bits(15.55F);
    world.public_fund = 2000;
    for (std::size_t i = 0; i < world.players.size(); ++i) {
        world.players[i].state.bank_balance = 10000;
        world.players[i].state.cash = 20000;
        world.players[i].stocks[0] = {1000 - static_cast<std::int32_t>(i) * 100, bits(21.25F)};
    }
    world.commercials[1].shareholder_order = {1, 2, 3, 4};
    world.commercials[1].owner_code = 1;
    return world;
}

std::uint32_t packed_order(const rich4::Commercial &commercial) {
    std::uint32_t result = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        result |= std::uint32_t(commercial.shareholder_order[i]) << (i * 8);
    }
    return result;
}

using Row = std::vector<std::uint32_t>;
std::vector<Row> read_csv(const std::filesystem::path &path, std::size_t columns) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot open " + path.string());
    }
    std::string line;
    std::getline(file, line); // Each oracle table carries a field-name header.
    std::vector<Row> rows;
    while (std::getline(file, line)) {
        Row row;
        std::istringstream fields(line);
        for (std::string field; std::getline(fields, field, ',');) {
            const auto value = std::stoull(field);
            check(value <= UINT32_MAX, "oracle integer out of range");
            row.push_back(static_cast<std::uint32_t>(value));
        }
        check(row.size() == columns, "oracle column count mismatch");
        rows.push_back(std::move(row));
    }
    check(!rows.empty(), "empty oracle table");
    return rows;
}

void unit_tests() {
    auto world = fixture();
    const auto sale = rich4::stocks::sell_stock(world, 0, 0, 1000, true);
    check(sale.proceeds == 15550, "sale value truncates");
    check(sale.commercial_owner_changed, "next shareholder acquires commercial");
    check(world.players[0].stocks[0].amount == 0 && world.players[0].stocks[0].raw_cost_basis == 0,
          "liquidation clears quantity and basis");
    check(world.players[0].state.bank_balance == 25550 && world.players[0].state.cash == 20000,
          "sale credits bank and preserves cash");
    check(world.public_fund == 2000, "bank sale preserves public fund");
    check(packed_order(world.commercials[1]) == 0x00040302, "liquidation removes shareholder");

    world = fixture();
    rich4::stocks::sell_stock(world, 0, 0, 100, false);
    check(world.public_fund == 3555 && world.players[0].state.bank_balance == 10000,
          "public sale credits public fund");
    check(world.players[0].stocks[0].raw_cost_basis == bits(21.25F), "partial sale preserves basis");
    check(world.commercials[1].owner_code == 2, "equal holding preserves incumbent after reinsertion");

    world = fixture();
    world.stocks[0].shares_available = 100000000;
    world.stocks[0].flags_10 = 100000000;
    world.players[0].state.bank_balance = INT64_MAX;
    world.stocks[0].float_bits[2] = bits(1.0F);
    bool rejected = false;
    try {
        rich4::stocks::sell_stock(world, 0, 0, 1, true);
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    check(rejected && world.stocks[0].shares_available == 100000000 &&
              world.players[0].state.bank_balance == INT64_MAX &&
              world.players[0].stocks[0].amount == 1000,
          "MOD must reject 64-bit balance and 100-million share cap before changing sale");
    rejected = false;
    try {
        rich4::stocks::sell_stock(world, 0, 0, 1001, true);
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    check(rejected && world.players[0].stocks[0].amount == 1000, "oversell fails before mutation");

    world = fixture();
    world.stocks[0].suspension_days = 5;
    world.stocks[0].news_movement = 0x11;
    world.stocks[0].float_bits[1] = bits(1000.0F);
    check(rich4::stocks::update_stocks(world, 1) == 1,
          "forced update ignores suspension and honors high nibble");
    check(world.stocks[0].float_bits[2] == bits(1100.0F), "forced increase reads previous price");
    check(world.stock_history_cursor == 0 && world.stock_history_bits[0][143] == bits(1100.0F),
          "history zero wraps to previous sample");
    check(world.stocks[0].news_movement == 0x11, "forced update preserves news flags");
    check(rich4::stocks::adjusted_price(1.0F, -10.0F) == 1.0F, "lower price clamp");
    check(rich4::stocks::adjusted_price(999999.0F, 10.0F) == 999999.0F, "MOD upper price clamp");

    // High-price boundary: > INT32_MAX proceeds are valid and exact in MOD.
    world = fixture();
    world.stocks[0].float_bits[2] = bits(999999.0F);
    world.players[0].stocks[0].amount = 3000;
    const auto large_sale = rich4::stocks::sell_stock(world, 0, 0, 3000, true);
    check(large_sale.proceeds == INT64_C(2999997000) &&
              world.players[0].state.bank_balance == INT64_C(3000007000) &&
              world.players[0].stocks[0].amount == 0,
          "MOD must credit high-price sales above signed32 without losing cents");

    // Multiply original market volume x10000: 50 million shares @999999
    // is 49,999,950,000,000, bank and net asset stay positive and exact.
    world = fixture();
    world.stocks[0].flags_4 = 0;
    world.stocks[0].shares_available = world.stocks[0].flags_10 = 100000000;
    world.stocks[0].float_bits[2] = bits(999999.0F);
    world.players[0].stocks[0].amount = 0;
    world.players[0].stocks[0].raw_cost_basis = 0;
    world.players[0].state.cash = 900000000;
    world.players[0].state.bank_balance = INT64_C(100000000000000);
    const auto before_wealth = rich4::stocks::player_wealth(world, 0);
    const auto huge_buy = rich4::stocks::buy_stock(world, 0, 0, 50000000, true);
    check(huge_buy.cost == INT64_C(49999950000000) &&
              world.players[0].state.bank_balance == INT64_C(50000050000000) &&
              world.players[0].stocks[0].amount == 50000000 &&
              world.stocks[0].shares_available == 50000000 &&
              rich4::stocks::player_wealth(world, 0) == before_wealth,
          "MOD 50-million share buy must conserve exact net assets without wraps");
    const auto huge_sell = rich4::stocks::sell_stock(world, 0, 0, 50000000, true);
    check(huge_sell.proceeds == huge_buy.cost &&
              world.players[0].state.bank_balance == INT64_C(100000000000000) &&
              world.stocks[0].shares_available == 100000000 &&
              world.players[0].stocks[0].amount == 0 &&
              rich4::stocks::player_wealth(world, 0) == before_wealth,
          "MOD high-volume stock sell must restore the exact cash and share supply");

    world = fixture();
    const auto purchase = rich4::stocks::buy_stock(world, 3, 0, 500, true);
    check(purchase.cost == 7775 && purchase.commercial_owner_changed,
          "market purchase changes controlling shareholder");
    check(world.players[3].state.bank_balance == 2225 && world.players[3].state.cash == 20000,
          "market purchase uses bank");
    check(world.players[3].stocks[0].amount == 1200 && world.commercials[1].owner_code == 4,
          "market purchase quantity and owner");
    world = fixture();
    world.commercials[1].valuation = 155599;
    world.commercials[1].shares_remaining = 5000;
    const auto primary = rich4::stocks::buy_stock(world, 3, 0, 500, false);
    check(primary.cost == 7500, "primary purchase truncates unit valuation before multiplying");
    check(world.players[3].state.cash == 12500 && world.players[3].state.bank_balance == 10000,
          "primary purchase uses cash");
    check(world.commercials[1].shares_remaining == 4500 && world.stocks[0].shares_available == 6600,
          "primary purchase preserves market shares");
    rich4::Random random(123);
    check(!rich4::stocks::daily_update(world, random, true) && random.state() == 123,
          "holiday preserves random stream");
    world.news_effect_days = 0x80;
    check(!rich4::stocks::daily_update(world, random, false) && random.state() == 123,
          "news freeze preserves random stream");
}

void compare_oracle(const std::filesystem::path &directory) {
    const auto prices = read_csv(directory / "prices.csv", 3);
    std::size_t failures = 0;
    for (const auto &row : prices) {
        const auto actual = bits(rich4::stocks::adjusted_price(floating(row[0]), floating(row[1])));
        if (actual != row[2]) {
            if (failures++ < 10) {
                std::cerr << "price " << floating(row[0]) << " percent " << floating(row[1])
                          << " expected bits " << row[2] << " actual " << actual << '\n';
            }
        }
    }
    std::cout << "Original prices: " << prices.size() << ", mismatches: " << failures << '\n';
    check(failures == 0, "original price result differs");

    const auto sales = read_csv(directory / "sales.csv", 19);
    for (const auto &row : sales) {
        auto world = fixture();
        const auto player = row[0];
        world.stocks[0].float_bits[2] = row[3];
        for (std::size_t i = 0; i < 4; ++i) {
            world.players[i].stocks[0].amount = static_cast<std::int32_t>(row[4 + i]);
            world.commercials[1].shareholder_order[i] = static_cast<std::uint8_t>(row[8] >> (8 * i));
        }
        world.commercials[1].owner_code = static_cast<std::uint8_t>(row[9]);
        const auto result =
            rich4::stocks::sell_stock(world, player, 0, static_cast<std::int32_t>(row[1]), row[2] != 0);
        check(std::bit_cast<std::uint32_t>(result.proceeds) == row[10], "original sale proceeds differs");
        check(std::bit_cast<std::uint32_t>(world.players[player].state.bank_balance) == row[11],
              "original sale bank differs");
        check(std::bit_cast<std::uint32_t>(world.public_fund) == row[12], "original sale fund differs");
        check(std::uint32_t(world.players[player].stocks[0].amount) == row[13],
              "original sale quantity differs");
        check(world.players[player].stocks[0].raw_cost_basis == row[14], "original sale basis differs");
        check(world.stocks[0].shares_available == row[15] && world.stocks[0].flags_10 == row[16],
              "original sale counters differ");
        check(packed_order(world.commercials[1]) == row[17] && world.commercials[1].owner_code == row[18],
              "original shareholder order differs");
        check(result.commercial_owner_changed == (row[9] != row[18]), "owner change result differs");
    }
    const auto updates = read_csv(directory / "updates.csv", 8);
    check(updates.size() % 12 == 0, "truncated update group");
    for (std::size_t group = 0; group < updates.size(); group += 12) {
        auto world = fixture();
        world.stock_history_cursor = static_cast<std::int32_t>(updates[group][0]);
        const auto selection = updates[group][1];
        std::size_t changed = 0;
        for (std::size_t index = 0; index < 12; ++index) {
            const auto &row = updates[group + index];
            check(row[2] == index && row[0] == updates[group][0] && row[1] == selection,
                  "inconsistent update group");
            world.stocks[index].news_movement = static_cast<std::uint8_t>(row[3]);
            world.stocks[index].float_bits[1] = row[4];
            world.stocks[index].float_bits[2] = bits(123.0F);
            changed += row[3] != 0 && (selection == 0 || selection == index + 1);
        }
        check(rich4::stocks::update_stocks(world, selection) == changed,
              "original forced update count differs");
        check(world.stock_history_cursor == static_cast<std::int32_t>(updates[group][0]),
              "forced update advances cursor");
        const auto sample = world.stock_history_cursor == 0 ? 143 : world.stock_history_cursor - 1;
        for (std::size_t index = 0; index < 12; ++index) {
            const auto &row = updates[group + index];
            check(world.stocks[index].float_bits[2] == row[5], "original forced quote differs");
            check(world.stocks[index].float_bits[4] == row[6], "original forced movement differs");
            for (std::size_t history = 0; history < 144; ++history) {
                const auto expected = history == static_cast<std::size_t>(sample) ? row[7] : 0;
                check(world.stock_history_bits[index][history] == expected,
                      "original forced history differs");
            }
        }
    }
    std::cout << "Original sales: " << sales.size() << "; update fields: " << updates.size() << '\n';
    if (std::filesystem::exists(directory / "wealth.csv")) {
        const auto wealth = read_csv(directory / "wealth.csv", 30);
        for (const auto &row : wealth) {
            auto world = fixture();
            const auto player = row[1];
            auto &account = world.players[player];
            account.state.cash = std::bit_cast<std::int32_t>(row[2]);
            account.state.bank_balance = std::bit_cast<std::int32_t>(row[3]);
            account.state.loan = std::bit_cast<std::int32_t>(row[4]);
            for (std::size_t index = 0; index < 12; ++index) {
                account.stocks[index].amount = static_cast<std::int32_t>(row[6 + index]);
                world.stocks[index].float_bits[2] = row[18 + index];
            }
            world.lands.resize(5);
            world.facilities.resize(4);
            for (std::size_t index = 1; index < 5; ++index) {
                auto &land = world.lands[index];
                land.economy.chain_store = static_cast<std::uint8_t>(index % 2);
                land.economy.owner_code = static_cast<std::uint8_t>(1 + ((player + index - 1) % 4));
                land.economy.level = static_cast<std::uint8_t>(index + 1);
                land.land_price = static_cast<std::uint16_t>(1234 * index);
                land.price_per_level = static_cast<std::uint16_t>(234 * index);
            }
            for (std::size_t index = 1; index < 4; ++index) {
                auto &facility = world.facilities[index];
                facility.owner_code = static_cast<std::uint8_t>(1 + ((player + index - 1) % 4));
                facility.level = static_cast<std::uint8_t>(index + 2);
                facility.land_price = static_cast<std::uint16_t>(4321 * index);
                facility.price_per_level() = static_cast<std::uint16_t>(321 * index);
            }
            const auto actual = std::bit_cast<std::uint32_t>(rich4::stocks::player_wealth(world, player));
            if (actual != row[5]) {
                std::cerr << "wealth case " << row[0] << " expected " << row[5] << " actual " << actual
                          << '\n';
                throw std::runtime_error("original wealth differs");
            }
        }
        std::cout << "Original wealth cases: " << wealth.size() << '\n';
    }
    if (std::filesystem::exists(directory / "limits.csv")) {
        const auto limits = read_csv(directory / "limits.csv", 6);
        check(limits.size() % 12 == 0, "truncated quota group");
        for (std::size_t group = 0; group < limits.size(); group += 12) {
            auto world = fixture();
            rich4::Random random(limits[group][1]);
            for (std::size_t index = 0; index < 12; ++index) {
                check(limits[group + index][2] == index, "quota index order");
                world.stocks[index].shares_available = static_cast<std::uint16_t>(limits[group + index][3]);
            }
            rich4::stocks::refresh_trade_limits(world, random);
            for (std::size_t index = 0; index < 12; ++index) {
                check(world.stocks[index].flags_10 == limits[group + index][4], "original quota differs");
                check(random.state() == limits[group + index][5], "original quota RNG differs");
            }
        }
        std::cout << "Original trade-limit records: " << limits.size() << '\n';
    }
    if (std::filesystem::exists(directory / "daily.csv")) {
        const auto daily = read_csv(directory / "daily.csv", 24);
        check(daily.size() % 12 == 0, "truncated daily group");
        for (std::size_t group = 0; group < daily.size(); group += 12) {
            auto world = fixture();
            world.commercials.resize(13);
            rich4::Random random(daily[group][1]);
            world.news_effect_days = static_cast<std::int32_t>(daily[group][2]);
            const auto holiday = daily[group][3] != 0;
            world.stock_history_cursor = static_cast<std::int32_t>(daily[group][4]);
            world.stock_market_index = 12345;
            world.stock_market_movement_bits = bits(2.25F);
            for (std::size_t index = 0; index < 12; ++index) {
                const auto &row = daily[group + index];
                check(row[5] == index && row[0] == daily[group][0], "daily index order");
                auto &stock = world.stocks[index];
                stock.flags_4 = static_cast<std::uint16_t>(row[6]);
                stock.suspension_days = static_cast<std::uint8_t>(row[7]);
                stock.news_movement = static_cast<std::uint8_t>(row[8]);
                stock.float_bits = {row[9], bits(99.0F), row[10], row[11], row[12]};
                stock.raw_32 = row[13];
                world.commercials[stock.flags_4].valuation = static_cast<std::int32_t>(row[14]);
            }
            check(rich4::stocks::daily_update(world, random, holiday) ==
                      (!holiday && world.news_effect_days == 0),
                  "daily closed/open result differs");
            for (std::size_t index = 0; index < 12; ++index) {
                const auto &row = daily[group + index];
                const auto &stock = world.stocks[index];
                const std::array<std::uint32_t, 9> actual{
                    stock.float_bits[1],
                    stock.float_bits[2],
                    stock.float_bits[4],
                    stock.raw_32,
                    world.stock_history_bits[index][row[4]],
                    std::bit_cast<std::uint32_t>(world.stock_market_index),
                    world.stock_market_movement_bits,
                    std::uint32_t(world.stock_history_cursor),
                    random.state()};
                for (std::size_t field = 0; field < actual.size(); ++field) {
                    if (actual[field] != row[15 + field]) {
                        std::cerr << "daily case " << row[0] << " stock " << index << " field " << field
                                  << " expected " << row[15 + field] << " actual " << actual[field] << '\n';
                        throw std::runtime_error("original daily state differs");
                    }
                }
                check(stock.suspension_days == row[7] && stock.news_movement == row[8],
                      "daily changes caller-owned counters");
            }
        }
        std::cout << "Original daily records: " << daily.size() << '\n';
    }
    if (std::filesystem::exists(directory / "purchases.csv")) {
        const auto purchases = read_csv(directory / "purchases.csv", 16);
        for (const auto &row : purchases) {
            auto world = fixture();
            const auto player = row[1];
            for (auto &account : world.players) {
                account.state.cash = account.state.bank_balance = 100000000;
                account.stocks[0].raw_cost_basis = row[5];
            }
            world.stocks[0].float_bits[2] = row[4];
            world.stocks[0].shares_available = world.stocks[0].flags_10 = 5000;
            world.commercials[1].valuation = static_cast<std::int32_t>(row[6]);
            world.commercials[1].shares_remaining = 5000;
            const auto result =
                rich4::stocks::buy_stock(world, player, 0, static_cast<std::int32_t>(row[2]), row[3] != 0);
            const auto &account = world.players[player];
            const std::array<std::uint32_t, 9> actual{
                std::uint32_t(account.stocks[0].amount),
                account.stocks[0].raw_cost_basis,
                world.stocks[0].shares_available,
                world.stocks[0].flags_10,
                std::bit_cast<std::uint32_t>(account.state.cash),
                std::bit_cast<std::uint32_t>(account.state.bank_balance),
                std::uint32_t(world.commercials[1].shares_remaining),
                packed_order(world.commercials[1]),
                world.commercials[1].owner_code};
            for (std::size_t field = 0; field < actual.size(); ++field) {
                if (actual[field] != row[7 + field]) {
                    std::cerr << "purchase case " << row[0] << " field " << field << " expected "
                              << row[7 + field] << " actual " << actual[field] << '\n';
                    throw std::runtime_error("original purchase state differs");
                }
            }
            check(result.cost == 100000000 - static_cast<std::int32_t>(row[row[3] ? 12 : 11]),
                  "purchase cost differs");
        }
        std::cout << "Original purchase cases: " << purchases.size() << '\n';
    }
    if (std::filesystem::exists(directory / "trends.csv")) {
        const auto trends = read_csv(directory / "trends.csv", 3);
        auto world = fixture();
        for (const auto &row : trends) {
            world.stocks[0].float_bits[1] = row[0];
            world.stocks[0].float_bits[2] = row[1];
            check(static_cast<std::uint8_t>(rich4::stocks::trend(world, 0)) == row[2],
                  "original price trend differs");
        }
        std::cout << "Original price-trend cases: " << trends.size() << '\n';
    }
}

} // namespace

int main(int argc, char **argv) {
    try {
        unit_tests();
        if (argc == 2) {
            compare_oracle(argv[1]);
        } else if (argc != 1) {
            throw std::runtime_error("usage: stocks_tests [original-stocks-golden-directory]");
        }
        std::cout << "Stocks tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
