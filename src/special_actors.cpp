// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/special_actors.hpp"
#include "rich4/mod_rules.hpp"
#include "rich4/direction.hpp"
#include "rich4/legacy_float.hpp"
#include "rich4/services.hpp"
#include <bit>
#include <stdexcept>

namespace rich4::special_actors {
namespace {
SpecialActor &actor(World &world, std::size_t slot) {
    if (slot < 4 || slot > 7)
        throw std::out_of_range("Special actor slot must be 4..7");
    return world.special_actors.at(slot - 4);
}
Result missing(const char *message) {
    Result result;
    result.supported = false;
    result.message = message;
    return result;
}
void prize_voice(Random &random, const Callbacks &callbacks, std::size_t owner, unsigned price) {
    std::uint8_t category = 2;
    if (price > 100)
        category = 0;
    else if (price > 50)
        category = static_cast<std::uint8_t>(random.next() & 1U);
    if (price && callbacks.voice)
        callbacks.voice(owner, category);
}
void append(Result &result, EffectKind kind, std::size_t slot, std::int32_t target, std::int64_t amount,
            std::uint16_t node) {
    result.effects.push_back({kind, slot, target, amount, node});
}
} // namespace

void age_status(SpecialActor &state) noexcept {
    for (auto *counter : {&state.days_sleeping, &state.days_sleep_walking, &state.days_stopping,
                          &state.days_tortoise_walking}) {
        if (*counter & 0x80U)
            *counter = 0;
        else if (*counter && --*counter == 0)
            *counter = 0x80;
    }
}

std::uint16_t move_one_step(World &world, Random &random, std::size_t slot) {
    auto &state = actor(world, slot);
    if (!state.node_id)
        throw std::out_of_range("Active special actor has no map node");
    auto &previous = world.nodes.at(state.node_id);
    std::array<std::uint16_t, 4> candidates{};
    unsigned count = 0;
    for (unsigned link = 0; link < 4; ++link) {
        const auto node = previous.neighbors[link];
        if (node && node != state.last_node_id && !(previous.flags & (0x40000000U >> link)))
            candidates[count++] = node;
    }
    const auto next_id = count ? candidates[random.next() % count] : state.last_node_id;
    if (!next_id)
        throw std::out_of_range("Special actor has no forward or return route");
    auto &next = world.nodes.at(next_id);
    state.last_node_id = state.node_id;
    state.node_id = next_id;
    const auto occupancy = 0x1000U << (slot - 4);
    previous.flags &= ~occupancy;
    next.flags |= occupancy;
    state.direction = direction::from_delta(next.x - previous.x, next.y - previous.y);
    state.x = static_cast<std::uint16_t>(next.x);
    state.y = static_cast<std::uint16_t>(next.y);
    return next_id;
}

Result interact(World &world, Random &random, std::size_t slot, bool stopped, const Callbacks &callbacks) {
    auto &state = actor(world, slot);
    const auto node_id = state.node_id;
    const auto node = world.nodes.at(node_id); // Original handler snapshots these flags before side effects.
    const auto event = node.flags & 0xffU;
    const auto object_id = static_cast<std::uint16_t>((node.flags >> 16) & 0xffU);
    const auto kind = object_id ? world.objects.at(object_id - 1).kind : 0;
    const auto owner = state.owner;
    if (owner >= world.players.size())
        throw std::out_of_range("Special actor owner outside player table");
    Result result;
    const bool pickup = slot == 4 && state.days_sleep_walking == 0;
    if ((kind == 11 && stopped) || (kind == 17 && slot != 4 && stopped)) {
        if (!callbacks.remove_object || !callbacks.hospital)
            return missing("特殊角色住院规则尚未接入");
        callbacks.remove_object(kind == 11 ? 11 : object_id);
        callbacks.hospital(slot, 3);
        result.stop_movement = true;
        append(result, EffectKind::hospitalized, slot, -1, 3, node_id);
    } else if ((kind == 13 || kind == 14 || kind == 16 || kind == 17 || kind == 18) && pickup) {
        if (!callbacks.remove_object)
            return missing("特殊角色道具拾取规则尚未接入");
        callbacks.remove_object(kind == 13 ? 13 : kind == 14 ? 14 : object_id);
        if (kind == 14) {
            auto &points = world.players[owner].state.points;
            points = static_cast<std::uint16_t>(points + 500U);
            append(result, EffectKind::points_received, slot, owner, 500, node_id);
            if (callbacks.voice)
                callbacks.voice(owner, 0);
        } else {
            const auto tool = kind == 13 ? services::receive_random_tool(world, random, owner)
                                         : static_cast<std::uint8_t>(kind - 14);
            if (tool) {
                if (kind != 13)
                    (void)receive_tool(world.players[owner].state.tool_amounts, world.tool_supply, tool);
                append(result, EffectKind::tool_received, slot, owner, tool, node_id);
                if (kind == 13)
                    prize_voice(random, callbacks, owner, services::tool_prices.at(tool - 1));
            }
        }
    } else if (kind == 16 && slot != 4) {
        if (!callbacks.remove_object)
            return missing("特殊角色路障规则尚未接入");
        callbacks.remove_object(object_id);
        result.stop_movement = true;
        append(result, EffectKind::blocked, slot, -1, 0, node_id);
    }
    stopped = stopped || result.stop_movement;

    if (state.inactive_state == 0 && state.days_sleep_walking == 0) {
        if (slot == 4 || slot == 5) {
            const auto victims = ((node.flags >> 8) & 15U) & ~(1U << owner);
            if (victims) {
                const auto victim = static_cast<std::size_t>(std::countr_zero(victims));
                auto &target = world.players.at(victim).state;
                if (target.controller_flags) {
                    if (slot == 4) {
                        const auto points = static_cast<std::uint16_t>(target.points / 2);
                        if (points) {
                            target.points = static_cast<std::uint16_t>(target.points - points);
                            auto &destination = world.players[owner].state.points;
                            destination = static_cast<std::uint16_t>(destination + points);
                            append(result, EffectKind::points_taken, slot, static_cast<std::int32_t>(victim),
                                   points, node_id);
                        }
                    } else if (const auto count = card_count(target.cards)) {
                        const auto card = target.cards[random.next() % count];
                        (void)consume_card(target.cards, world.card_supply, card);
                        receive_card(world.players[owner].state.cards, world.card_supply,
                                     services::card_prices, card);
                        append(result, EffectKind::card_taken, slot, static_cast<std::int32_t>(victim), card,
                               node_id);
                    }
                }
            }
            if (slot == 5 && event == 14) {
                if (!callbacks.pay)
                    return missing("特殊角色银行结算尚未接入");
                for (std::size_t victim = 0; victim < world.players.size(); ++victim) {
                    const auto &target = world.players[victim].state;
                    if (victim == owner || !target.controller_flags)
                        continue;
                    const auto amount =
                        (numeric::Extended::from(target.bank_balance) * numeric::Extended::from(0.2))
                            .integer64();
                    callbacks.pay(static_cast<std::int32_t>(victim), owner, amount, 5);
                    append(result, EffectKind::bank_taken, slot, static_cast<std::int32_t>(victim), amount,
                           node_id);
                }
            }
        } else if (stopped) {
            const auto code = node.type_and_index;
            std::int64_t amount = 0;
            std::int32_t payer = -1;
            bool collect = false;
            if (code > 2000 && code < 4000) {
                const auto &land = world.lands.at(code - 2000);
                if (land.economy.owner_code && land.economy.owner_code != owner + 1) {
                    payer = land.economy.owner_code - 1;
                    if (slot == 6) {
                        for (std::size_t i = 1; i < world.lands.size(); ++i) {
                            const auto &other = world.lands[i];
                            if (other.economy.owner_code == land.economy.owner_code &&
                                other.economy.street_name == land.economy.street_name)
                                amount += other.land_price;
                        }
                        amount *= world.header.price_index;
                        collect = true;
                    } else {
                        amount = std::bit_cast<std::int32_t>(land.removed_building);
                        collect = amount != 0;
                    }
                }
            } else if (code > 4000 && code < 6000) {
                const auto &facility = world.facilities.at(code - 4000);
                if (facility.owner_code && facility.owner_code != owner + 1) {
                    payer = facility.owner_code - 1;
                    amount = slot == 6 ? facility.land_price *
                                             std::bit_cast<std::uint32_t>(world.header.price_index)
                                       : std::bit_cast<std::int32_t>(facility.removed_building);
                    collect = slot == 6 || amount != 0;
                }
            } else if (code > 6000 && code < 8000 && slot == 7) {
                const auto &company = world.commercials.at(code - 6000);
                if (company.owner_code && company.owner_code != owner + 1) {
                    payer = code - 5900;
                    amount = company.funds;
                    collect = amount != 0;
                }
            }
            if (collect) {
                if (!callbacks.pay)
                    return missing("特殊角色地产结算尚未接入");
                callbacks.pay(payer, owner, amount, 0);
                append(result, EffectKind::property_taken, slot, payer, amount, node_id);
            }
        }
    }
    const auto destination = state.flags & 0x7fU;
    if ((destination == 1 && event == 4) || (destination == 2 && event == 5)) {
        if (state.flags & 0x80U) {
            const auto &confine = destination == 1 ? callbacks.prison : callbacks.hospital;
            if (!confine)
                return missing("特殊角色返回监狱或医院规则尚未接入");
            confine(slot, 0);
            result.stop_movement = true;
            append(result, EffectKind::returned_home, slot, -1, static_cast<std::int32_t>(destination),
                   node_id);
        } else
            state.flags |= 0x80U;
    }
    return result;
}

Result turn(World &world, Random &random, std::size_t slot, const Callbacks &callbacks) {
    auto &state = actor(world, slot);
    Result result;
    if (state.inactive_state)
        return result;
    age_status(state);
    if (state.days_sleeping || state.days_stopping)
        return result;
    result.planned_steps = state.days_tortoise_walking ? 1U : static_cast<unsigned>(random.next() % 9) + 2;
    for (unsigned step = 0; step < result.planned_steps; ++step) {
        if (callbacks.game_over && callbacks.game_over())
            break;
        const auto node = move_one_step(world, random, slot);
        ++result.moved_steps;
        auto landing = interact(world, random, slot, step + 1 == result.planned_steps, callbacks);
        result.effects.insert(result.effects.end(), landing.effects.begin(), landing.effects.end());
        if (callbacks.after_step)
            callbacks.after_step(slot, node);
        if (!landing.supported) {
            result.supported = false;
            result.message = std::move(landing.message);
            break;
        }
        if (landing.stop_movement || state.inactive_state) {
            result.stop_movement = true;
            break;
        }
    }
    return result;
}
} // namespace rich4::special_actors
