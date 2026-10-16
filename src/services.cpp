// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/services.hpp"
#include "rich4/mod_rules.hpp"
#include "rich4/stocks.hpp"
#include <algorithm>
#include <bit>
#include <numeric>
#include <stdexcept>

namespace rich4::services {
namespace {
constexpr std::array<unsigned, 30> card_preferences{2, 2, 1, 0, 0, 0, 0, 1, 0, 2, 2, 1, 2, 0, 2,
                                                    1, 2, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 0, 0};
constexpr std::array<unsigned, 13> tool_preferences{0, 1, 1, 1, 0, 0, 2, 0, 1, 2, 1, 2, 2};
std::int32_t add(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(a) + std::bit_cast<std::uint32_t>(b));
}
template <class Supply> std::uint8_t weighted(const Supply &supply, Random &random) {
    const auto total = std::accumulate(supply.begin(), supply.end(), 0U);
    if (!total)
        return 0;
    auto index = static_cast<unsigned>(random.next()) % total;
    for (std::size_t id = 0; id < supply.size(); ++id) {
        if (index < supply[id])
            return static_cast<std::uint8_t>(id + 1);
        index -= supply[id];
    }
    throw std::logic_error("weighted stock invariant");
}
void buy_card_unchecked(World &world, Shop &shop, std::size_t slot, std::uint8_t id) {
    auto &player = world.players.at(slot).state;
    receive_card(player.cards, world.card_supply, card_prices, id);
    player.points = static_cast<std::uint16_t>(player.points - card_prices.at(id - 1));
    shop.revenue = add(shop.revenue, card_prices.at(id - 1) * 10);
}
void buy_tool_unchecked(World &world, Shop &shop, std::size_t slot, std::uint8_t id) {
    auto &player = world.players.at(slot).state;
    (void)receive_tool(player.tool_amounts, world.tool_supply, id);
    player.points = static_cast<std::uint16_t>(player.points - tool_prices.at(id - 1));
    shop.revenue = add(shop.revenue, tool_prices.at(id - 1) * 10);
}
void computer_shop(World &world, Shop &shop, std::size_t slot) {
    auto &player = world.players.at(slot).state;
    const auto profile = world.players.at(slot).status.rescue_strategy;
    const auto dislikes = [profile](unsigned preference) { return int(preference) - int(profile) == 2; };
    // Original 0x42edac indexes the preference table by INVENTORY SLOT, not ID.
    for (std::size_t index = 0; index < player.cards.size() && player.cards[index];) {
        if (dislikes(card_preferences[index]))
            (void)sell_card(world, shop, slot, player.cards[index]);
        else
            ++index;
    }
    for (std::size_t index = 0; index < tool_prices.size(); ++index)
        if (player.tool_amounts[index] && dislikes(tool_preferences[index]))
            (void)sell_tool(world, shop, slot, static_cast<std::uint8_t>(index + 1),
                            player.tool_amounts[index]);
    if (player.points < 100) {
        if (const auto card = cheapest_card(player.cards, card_prices))
            (void)sell_card(world, shop, slot, card);
        for (std::size_t index = 0; index < tool_prices.size(); ++index)
            if (player.tool_amounts[index] > 1)
                (void)sell_tool(world, shop, slot, static_cast<std::uint8_t>(index + 1),
                                player.tool_amounts[index] - 1U);
        const auto transport = player.traffic_method & 3U;
        if (transport == 1 && player.tool_amounts[4])
            (void)sell_tool(world, shop, slot, 5, player.tool_amounts[4]);
        if (transport == 2 && player.tool_amounts[5])
            (void)sell_tool(world, shop, slot, 6, player.tool_amounts[5]);
    }
    unsigned card_budget = player.points / 2U, tool_budget = player.points - card_budget;
    std::vector<std::uint8_t> candidates;
    for (std::size_t index = 0; index < card_prices.size(); ++index)
        if (card_prices[index] <= card_budget && !dislikes(card_preferences[index]))
            candidates.insert(candidates.end(), world.card_supply[index],
                              static_cast<std::uint8_t>(index + 1));
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](auto a, auto b) { return card_prices[a - 1] > card_prices[b - 1]; });
    for (const auto id : candidates) {
        if (card_count(player.cards) == player.cards.size())
            break;
        if (card_prices[id - 1] <= card_budget) {
            buy_card_unchecked(world, shop, slot, id);
            card_budget -= card_prices[id - 1];
        }
    }
    for (const std::uint8_t id : {std::uint8_t(5), std::uint8_t(6)}) {
        if ((player.traffic_method & 3U) != id - 4U && !player.tool_amounts[id - 1] &&
            tool_prices[id - 1] < tool_budget && world.tool_supply[id - 1]) {
            buy_tool_unchecked(world, shop, slot, id);
            tool_budget -= tool_prices[id - 1];
        }
    }
    constexpr std::array<std::uint8_t, 6> order{8, 2, 7,
                                                1, 4, 3}; // Original 0x4755f0, converted to one-based IDs.
    for (const auto id : order) {
        if (player.tool_amounts[id - 1] < mod::max_tool_amount && !dislikes(tool_preferences[id - 1]) &&
            tool_prices[id - 1] <= tool_budget && world.tool_supply[id - 1]) {
            buy_tool_unchecked(world, shop, slot, id);
            tool_budget -= tool_prices[id - 1];
        }
    }
}
std::uint16_t property_under(const World &world, std::size_t slot) {
    const auto &player = world.players.at(slot);
    const auto &status = player.status;
    if (status.days_in_hotel || status.days_disappearing || status.days_in_prison || status.days_in_hospital)
        return 0;
    const auto code = world.nodes.at(player.state.node_id).type_and_index;
    return code > 2000 && code < 6000 ? code : 0;
}
} // namespace

