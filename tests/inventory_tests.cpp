// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/inventory.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using namespace rich4;

void require(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}

template <typename Function> void invalid(Function &&function) {
    try {
        function();
    } catch (const std::invalid_argument &) {
        return;
    }
    throw std::runtime_error("Expected invalid inventory input rejection");
}

void card_rules() {
    CardSlots cards{};
    CardSupply supply{};
    CardPrices prices{};
    prices.fill(17);
    require(card_count(cards) == 0 && has_card(cards, 0) && !has_card(cards, 1) &&
                cheapest_card(cards, prices) == 0 && !drop_half_cards(cards, supply),
            "empty hand");
    require(!consume_card(cards, supply, 30), "missing card consume has no effect");
    cards = {5, 2, 5, 7};
    require(cheapest_card(cards, prices) == 5, "equal price keeps first slot, not smallest ID");
    supply[4] = 255;
    require(consume_card(cards, supply, 5) && cards == CardSlots{2, 5, 7} && supply[4] == 0,
            "consume removes first duplicate, compacts, wraps supply");
    const auto before_missing = cards;
    const auto supply_before_missing = supply;
    require(!consume_card(cards, supply, 30) && cards == before_missing && supply == supply_before_missing,
            "missing consume preserves bytes");
    cards = {1, 0, 3, 0, 5};
    receive_card(cards, supply, prices, 30);
    require(cards == CardSlots{1, 30, 3, 0, 5} && supply[29] == 255,
            "receive fills first gap and allows zero supply to wrap");
    cards = {};
    for (std::size_t i = 0; i < cards.size(); ++i)
        cards[i] = static_cast<std::uint8_t>(i + 1);
    prices.fill(100);
    prices[2] = prices[3] = 0;
    supply.fill(0);
    require(card_count(cards) == 15 && !has_card(cards, 0), "full hand");
    receive_card(cards, supply, prices, 30);
    require(cards == CardSlots{1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 30} && supply[2] == 1 &&
                supply[29] == 255,
            "full hand discards first cheapest, then appends");
    cards.fill(7);
    supply.fill(123);
    receive_card(cards, supply, prices, 7);
    require(std::all_of(cards.begin(), cards.end(), [](auto c) { return c == 7; }) && supply[6] == 123,
            "replace same card returns then takes supply");
    cards = {1, 2, 3, 4};
    supply.fill(0);
    require(drop_half_cards(cards, supply) && cards == CardSlots{2, 4} && supply[0] == 1 && supply[2] == 1,
            "ABCD drops A then C");
    cards = {1, 2, 3, 4, 5};
    require(drop_half_cards(cards, supply) && cards == CardSlots{2, 4, 5}, "odd count uses floor half");
    cards = {1, 2, 2, 3, 4, 5};
    require(drop_half_cards(cards, supply) && cards == CardSlots{2, 3, 5},
            "drop selects live slot, consume still removes first matching duplicate");
    cards = {1};
    require(!drop_half_cards(cards, supply) && cards == CardSlots{1}, "single card stays");
    cards.fill(30);
    prices.fill(255);
    supply.fill(250);
    require(sell_all_cards(cards, supply, prices) == 3825 && cards == CardSlots{} && supply[29] == 9,
            "sell adds full byte prices and wraps returned supply");
}

void mod_stacked_cards() {
    CardSlots cards{3, 3, 5};
    CardSupply supply{};
    CardPrices prices{};
    prices.fill(20);
    enable_card_stacks(cards);
    require(cards.stacked && card_count(cards) == 2 && card_total_quantity(cards) == 3 &&
                card_quantity(cards, 3) == 2 && cards[0] == 3 && cards[1] == 5,
            "old duplicate cards must compact into distinct MOD stacks");
    supply[2] = 250;
    for (int n = 0; n < 997; ++n)
        receive_card(cards, supply, prices, 3);
    require(card_quantity(cards, 3) == 999 && card_count(cards) == 2 &&
                !card_can_receive(cards, 3), "MOD card stack reaches 999 without consuming slots");
    const auto inventory_before = cards;
    const auto supply_before = supply;
    receive_card(cards, supply, prices, 3);
    require(card_quantity(cards, 3) == 999 && cards.stacked_amounts == inventory_before.stacked_amounts &&
                supply == supply_before, "1000th card grant is an unchanged no-op");
    require(consume_card(cards, supply, 3) && card_quantity(cards, 3) == 998 &&
                cards[0] == 3 && card_count(cards) == 2,
            "using one stacked card must leave 998 and keep the type slot");
    require(sell_all_cards(cards, supply, prices) == 19980 &&
                card_count(cards) == 0 && card_total_quantity(cards) == 0,
            "selling all stacks totals every physical card and clears all counts");
}
void tool_rules() {
    ToolAmounts amounts{};
    ToolSupply supply{};
    ToolPrices prices{};
    require(!receive_tool(amounts, supply, 1), "limited tool needs supply");
    supply[0] = 1;
    require(receive_tool(amounts, supply, 1) && amounts[0] == 1 && supply[0] == 0, "tool supply transfer");
    amounts[0] = 998;
    supply[0] = 2;
    require(receive_tool(amounts, supply, 1) && amounts[0] == 999 && supply[0] == 1, "receive reaches MOD cap");
    require(!receive_tool(amounts, supply, 1) && supply[0] == 1, "MOD cap prevents supply charge");
    amounts[0] = 1000;
    require(!receive_tool(amounts, supply, 1) && amounts[0] == 1000,
            "out-of-range high count remains capped");
    const auto supply_before_unlimited = supply;
    require(receive_tool(amounts, supply, 13) && amounts[12] == 1 && supply == supply_before_unlimited,
            "tool types 9..13 have no shared supply");
    require(consume_tool(amounts, supply, 13) && amounts[12] == 0 && supply == supply_before_unlimited,
            "unlimited tool consume leaves shared supply unchanged");
    require(!consume_tool(amounts, supply, 13), "zero tools is a no-op");
    supply[0] = 255;
    require(consume_tool(amounts, supply, 1) && amounts[0] == 999 && supply[0] == 0,
            "tool supply byte wraps on return");
    amounts.fill(255);
    supply.fill(255);
    prices.fill(255);
    require(sell_stored_tools(amounts, supply, prices) == 845325, "sale uses full unsigned byte product");
    require(std::all_of(amounts.begin(), amounts.begin() + 13, [](auto value) { return value == 0; }) &&
                amounts[13] == 255 && amounts[14] == 255,
            "only first thirteen tool slots are sold");
    require(std::all_of(supply.begin(), supply.end(), [](auto value) { return value == 254; }),
            "bulk tool return wraps, without saturation");
}

