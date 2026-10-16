// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/services.hpp"
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace {
using namespace rich4;
void require(bool result, const char *message) {
    if (!result)
        throw std::runtime_error(message);
}
World sample() {
    World world;
    world.players.resize(4);
    world.nodes.resize(5);
    world.lands.resize(3);
    world.facilities.resize(2);
    world.commercials.resize(2);
    world.header.price_index = 2;
    for (std::size_t slot = 0; slot < 4; ++slot) {
        auto &p = world.players[slot].state;
        p.slot = slot;
        p.controller_flags = slot ? 2 : 1;
        p.cash = 100;
        p.bank_balance = 100;
        p.points = 100;
        p.node_id = static_cast<std::uint16_t>(slot + 1);
        p.dice_count = 1;
    }
    world.card_supply.fill(3);
    world.tool_supply.fill(10);
    return world;
}
void weighted_and_shelf() {
    auto w = sample();
    w.card_supply.fill(0);
    w.card_supply[2] = 2;
    w.card_supply[29] = 1;
    Random actual(12), expected(12);
    auto n = expected.next() % 3;
    const auto card = services::receive_random_card(w, actual, 0);
    require(card == (n < 2 ? 3 : 30), "weighted ascending selection");
    require(actual.state() == expected.state(), "gift random count");
    require(w.players[0].state.cards[0] == card && w.card_supply[card - 1] == (card == 3 ? 1 : 0),
            "gift supply transfer");
    w.card_supply.fill(0);
    const auto seed = actual.state();
    require(services::receive_random_card(w, actual, 0) == 0 && actual.state() == seed,
            "empty gift must not draw random");
    for (std::uint32_t seed2 = 0; seed2 < 100; ++seed2) {
        w = sample();
        Random random(seed2), reference(seed2);
        const auto before = w.card_supply;
        auto shop = services::open_shop(w, random, 0);
        const auto count = reference.next() % 10 + 6;
        require(shop.cards.size() == count && shop.tools.size() == 8, "original shelf count");
        require(w.card_supply == before, "shelf should not reserve global supply");
        std::array<unsigned, 30> counts{};
        for (const auto id : shop.cards)
            ++counts[id - 1];
        for (std::size_t i = 0; i < counts.size(); ++i)
            require(counts[i] <= before[i], "shelf exceeds temporary stock");
        for (unsigned i = 0; i < count; ++i)
            (void)reference.next();
        require(random.state() == reference.state(), "shelf random count");
    }
}
void transactions() {
    auto w = sample();
    services::Shop shop;
    shop.cards = {3, 3};
    shop.tools = {1, 5};
    require(services::buy_card(w, shop, 0, 3), "card buy");
    require(w.players[0].state.points == 65 && shop.revenue == 350 && shop.cards.size() == 1 &&
                w.card_supply[2] == 2,
            "card buy economics");
    require(services::sell_card(w, shop, 0, 3), "card sell");
    require(w.players[0].state.points == 96 && shop.revenue == 385 && w.card_supply[2] == 3,
            "90 percent truncated sale");
    require(!services::sell_card(w, shop, 0, 3) && !services::buy_card(w, shop, 0, 1),
            "unowned/unoffered card rejected");
    require(services::buy_tool(w, shop, 0, 5), "tool buy");
    require(w.players[0].state.points == 16 && w.players[0].state.tool_amounts[4] == 1 &&
                w.tool_supply[4] == 9,
            "tool transfer");
    require(!services::buy_tool(w, shop, 0, 5), "overspending rejected");
    require(services::sell_tool(w, shop, 0, 5) && w.players[0].state.points == 88, "tool sale");
    services::close_shop(w, shop, 6001);
    const auto funds = w.commercials[1].funds;
    require(funds == shop.revenue && w.commercials[1].funds_change == funds, "commercial income");
    services::close_shop(w, shop, 6001);
    require(w.commercials[1].funds == funds && !services::buy_tool(w, shop, 0, 1),
            "closed shop cannot duplicate payment");
    w = sample();
    shop = {};
    shop.tools = {1};
    w.players[0].state.tool_amounts[0] = 999;
    require(!services::buy_tool(w, shop, 0, 1), "MOD 999 tool cap");
    w.players[0].state.tool_amounts[0] = 2;
    require(services::sell_tool(w, shop, 0, 1, 2) && w.players[0].state.points == 127,
            "quantity rounded once");
    // The original shared pool reaches zero after ~8 purchases; MOD shops
    // must continue selling until the personal per-tool limit of 999.
    w = sample();
    w.tool_supply.fill(0);
    w.players[0].state.points = 50000;
    w.players[0].state.tool_amounts[0] = 8;
    Random restock_random(8);
    auto restocked = services::open_shop(w, restock_random, 0);
    require(restocked.tools.size() == 8, "depleted shared pool must not hide MOD shop tools");
    require(services::buy_tool(w, restocked, 0, 1) &&
                w.players[0].state.tool_amounts[0] == 9 &&
                w.tool_supply[0] == 0,
            "MOD tool purchase beyond eight remains possible and does not underflow supply");
    w.players[0].state.tool_amounts[0] = 998;
    restocked = services::open_shop(w, restock_random, 0);
    require(services::buy_tool(w, restocked, 0, 1) &&
                w.players[0].state.tool_amounts[0] == 999,
            "MOD depleted-pool tool can reach exact 999 cap");
    restocked = services::open_shop(w, restock_random, 0);
    const auto points_before = w.players[0].state.points;
    require(!services::buy_tool(w, restocked, 0, 1) &&
                w.players[0].state.tool_amounts[0] == 999 &&
                w.players[0].state.points == points_before,
            "1000th tool rejected without charging points");
}
void mod_restocked_cards() {
    auto w = sample();
    auto &player = w.players[0].state;
    player.cards = {};
    enable_card_stacks(player.cards);
    player.points = 50000;
    w.card_supply.fill(0);
    Random random(13);
    auto shelf = services::open_shop(w, random, 0);
    require(!shelf.cards.empty(), "MOD card shop must restock an exhausted world supply");
    const auto id = shelf.cards.front();
    require(services::buy_card(w, shelf, 0, id) && card_quantity(player.cards, id) == 1 &&
                w.card_supply[id - 1] == 0, "MOD restock sale must not underflow shared supply");
    player.cards.stacked_amounts[id - 1] = 998;
    shelf = services::open_shop(w, random, 0);
    shelf.cards.push_back(id);
    require(services::buy_card(w, shelf, 0, id) && card_quantity(player.cards, id) == 999 &&
                w.card_supply[id - 1] == 0,
            "MOD card shop sale reaches 999 without consuming another slot");
    shelf = services::open_shop(w, random, 0);
    shelf.cards.push_back(id);
    const auto points_before = player.points;
    require(!services::buy_card(w, shelf, 0, id) &&
                card_quantity(player.cards, id) == 999 && player.points == points_before,
            "MOD card shop rejects 1000th card without charging points");
}
void computer_policy() {
    auto w = sample();
    auto &p = w.players[1].state;
    p.points = 200;
    p.cards = {3, 6, 20};
    p.tool_amounts[6] = 2;
    Random random(71);
    auto shop = services::open_shop(w, random, 1);
    require(random.state() == 71, "ordinary AI shop must consume no RNG");
    require(shop.cards.empty() && shop.tools.empty(), "AI must not generate a human shelf");
    require(p.points < 200 && shop.revenue > 0, "AI must actually transact");
    // All initial cards are sold: the original preference bug keeps consulting
    // inventory slot zero's dislike value while the packed hand shifts left.
    require(shop.revenue >= 35 + 20 + 25 + 200, "original slot preference sell loop");
}
void magic_selection_and_effects() {
    auto w = sample();
    Random random(42);
    require(services::magic_targets(w, 0) == std::vector<std::size_t>({0, 1, 2, 3}), "wealth ties");
    require(services::magic_targets(w, 1).empty(), "no land cannot be most land");
    w.lands[1].economy.owner_code = 2;
    w.lands[1].economy.level = 2;
    w.facilities[1].owner_code = 3;
    w.facilities[1].level = 1;
    require(services::magic_targets(w, 1) == std::vector<std::size_t>({1, 2}), "land includes facilities");
    require(services::magic_targets(w, 2) == std::vector<std::size_t>({1, 2}),
            "buildings counted not level sum");
    w.players[1].state.controller_flags = 0;
    require(services::magic_targets(w, 2) == std::vector<std::size_t>({2}), "inactive excluded");
    const std::array<std::size_t, 2> targets{0, 2};
    services::MagicCallbacks callbacks;
    require(services::apply_magic(w, random, 0, targets, 4, callbacks), "cash to bank");
    require(w.players[0].state.cash == 0 && w.players[0].state.bank_balance == 200 &&
                w.players[2].state.cash == 0,
            "all magic targets applied");
    w.players[0].status.days_stopping = 0xff;
    require(services::apply_magic(w, random, 0, targets, 3, callbacks) &&
                w.players[0].status.days_stopping == 0,
            "stop high-bit wrap");
    require(!services::apply_magic(w, random, 0, targets, 2, callbacks),
            "missing physical callbacks cannot silently skip");
    unsigned fortune = 0, hostility = 0, prison = 0;
    callbacks.fortune = [&](std::size_t) { ++fortune; };
    callbacks.hostility = [&](std::size_t, std::size_t, std::int32_t amount) {
        require(amount == 180, "hostility indexed");
        ++hostility;
    };
    callbacks.defense = [](std::size_t slot) { return slot == 0 ? -1 : 3; };
    callbacks.prison = [&](std::size_t slot, unsigned days) {
        require(slot == 3 && days == 3, "defense redirected target");
        ++prison;
    };
    require(services::apply_magic(w, random, 0, targets, 1, callbacks) && fortune == 6,
            "three fortunes per target");
    require(services::apply_magic(w, random, 0, targets, 2, callbacks) && hostility == 2 && prison == 1,
            "defense and hostility");
    w.players[0].state.traffic_method = 2;
    w.players[0].state.dice_count = 3;
    w.players[0].state.tool_amounts[5] = 2;
    auto points = w.players[0].state.points;
    require(services::apply_magic(w, random, 0, std::span(targets).first(1), 8, callbacks),
            "sell equipped tools");
    require(w.players[0].state.traffic_method == 0 && w.players[0].state.dice_count == 1 &&
                w.players[0].state.points == points + 450 && w.players[0].state.tool_amounts[5] == 0,
            "equipped car included at full price");
}

void mod_wide_enterprise_shop_income() {
    auto w = sample();
    w.commercials[1].funds = 15'100'000'000LL;
    w.commercials[1].funds_change = 9'000'000'000LL;
    services::Shop shop;
    shop.revenue = 2'500'000'000LL;
    services::close_shop(w, shop, 6001);
    require(shop.closed &&
                w.commercials[1].funds == 17'600'000'000LL &&
                w.commercials[1].funds_change == 11'500'000'000LL,
            "wide shop close must credit both enterprise ledgers without wrap");
    w = sample();
    w.commercials[1].funds = INT64_MAX - 30;
    w.commercials[1].funds_change = 900;
    services::Shop rejected;
    rejected.revenue = 100;
    bool overflow = false;
    try { services::close_shop(w, rejected, 6001); }
    catch (const std::overflow_error &) { overflow = true; }
    require(overflow && !rejected.closed && w.commercials[1].funds == INT64_MAX - 30 &&
                w.commercials[1].funds_change == 900,
            "shop credit overflow must not partly credit company funds or close shop");
}
void mod_magic_64bit_money() {
    auto w = sample();
    w.players[0].state.cash = 3'000'000'000'000LL;
    w.players[1].state.cash = 4'000'000'000'000LL;
    w.players[0].state.bank_balance = 5'000'000'000'000LL;
    w.players[1].state.bank_balance = 6'000'000'000'000LL;
    require(services::magic_targets(w, 3) == std::vector<std::size_t>({1}),
            "magic cash ranking must compare signed64 instead of narrowing");
    require(services::magic_targets(w, 4) == std::vector<std::size_t>({1}),
            "magic deposit ranking must compare signed64 instead of narrowing");
    require(services::magic_targets(w, 0) == std::vector<std::size_t>({1}),
            "magic net-wealth ranking must compare full 64-bit value");
    Random random(123);
    const std::array<std::size_t, 2> group{0, 1};
    require(services::apply_magic(w, random, 0, group, 4, {}),
            "magic transfer cash to bank with 64-bit amounts");
    require(w.players[0].state.cash == 0 &&
                w.players[0].state.bank_balance == 8'000'000'000'000LL &&
                w.players[1].state.cash == 0 &&
                w.players[1].state.bank_balance == 10'000'000'000'000LL,
            "magic cash-to-bank transfer must conserve funds above int32");

    auto overflow = sample();
    overflow.players[0].state.cash = 3'000'000'000'000LL;
    overflow.players[0].state.bank_balance = 4'000'000'000'000LL;
    overflow.players[1].state.cash = 1000;
    overflow.players[1].state.bank_balance = std::numeric_limits<std::int64_t>::max() - 500;
    bool rejected = false;
    try {
        (void)services::apply_magic(overflow, random, 0, group, 4, {});
    } catch (const std::overflow_error &) {
        rejected = true;
    }
    require(rejected, "overflowing target must reject the entire cash-to-bank spell");
    require(overflow.players[0].state.cash == 3'000'000'000'000LL &&
                overflow.players[0].state.bank_balance == 4'000'000'000'000LL &&
                overflow.players[1].state.cash == 1000 &&
                overflow.players[1].state.bank_balance == std::numeric_limits<std::int64_t>::max() - 500,
            "rejected group cash-to-bank spell must not partially transfer first target");
}
} // namespace
int main() {
    try {
        weighted_and_shelf();
        transactions();
        mod_restocked_cards();
        computer_policy();
        magic_selection_and_effects();
        mod_wide_enterprise_shop_income();
        mod_magic_64bit_money();
        std::cout << "shop inventories, pricing, AI, magic targeting/effects passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
