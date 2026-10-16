// SPDX-License-Identifier: GPL-3.0-or-later
// Original purchase/outcome/settlement rules: asm/rich4_ui_letou.asm.
#include "rich4/lottery.hpp"
#include "rich4/mod_rules.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <stdexcept>

namespace rich4::lottery {
namespace {
void validate_owners(const World &world) {
    if (world.players.size() > 4)
        throw SaveError("Lottery supports the original four player slots");
    for (const auto owner : world.lottery_tickets)
        if (owner > world.players.size())
            throw SaveError("Lottery ticket has an invalid player owner");
}
std::int32_t add(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(a) + std::bit_cast<std::uint32_t>(b));
}
} // namespace

std::vector<std::uint8_t> available_numbers(const World &world) {
    validate_owners(world);
    std::vector<std::uint8_t> result;
    for (std::size_t i = 0; i < world.lottery_tickets.size(); ++i)
        if (world.lottery_tickets[i] == 0)
            result.push_back(static_cast<std::uint8_t>(i + 1));
    return result;
}

Purchase buy_ticket(World &world, std::size_t actor, std::uint32_t number) {
    if (number == 0 || number > world.lottery_tickets.size())
        throw std::out_of_range("Lottery number must be1..36");
    validate_owners(world);
    auto &player = world.players.at(actor);
    if (player.state.controller_flags == 0)
        return Purchase::inactive_player;
    if (world.lottery_tickets[number - 1] != 0)
        return Purchase::already_owned;
    if (player.state.cash < ticket_price)
        return Purchase::insufficient_cash;
    const auto next_fund = mod::money_add(world.public_fund, ticket_price);
    world.lottery_tickets[number - 1] = static_cast<std::uint8_t>(actor + 1);
    player.state.cash -= ticket_price;
    world.public_fund = next_fund;
    // The original direct subtraction does not add to cumulative spending.
    return Purchase::purchased;
}

std::optional<std::uint8_t> buy_random_ticket(World &world, Random &random, std::size_t actor) {
    const auto &player = world.players.at(actor);
    if (player.state.controller_flags == 0 || player.state.cash <= ticket_price)
        return std::nullopt;
    const auto numbers = available_numbers(world);
    if (numbers.empty())
        return std::nullopt;
    const auto number = numbers[static_cast<std::size_t>(random.next()) % numbers.size()];
    if (buy_ticket(world, actor, number) != Purchase::purchased)
        throw SaveError("Lottery purchase state changed during selection");
    return number;
}

void discard_player_tickets(World &world, std::size_t actor) {
    (void)world.players.at(actor);
    for (auto &ticket : world.lottery_tickets)
        if (ticket == actor + 1)
            ticket = 0;
}

Draw draw(World &world, Random &random) {
    validate_owners(world);
    std::array<std::uint8_t, 4> owned{};
    std::vector<std::uint8_t> sold;
    for (std::size_t index = 0; index < world.lottery_tickets.size(); ++index) {
        const auto owner = world.lottery_tickets[index];
        if (owner != 0) {
            ++owned[owner - 1];
            sold.push_back(static_cast<std::uint8_t>(index + 1));
        }
    }
    if (sold.empty())
        return {};
    Draw result;
    result.held = true;
    result.sold_numbers_only = std::any_of(owned.begin(), owned.end(), [](auto count) { return count > 10; });
    const auto value = random.next();
    result.number = result.sold_numbers_only ? sold[static_cast<std::size_t>(value) % sold.size()]
                                             : static_cast<std::uint8_t>(value % 36 + 1);
    const auto owner = world.lottery_tickets[result.number - 1];
    if (owner != 0) {
        result.winner = owner - 1;
        result.payout = world.public_fund;
        auto &player = world.players[*result.winner];
        player.state.cash = mod::money_add(player.state.cash, result.payout);
        player.status.cumulative_income += static_cast<std::uint32_t>(result.payout);
        world.public_fund = 0;
        world.lottery_tickets.fill(0);
    }
    return result;
}

bool draw_due(SaveDate date) noexcept {
    return date.day == 15;
}
} // namespace rich4::lottery