std::uint8_t receive_random_card(World &world, Random &random, std::size_t slot) {
    const auto id = weighted(world.card_supply, random);
    if (id)
        receive_card(world.players.at(slot).state.cards, world.card_supply, card_prices, id);
    return id;
}
std::uint8_t receive_random_tool(World &world, Random &random, std::size_t slot) {
    const auto id = weighted(world.tool_supply, random);
    if (id)
        (void)receive_tool(world.players.at(slot).state.tool_amounts, world.tool_supply, id);
    return id; // Original returns the selected ID even if the per-type cap rejects it.
}
Shop open_shop(World &world, Random &random, std::size_t slot, std::uint16_t property) {
    Shop shop;
    const auto &player = world.players.at(slot).state;
    if (property > 6000 && property < 8000 && world.commercials.at(property - 6000).owner_code == slot + 1) {
        if (random.next() & 1)
            shop.gift_tool = receive_random_tool(world, random, slot);
        else
            shop.gift_card = receive_random_card(world, random, slot);
    }
    if (player.controller_flags != 1) {
        computer_shop(world, shop, slot);
        return shop;
    }
    auto supply = world.card_supply;
    const unsigned count = static_cast<unsigned>(random.next()) % 10 + 6;
    for (unsigned i = 0; i < count; ++i) {
        const auto id = weighted(supply, random);
        if (!id)
            break; // Explicit safe boundary for an empty supply, original divided by zero.
        shop.cards.push_back(id);
        --supply[id - 1];
    }
    for (std::size_t index = 0; index < world.tool_supply.size(); ++index)
        if (world.tool_supply[index])
            shop.tools.push_back(static_cast<std::uint8_t>(index + 1));
    return shop;
}
bool buy_card(World &world, Shop &shop, std::size_t slot, std::uint8_t id) {
    auto &player = world.players.at(slot).state;
    const auto item = std::find(shop.cards.begin(), shop.cards.end(), id);
    if (shop.closed || !id || id > 30 || item == shop.cards.end() || player.points < card_prices[id - 1] ||
        card_count(player.cards) == player.cards.size() || !world.card_supply[id - 1])
        return false;
    buy_card_unchecked(world, shop, slot, id);
    shop.cards.erase(item);
    return true;
}
bool buy_tool(World &world, Shop &shop, std::size_t slot, std::uint8_t id) {
    auto &player = world.players.at(slot).state;
    const auto item = std::find(shop.tools.begin(), shop.tools.end(), id);
    if (shop.closed || !id || id > 8 || item == shop.tools.end() || player.points < tool_prices[id - 1] ||
        player.tool_amounts[id - 1] >= mod::max_tool_amount || !world.tool_supply[id - 1])
        return false;
    buy_tool_unchecked(world, shop, slot, id);
    if (!world.tool_supply[id - 1])
        shop.tools.erase(item);
    return true;
}
bool sell_card(World &world, Shop &shop, std::size_t slot, std::uint8_t id) {
    auto &player = world.players.at(slot).state;
    if (shop.closed || !id || id > 30 || !consume_card(player.cards, world.card_supply, id))
        return false;
    player.points = static_cast<std::uint16_t>(player.points + unsigned(card_prices[id - 1]) * 9U / 10U);
    shop.revenue = add(shop.revenue, card_prices[id - 1]);
    return true;
}
bool sell_tool(World &world, Shop &shop, std::size_t slot, std::uint8_t id, unsigned quantity) {
    auto &player = world.players.at(slot).state;
    if (shop.closed || !id || id > 13 || !quantity || quantity > player.tool_amounts[id - 1])
        return false;
    const unsigned price = tool_prices[id - 1] * quantity;
    for (unsigned i = 0; i < quantity; ++i)
        (void)consume_tool(player.tool_amounts, world.tool_supply, id);
    mod::credit_points(player.points, price * 9U / 10U);
    shop.revenue = add(shop.revenue, static_cast<std::int32_t>(price));
    return true;
}
void close_shop(World &world, Shop &shop, std::uint16_t property) {
    if (shop.closed)
        return;
    if (property > 6000 && property < 8000) {
        auto &commercial = world.commercials.at(property - 6000);
        commercial.funds = add(commercial.funds, shop.revenue);
        commercial.funds_change = add(commercial.funds_change, shop.revenue);
    }
    shop.closed = true;
}

