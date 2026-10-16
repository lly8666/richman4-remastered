// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/lottery.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace rich4;
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
World fixture() {
    World world;
    world.players.resize(4);
    for (std::size_t i = 0; i < 4; ++i) {
        world.players[i].state.slot = static_cast<std::uint32_t>(i);
        world.players[i].state.controller_flags = i == 0 ? 1 : 2;
        world.players[i].state.cash = 5000;
        world.players[i].state.bank_balance = 10000;
    }
    return world;
}
void purchases() {
    auto world = fixture();
    world.header.price_index = 99;
    world.players[0].state.cash = 1000;
    require(lottery::buy_ticket(world, 0, 36) == lottery::Purchase::purchased,
            "Human may spend the final 1000 and select the last number");
    require(world.lottery_tickets[35] == 1 && world.players[0].state.cash == 0 && world.public_fund == 1000 &&
                world.players[0].status.cumulative_spending == 0 &&
                world.players[0].state.bank_balance == 10000,
            "Ticket price is unindexed cash, not a toll/accounting expense");
    require(lottery::buy_ticket(world, 1, 36) == lottery::Purchase::already_owned &&
                world.public_fund == 1000,
            "Cannot sell the same number twice");
    require(lottery::buy_ticket(world, 0, 1) == lottery::Purchase::insufficient_cash,
            "Bank funds do not buy tickets");
    world.players[2].state.controller_flags = 0;
    require(lottery::buy_ticket(world, 2, 1) == lottery::Purchase::inactive_player,
            "No inactive-player purchase");
    world.public_fund = std::numeric_limits<std::int32_t>::max() - 999;
    require(lottery::buy_ticket(world, 1, 1) == lottery::Purchase::purchased &&
                world.public_fund == std::int64_t(INT32_MAX) + 1,
            "MOD ticket purchase retains whole public fund beyond signed32");
    for (auto invalid : {0U, 37U}) {
        bool rejected = false;
        try {
            (void)lottery::buy_ticket(world, 1, invalid);
        } catch (const std::out_of_range &) {
            rejected = true;
        }
        require(rejected, "Invalid visible number is rejected");
    }
    world.lottery_tickets[0] = 5;
    bool rejected = false;
    try {
        (void)lottery::available_numbers(world);
    } catch (const SaveError &) {
        rejected = true;
    }
    require(rejected, "Invalid saved owner cannot index outside players");
}
void computer_purchase() {
    auto world = fixture();
    Random random(0);
    world.players[1].state.cash = 1000;
    require(!lottery::buy_random_ticket(world, random, 1) && random.state() == 0,
            "AI requires strictly more than1000 without drawing RNG on rejection");
    world.players[1].state.cash = 1001;
    world.lottery_tickets.fill(1);
    world.lottery_tickets[14] = 0;
    const auto chosen = lottery::buy_random_ticket(world, random, 1);
    require(chosen == 15 && world.players[1].state.cash == 1 && random.state() == 12345 &&
                world.lottery_tickets[14] == 2,
            "Single free number still consumes exactly one original random draw");
    world.players[1].state.cash = 5000;
    require(!lottery::buy_random_ticket(world, random, 1) && random.state() == 12345,
            "Sold-out visit consumes no outcome draw");
}
void drawings() {
    auto world = fixture();
    Random random(0);
    world.public_fund = 15000;
    require(!lottery::draw(world, random).held && random.state() == 0 && world.public_fund == 15000,
            "Pool alone does not cause a drawing");
    world.lottery_tickets[1] = 1; // Seed0 produces number1; it is unowned.
    const auto miss = lottery::draw(world, random);
    require(miss.held && miss.number == 1 && !miss.winner && !miss.sold_numbers_only &&
                world.public_fund == 15000 && world.lottery_tickets[1] == 1,
            "A miss retains the pool AND all sold tickets");
    random = Random(0);
    world.lottery_tickets[0] = 2;
    world.players[1].state.cash = std::numeric_limits<std::int32_t>::max();
    world.players[1].status.cumulative_income = 0xfffffff0;
    const auto win = lottery::draw(world, random);
    require(win.winner == 1 && win.number == 1 && win.payout == 15000 && world.public_fund == 0 &&
                std::all_of(world.lottery_tickets.begin(), world.lottery_tickets.end(),
                            [](auto value) { return value == 0; }),
            "Winning ticket pays its owner and retires every ticket");
    require(world.players[1].state.cash == std::int64_t(INT32_MAX) + 15000 &&
                world.players[1].status.cumulative_income == 14984 &&
                world.players[1].state.bank_balance == 10000,
            "MOD prize credits whole signed64 cash; legacy status counter still wraps");
    world = fixture();
    for (std::size_t i = 10; i < 20; ++i)
        world.lottery_tickets[i] = 1;
    random = Random(0);
    require(!lottery::draw(world, random).sold_numbers_only && world.lottery_tickets[10] == 1,
            "Ten tickets does not trigger the guaranteed sold-number branch");
    world.lottery_tickets[20] = 1;
    world.lottery_tickets[30] = 3;
    random = Random(0);
    const auto guaranteed = lottery::draw(world, random);
    require(guaranteed.sold_numbers_only && guaranteed.number == 11 && guaranteed.winner == 0,
            "Eleven tickets by one owner selects among ALL sold numbers in ascending order");
    world = fixture();
    for (std::size_t i = 0; i < 20; ++i)
        world.lottery_tickets[i] = static_cast<std::uint8_t>(i < 10 ? 1 : 2);
    random = Random(1);
    require(!lottery::draw(world, random).sold_numbers_only, "Threshold is per-owner, not total sold count");
}
void wide_pool_and_atomic_rejection() {
    auto w = fixture();
    w.public_fund = 15'100'000'000LL;
    w.lottery_tickets[0] = 1;
    Random random(0);
    const auto win = lottery::draw(w, random);
    require(win.winner == 0 && win.payout == 15'100'000'000LL &&
                w.players[0].state.cash == 15'100'005'000LL && w.public_fund == 0,
            "15.1b pool must pay full winner cash without signed32 truncation");
    w = fixture();
    w.public_fund = INT64_MAX - 999;
    w.players[0].state.cash = 1000;
    const auto tickets = w.lottery_tickets;
    bool rejected = false;
    try { (void)lottery::buy_ticket(w, 0, 1); }
    catch (const std::overflow_error &) { rejected = true; }
    require(rejected && w.public_fund == INT64_MAX - 999 &&
                w.players[0].state.cash == 1000 && w.lottery_tickets == tickets,
            "ticket pool overflow must not debit cash or reserve the number");
}
void cleanup_and_calendar() {
    auto world = fixture();
    world.lottery_tickets[0] = 1;
    world.lottery_tickets[1] = 2;
    world.public_fund = 2000;
    lottery::discard_player_tickets(world, 0);
    require(world.lottery_tickets[0] == 0 && world.lottery_tickets[1] == 2 && world.public_fund == 2000 &&
                world.players[0].state.cash == 5000,
            "Bankruptcy discards only that owner's numbers without refund");
    require(lottery::draw_due({15, 10, 2026}) && !lottery::draw_due({14, 10, 2026}) &&
                !lottery::draw_due({16, 10, 2026}),
            "Only original monthly day15 triggers the drawing");
}
} // namespace
int main() {
    try {
        purchases();
        computer_purchase();
        drawings();
        wide_pool_and_atomic_rejection();
        cleanup_and_calendar();
        std::cout << "Lottery state tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