void invalid_inputs() {
    CardSlots cards{1, 2};
    CardSupply supply{};
    CardPrices prices{};
    for (const auto id : {std::uint8_t{0}, std::uint8_t{31}, std::uint8_t{255}}) {
        invalid([&] { receive_card(cards, supply, prices, id); });
        invalid([&] { consume_card(cards, supply, id); });
        require(cards == CardSlots{1, 2} && supply == CardSupply{},
                "invalid ID cannot partially mutate state");
    }
    cards = {1, 255};
    invalid([&] { (void)cheapest_card(cards, prices); });
    invalid([&] { receive_card(cards, supply, prices, 2); });
    invalid([&] { (void)sell_all_cards(cards, supply, prices); });
    invalid([&] { (void)drop_half_cards(cards, supply); });
    require(cards == CardSlots{1, 255} && supply == CardSupply{}, "bad hand is rejected before any mutation");
    cards = {1, 0, 2, 3};
    invalid([&] { (void)drop_half_cards(cards, supply); });
    require(cards == CardSlots{1, 0, 2, 3} && supply == CardSupply{},
            "sparse drop-half is rejected atomically");
    ToolAmounts tools{};
    ToolSupply tools_supply{};
    for (const auto id : {std::uint8_t{0}, std::uint8_t{14}, std::uint8_t{255}}) {
        invalid([&] { receive_tool(tools, tools_supply, id); });
        invalid([&] { consume_tool(tools, tools_supply, id); });
        require(tools == ToolAmounts{} && tools_supply == ToolSupply{},
                "invalid tool ID leaves arrays unchanged");
    }
}

template <typename T, std::size_t Size> void read_array(std::istream &input, std::array<T, Size> &result) {
    for (auto &value : result) {
        unsigned parsed;
        require(static_cast<bool>(input >> parsed) && parsed <= 255, "invalid oracle byte");
        value = static_cast<T>(parsed);
    }
}

void compare_original(const char *path) {
    std::ifstream input(path);
    std::string signature;
    std::size_t count;
    require(static_cast<bool>(input >> signature >> count) && signature == "rich4-inventory-v1" &&
                count > 0 && count <= 1000000,
            "invalid original inventory oracle header");
    for (std::size_t case_index = 0; case_index < count; ++case_index) {
        int operation, argument, expected_result;
        require(static_cast<bool>(input >> operation >> argument >> expected_result) && argument >= 0 &&
                    argument <= 255,
                "truncated oracle case");
        CardPrices prices;
        ToolPrices tool_prices;
        CardSlots cards, expected_cards;
        CardSupply supply, expected_supply;
        ToolAmounts tools, expected_tools;
        ToolSupply tool_supply, expected_tool_supply;
        read_array(input, prices);
        read_array(input, tool_prices);
        read_array(input, cards);
        read_array(input, supply);
        read_array(input, tools);
        read_array(input, tool_supply);
        read_array(input, expected_cards);
        read_array(input, expected_supply);
        read_array(input, expected_tools);
        read_array(input, expected_tool_supply);
        const auto id = static_cast<std::uint8_t>(argument);
        int result = -1;
        switch (operation) {
        case 0:
            result = static_cast<int>(card_count(cards));
            break;
        case 1:
            result = cheapest_card(cards, prices);
            break;
        case 2:
            receive_card(cards, supply, prices, id);
            break;
        case 3:
            consume_card(cards, supply, id);
            break;
        case 4:
            result = has_card(cards, id);
            break;
        case 5:
            result = drop_half_cards(cards, supply);
            break;
        case 6:
            result = static_cast<int>(sell_all_cards(cards, supply, prices));
            break;
        case 7:
            receive_tool(tools, tool_supply, id);
            break;
        case 8:
            consume_tool(tools, tool_supply, id);
            break;
        case 9:
            result = static_cast<int>(sell_stored_tools(tools, tool_supply, tool_prices));
            break;
        default:
            throw std::runtime_error("Unknown oracle operation");
        }
        require(result == expected_result && cards == expected_cards && supply == expected_supply &&
                    tools == expected_tools && tool_supply == expected_tool_supply,
                "Original inventory mismatch at case " + std::to_string(case_index) + ", operation " +
                    std::to_string(operation));
    }
    input >> std::ws;
    require(input.eof(), "extra bytes after declared oracle cases");
    std::cout << "Original EXE inventory cases matched: " << count << '\n';
}

} // namespace

int main(int argc, char **argv) {
    try {
        card_rules();
        mod_stacked_cards();
        tool_rules();
        invalid_inputs();
        for (int i = 1; i < argc; ++i)
            compare_original(argv[i]);
        std::cout << "Inventory tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Inventory tests failed: " << error.what() << '\n';
        return 1;
    }
}