std::vector<std::size_t> magic_targets(const World &world, unsigned category) {
    if (category >= 12)
        throw std::invalid_argument("magic category outside original table");
    std::vector<std::size_t> result;
    std::int64_t maximum = 0;
    for (std::size_t slot = 0; slot < world.players.size(); ++slot) {
        const auto &player = world.players[slot];
        if (!player.state.controller_flags)
            continue;
        if (category >= 6) {
            const auto transport = player.state.traffic_method & 3U;
            const bool include = category == 6    ? transport == 0
                                 : category == 7  ? transport == 1
                                 : category == 8  ? transport == 2
                                 : category == 9  ? player.status.god_info != 0
                                 : category == 10 ? player.state.sex != 0
                                                  : player.state.sex == 0;
            if (include)
                result.push_back(slot);
            continue;
        }
        std::int64_t value = 0;
        if (category == 0)
            value = stocks::player_wealth(world, slot);
        else if (category == 1 || category == 2) {
            for (std::size_t id = 1; id < world.lands.size(); ++id)
                if (world.lands[id].economy.owner_code == slot + 1 &&
                    (category == 1 || world.lands[id].economy.level))
                    ++value;
            for (std::size_t id = 1; id < world.facilities.size(); ++id)
                if (world.facilities[id].owner_code == slot + 1 &&
                    (category == 1 || world.facilities[id].level))
                    ++value;
        } else if (category == 3)
            value = player.state.cash;
        else if (category == 4)
            value = player.state.bank_balance;
        else
            value = player.state.points;
        if (category != 0 && !value)
            continue;
        if (value < maximum)
            continue;
        if (value > maximum) {
            maximum = value;
            result.clear();
        }
        result.push_back(slot);
    }
    return result;
}
std::optional<MagicChoice> choose_magic_ai(const World &world, Random &random, std::size_t actor) {
    bool any = false;
    for (unsigned category = 0; category < 12; ++category)
        any |= !magic_targets(world, category).empty();
    if (!any)
        return std::nullopt;
    unsigned category;
    std::vector<std::size_t> targets;
    do {
        category = static_cast<unsigned>(random.next()) % 12;
        targets = magic_targets(world, category);
    } while (targets.empty());
    unsigned effect = 6;
    if (std::find(targets.begin(), targets.end(), actor) == targets.end()) {
        effect = static_cast<unsigned>(random.next()) % 11;
        if (effect == 6)
            effect = 7;
    }
    return MagicChoice{category, effect};
}
bool apply_magic(World &world, Random &random, std::size_t actor, std::span<const std::size_t> targets,
                 unsigned effect, const MagicCallbacks &callbacks) {
    if (actor >= world.players.size() || effect >= 12)
        throw std::invalid_argument("invalid magic action");
    for (const auto slot : targets)
        if (slot >= world.players.size())
            throw std::out_of_range("magic target");
    if ((effect == 1 && !callbacks.fortune) ||
        ((effect == 2 || effect == 10) && (!callbacks.defense || !callbacks.hostility)) ||
        (effect == 2 && !callbacks.prison) || (effect == 10 && !callbacks.hospital) ||
        (effect == 5 && !callbacks.upgrade) || (effect == 9 && !callbacks.downgrade) ||
        (effect == 7 && !callbacks.turn_around) || (effect == 11 && !callbacks.auction))
        return false;
    // The cash-to-bank spell affects every selected player. Validate all
    // 64-bit balances before touching any account, so overflow cannot leave
    // a partially transferred target group.
    if (effect == 4) {
        for (const auto slot : targets) {
            const auto &state = world.players[slot].state;
            (void)mod::money_add(state.bank_balance, state.cash);
        }
    }
    for (const auto slot : targets) {
        auto &player = world.players.at(slot);
        switch (effect) {
        case 0:
            player.state.points = static_cast<std::uint16_t>(
                player.state.points + sell_all_cards(player.state.cards, world.card_supply, card_prices));
            break;
        case 1:
            for (unsigned i = 0; i < 3; ++i)
                callbacks.fortune(slot);
            break;
        case 2:
        case 10: {
            callbacks.hostility(slot, actor,
                                std::bit_cast<std::int32_t>(std::uint32_t(world.header.price_index) * 90U));
            const int target = callbacks.defense(slot);
            if (target >= 0) {
                if (effect == 2)
                    callbacks.prison(static_cast<std::size_t>(target), 3);
                else
                    callbacks.hospital(static_cast<std::size_t>(target), 3);
            }
            break;
        }
        case 3:
            player.status.days_stopping = static_cast<std::uint8_t>((player.status.days_stopping + 1) & 0x7f);
            break;
        case 4:
            player.state.bank_balance = mod::money_add(player.state.bank_balance, player.state.cash);
            player.state.cash = 0;
            break;
        case 5:
            if (const auto property = property_under(world, slot))
                callbacks.upgrade(property);
            break;
        case 6:
            (void)receive_random_card(world, random, slot);
            break;
        case 7: {
            const auto &status = player.status;
            if (!status.days_in_hotel && !status.days_disappearing && !status.days_in_prison &&
                !status.days_in_hospital)
                callbacks.turn_around(slot);
            break;
        }
        case 8: {
            const auto transport = player.state.traffic_method & 3U;
            if (transport) {
                const auto index = transport == 1 ? 4U : transport == 2 ? 5U : 11U;
                mod::grant_equipped_tool(player.state.tool_amounts[index]);
            }
            player.state.traffic_method = 0;
            player.state.dice_count = 1;
            mod::credit_points(player.state.points,
                               sell_stored_tools(player.state.tool_amounts, world.tool_supply, tool_prices));
            break;
        }
        case 9:
            if (const auto property = property_under(world, slot))
                callbacks.downgrade(property);
            break;
        case 11:
            if (const auto property = property_under(world, slot))
                callbacks.auction(property, slot);
            break;
        default:
            break;
        }
    }
    return true;
}
} // namespace rich4::services
