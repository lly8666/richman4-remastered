// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/cards.hpp"
#include "rich4/mod_rules.hpp"
#include "rich4/calendar.hpp"
#include "rich4/legacy_float.hpp"
#include "rich4/services.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <string_view>

namespace rich4 {
namespace {
using Kind = CardTargetKind;
std::int32_t add32(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) + std::uint32_t(b));
}
std::int32_t mul32(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) * std::uint32_t(b));
}
std::string_view name(const std::string &value) {
    return std::string_view(value).substr(0, value.find('\0'));
}
bool land_code(const World &w, std::uint16_t code) {
    return code > 2000 && code < 4000 && code - 2000U < w.lands.size();
}
bool facility_code(const World &w, std::uint16_t code) {
    return code > 4000 && code < 6000 && code - 4000U < w.facilities.size();
}
bool property_code(const World &w, std::uint16_t code) {
    return land_code(w, code) || facility_code(w, code);
}
std::uint16_t standing_property(const World &w, std::size_t actor) {
    const auto node = w.players.at(actor).state.node_id;
    return node < w.nodes.size() ? w.nodes[node].type_and_index : 0;
}
struct Property {
    std::uint8_t &owner, &level, &type, &temporary;
    std::uint32_t &expiry;
    std::uint16_t base, building;
};
Property property(World &w, std::uint16_t code) {
    if (land_code(w, code)) {
        auto &p = w.lands[code - 2000];
        return {p.economy.owner_code, p.economy.level, p.economy.chain_store, p.temporary_state,
                p.expired_date,       p.land_price,    p.price_per_level};
    }
    auto &p = w.facilities.at(code - 4000);
    return {p.owner_code,   p.level,      p.facility_type,    p.temporary_state,
            p.expired_date, p.land_price, p.price_per_level()};
}
std::uint8_t owner(const World &w, std::uint16_t code) {
    return land_code(w, code) ? w.lands[code - 2000].economy.owner_code
                              : w.facilities.at(code - 4000).owner_code;
}
std::uint8_t level(const World &w, std::uint16_t code) {
    return land_code(w, code) ? w.lands[code - 2000].economy.level : w.facilities.at(code - 4000).level;
}
std::int32_t cost(const World &w, std::uint16_t code) {
    if (land_code(w, code)) {
        const auto &p = w.lands[code - 2000];
        return mul32(add32(p.land_price, mul32(p.price_per_level, p.economy.level)), w.header.price_index);
    }
    const auto &p = w.facilities.at(code - 4000);
    return mul32(add32(p.land_price, mul32(p.price_per_level(), p.level)), w.header.price_index);
}
bool needs_target(std::uint8_t id) {
    return id == 2 || id == 4 || id == 5 || id == 6 || (id >= 9 && id <= 14) || id == 16 || id == 17 ||
           (id >= 24 && id <= 30);
}
bool present(const World &w, std::size_t slot) {
    if (slot < 4)
        return slot < w.players.size() && w.players[slot].state.controller_flags && w.players[slot].state.x;
    return slot < 8 && !w.special_actors[slot - 4].inactive_state && w.special_actors[slot - 4].x;
}
bool visible(const std::vector<std::uint16_t> &instances, CardTarget t) {
    return std::any_of(instances.begin(), instances.end(), [=](auto code) {
        if (t.kind == Kind::actor)
            return (code & 0x8000U) && (code & (1U << t.value));
        if (t.kind == Kind::object)
            return (code & 0x8000U) && ((code >> 8U) & 0x7fU) == t.value;
        return code == t.value;
    });
}
void hostility(World &w, const CardCallbacks &cb, std::size_t from, std::size_t to, std::int32_t amount) {
    if (cb.hostility) {
        cb.hostility(from, to, amount);
        return;
    }
    if (from == to || from >= w.players.size() || to >= w.players.size())
        return;
    auto &p = w.players[from].status;
    if (amount < 0 && !p.hostility[to])
        return;
    const auto total = add32(std::bit_cast<std::int32_t>(p.hostility[to]), amount);
    p.hostility[to] = total < 0 ? 0 : std::uint32_t(total);
    if (amount > 0 && p.allied_player == to + 1) {
        p.allied_player = 0;
        p.allied_days = 0;
        w.players[to].status.allied_player = 0;
        w.players[to].status.allied_days = 0;
    }
}
std::int32_t double_argument_low_word(double value) {
    // Three original handlers push a double, but 0x40df69 reads an int32.
    return std::bit_cast<std::int32_t>(std::uint32_t(std::bit_cast<std::uint64_t>(value)));
}
std::int32_t property_hostility(std::int32_t indexed_base, std::uint8_t level) {
    using E = numeric::Extended;
    const auto value = E::from(indexed_base) * ((E::from(level) + E::from(2)) / E::from(5));
    return double_argument_low_word(value.double_precision());
}
void consume(World &w, std::size_t actor, std::uint8_t id) {
    (void)consume_card(w.players[actor].state.cards, w.card_supply, id);
}
std::vector<std::uint16_t> same_street(const World &w, std::uint16_t code) {
    if (facility_code(w, code))
        return {code};
    std::vector<std::uint16_t> result;
    const auto street = name(w.lands.at(code - 2000).economy.street_name);
    for (std::size_t i = 1; i < w.lands.size(); ++i)
        if (name(w.lands[i].economy.street_name) == street)
            result.push_back(std::uint16_t(2000 + i));
    return result;
}
bool summonable(const MapObject &object) {
    return object.kind && ((object.kind <= 12 && object.kind != 11) || object.kind == 15) &&
           !object.attached_player && object.node_id;
}
std::optional<std::uint16_t> nearest_god(const World &w, std::size_t actor, const CardCallbacks &cb) {
    if (!cb.nearby_instances)
        return std::nullopt;
    float minimum = 10000;
    std::optional<std::uint16_t> chosen;
    for (auto code : cb.nearby_instances(actor)) {
        const auto id = std::uint16_t((code >> 8U) & 0x7fU);
        if (!(code & 0x8000U) || !id || id > w.objects.size())
            continue;
        const auto &object = w.objects[id - 1];
        if (!summonable(object) || object.node_id >= w.nodes.size())
            continue;
        const auto &node = w.nodes[object.node_id];
        const auto &p = w.players[actor].state;
        const auto dx = std::int32_t(node.x) - p.x, dy = std::int32_t(node.y) - p.y;
        const auto distance = float(std::sqrt(double(add32(mul32(dx, dx), mul32(dy, dy)))));
        if (distance < minimum) {
            minimum = distance;
            chosen = id;
        }
    }
    return chosen;
}
void sleepwalk(World &w, std::size_t target, std::uint8_t days, bool add_total) {
    auto &p = w.players.at(target);
    p.status.days_sleep_walking = days;
    if (add_total)
        p.status.total_winter_sleep_days += 5;
    p.status.flags_102 = p.state.traffic_method;
    p.status.flags_103 = p.state.dice_count;
    if (p.state.traffic_method == 1)
        ++p.state.tool_amounts[4];
    if (p.state.traffic_method == 2)
        ++p.state.tool_amounts[5];
    // Initial sleepwalk leaves a walking player's configured dice unchanged;
    // revenge always resets it, exactly as the two original branches differ.
    if (p.state.traffic_method || !add_total) {
        p.state.traffic_method = 0;
        p.state.dice_count = 1;
    }
}
void break_ally(World &w, std::size_t target) {
    auto &p = w.players[target].status;
    if (p.allied_player && p.allied_player <= w.players.size()) {
        auto &other = w.players[p.allied_player - 1].status;
        other.allied_player = 0;
        other.allied_days = 0;
    }
    p.allied_player = 0;
    p.allied_days = 0;
}
// Card speech (rich4_card_*.asm -> _rich4_player_say): presentation only.
void say(const CardCallbacks &cb, std::size_t speaker, std::uint8_t row, std::uint8_t card,
         std::uint8_t mood) {
    if (cb.say)
        cb.say(speaker, row, card, mood);
}
CardResult result(CardUseStatus status, const char *message) {
    return {status, message};
}
} // namespace

bool active_card(std::uint8_t id) noexcept {
    return id >= 1 && id <= 30 && !(id >= 18 && id <= 21);
}

std::vector<CardTarget> legal_card_targets(const World &w, std::size_t actor, std::uint8_t id,
                                           const CardCallbacks &cb) {
    std::vector<CardTarget> targets;
    if (actor >= w.players.size() || !active_card(id))
        return targets;
    if (id == 24 || id == 25) {
        if (cb.market_closed && cb.market_closed())
            return targets;
        // Human card mode does not share ordinary trading's suspension or
        // daily-price-limit checks (0x42b0da).
        for (std::uint16_t i = 0; i < w.stocks.size(); ++i)
            targets.push_back({Kind::stock, i});
        return targets;
    }
    const bool actor_target = id == 2 || id == 6 || id == 13 || id == 14 || id == 16 || id == 17 ||
                              id == 26 || id == 29 || id == 30;
    if (actor_target) {
        for (std::uint16_t slot = 0; slot < 8; ++slot) {
            if (!present(w, slot))
                continue;
            if ((id == 2 || id == 13 || id == 26 || id == 29) && (slot >= 4 || slot == actor))
                continue;
            if ((id == 16 || id == 17) && slot == actor)
                continue;
            targets.push_back({Kind::actor, slot});
        }
    } else if (id == 4 || id == 5 || id == 9 || id == 10 || id == 11 || id == 12 || id == 27 || id == 28) {
        const auto standing = standing_property(w, actor);
        for (std::size_t kind = 0; kind < 2; ++kind) {
            const auto count = kind ? w.facilities.size() : w.lands.size();
            for (std::size_t i = 1; i < count; ++i) {
                const auto code = std::uint16_t((kind ? 4000 : 2000) + i);
                if ((id == 4 || id == 5) &&
                    (code == standing || (kind ? !facility_code(w, standing) : !land_code(w, standing))))
                    continue;
                if ((id == 11 || id == 12) && (owner(w, code) == actor + 1 || !level(w, code)))
                    continue;
                targets.push_back({Kind::property, code});
            }
        }
        if (id == 12)
            for (std::size_t i = 0; i < w.objects.size(); ++i) {
                const auto &object = w.objects[i];
                if (object.node_id && object.kind >= 16 && object.kind <= 18)
                    targets.push_back({Kind::object, std::uint16_t(i + 1)});
            }
    }
    if (cb.nearby_instances) {
        const auto instances = cb.nearby_instances(actor);
        std::erase_if(targets, [&](auto t) { return !visible(instances, t); });
    }
    return targets;
}

CardResult apply_card(World &w, Random &random, std::size_t actor, const CardUse &use,
                      const CardCallbacks &cb) {
    (void)random; // All random effects are delegated to their original owning rule.
    const auto id = use.card_id;
    if (actor >= w.players.size() || !w.players[actor].state.controller_flags || !active_card(id) ||
        !has_card(w.players[actor].state.cards, id))
        return result(CardUseStatus::invalid, "Card is not an active card in this hand");
    auto selected = use.target;
    if ((id == 24 || id == 25) && !cb.market_closed)
        return result(CardUseStatus::missing_callback, "Stock market calendar is required");
    if ((id == 24 || id == 25) && cb.market_closed())
        return result(CardUseStatus::cancelled, "Stock market is closed");
    if (needs_target(id)) {
        const auto options = legal_card_targets(
            w, actor, id, w.players[actor].state.controller_flags == 1 ? cb : CardCallbacks{});
        if (options.empty())
            return result(CardUseStatus::cancelled, "No legal target");
        if (!selected) {
            if (w.players[actor].state.controller_flags != 1)
                return result(CardUseStatus::cancelled, "Computer card parameter has no target");
            if (!cb.choose_target)
                return result(CardUseStatus::missing_callback, "Target selection is required");
            selected = cb.choose_target(id, options);
            if (!selected)
                return result(CardUseStatus::cancelled, "Target selection cancelled");
        }
        if (std::find(options.begin(), options.end(), *selected) == options.end())
            return result(CardUseStatus::invalid, "Illegal card target");
    }
    const auto target = selected ? selected->value : std::uint16_t(0);
    const auto index = w.header.price_index;
    const auto standing = standing_property(w, actor);
    auto &player = w.players[actor];
    const auto used = [&] { return result(CardUseStatus::used, "Card used"); };
    const auto missing = [] {
        return result(CardUseStatus::missing_callback, "Required card rule callback is unavailable");
    };
    switch (id) {
    case 1: {
        __int128 sum = 0;
        std::int64_t count = 0;
        for (const auto &p : w.players)
            if (p.state.controller_flags) {
                sum += p.state.cash;
                ++count;
            }
        const auto average = static_cast<std::int64_t>(sum / count);
        consume(w, actor, id);
        say(cb, actor, 0, id, 3);
        for (std::size_t i = 0; i < w.players.size(); ++i)
            if (w.players[i].state.controller_flags) {
                auto &cash = w.players[i].state.cash;
                if (cash > average)
                    hostility(w, cb, i, actor, mod::display_i32(mod::money_sub(cash, average) / 100));
                cash = average;
            }
        return used();
    }
    case 2: {
        const auto average = static_cast<std::int64_t>(
            (__int128(player.state.cash) + w.players[target].state.cash) / 2);
        consume(w, actor, id);
        say(cb, actor, 0, id, 3);
        if (w.players[target].state.cash > average)
            hostility(w, cb, target, actor,
                      mod::display_i32(mod::money_sub(w.players[target].state.cash, average) / 100));
        player.state.cash = average;
        w.players[target].state.cash = average;
        say(cb, target, 2, id, 1);
        return used();
    }
    case 3: {
        if (!property_code(w, standing) || !owner(w, standing) || owner(w, standing) == actor + 1 ||
            cost(w, standing) > player.state.cash)
            return result(CardUseStatus::cancelled, "Standing property cannot be purchased");
        if (!cb.pay)
            return missing();
        auto p = property(w, standing);
        const auto seller = p.owner - 1;
        const auto price = cost(w, standing);
        hostility(w, cb, std::size_t(seller), actor, property_hostility(mul32(p.base, index), p.level));
        say(cb, actor, 0, id, 3);
        p.owner = std::uint8_t(actor + 1);
        if (w.lease_mode) {
            constexpr std::array<std::uint32_t, 6> intervals{0, 0x20000, 0x10000, 0x600, 0x300, 0x100};
            if (w.lease_mode < intervals.size())
                p.expiry = calendar::add_packed_interval(
                    calendar::pack({w.header.date.year, w.header.date.month, w.header.date.day}),
                    intervals[w.lease_mode]);
        }
        cb.pay(std::int32_t(actor), seller, price, 0);
        say(cb, std::size_t(seller), 2, id, 1);
        consume(w, actor, id);
        return used();
    }
    case 4:
    case 5: {
        auto a = property(w, standing), b = property(w, target);
        // rich4_card_huandika/huanwuka: the user speaks only when the swap favours it (its own
        // property for one at least as built up); the other owner then complains.
        const unsigned mine = unsigned(actor) + 1, owner_a = a.owner, owner_b = b.owner;
        const auto level_a = a.level, level_b = b.level;
        const bool gain_b = owner_a == mine && owner_b != mine && level_b >= level_a;
        const bool gain_a = owner_a != mine && owner_b == mine && level_a >= level_b;
        if (gain_a || gain_b)
            say(cb, actor, 0, id, 3);
        if (id == 4)
            std::swap(a.owner, b.owner);
        else {
            std::swap(a.level, b.level);
            std::swap(a.type, b.type);
        }
        if (gain_b && owner_b)
            say(cb, owner_b - 1, 2, id, 2);
        else if (gain_a && owner_a)
            say(cb, owner_a - 1, 2, id, 2);
        consume(w, actor, id);
        return used();
    }
    case 6:
        if (!cb.turn_around)
            return missing();
        consume(w, actor, id);
        say(cb, actor, 0, id, 3);
        cb.turn_around(target);
        if (target == actor)
            say(cb, actor, 1, id, 0);
        else if (target < 4)
            say(cb, target, 2, id, 2);
        return used();
    case 7: {
        if (!property_code(w, standing) || !level(w, standing))
            return result(CardUseStatus::cancelled, "Standing property has no building");
        say(cb, actor, 0, id, 3); // Before the facility type choice, even if it is then cancelled.
        auto p = property(w, standing);
        if (land_code(w, standing)) {
            p.type ^= 1;
            if (p.type && p.level > 1)
                p.level = 1;
        } else {
            std::optional<std::uint8_t> type;
            if (use.choice)
                type = std::uint8_t(use.choice->value);
            else if (cb.choose_facility_type)
                type = cb.choose_facility_type(actor);
            else
                return missing();
            if (!type)
                return result(CardUseStatus::cancelled, "Reconstruction cancelled");
            if (*type > 4)
                return result(CardUseStatus::invalid, "Invalid facility type");
            p.type = *type;
            if ((p.type == 0 || p.type == 3) && p.level > 1)
                p.level = 1;
        }
        consume(w, actor, id);
        return used();
    }
    case 8: {
        if (!property_code(w, standing))
            return result(CardUseStatus::cancelled, "No standing property");
        if (!cb.auction_property)
            return missing();
        auto p = property(w, standing);
        if (p.owner)
            hostility(w, cb, p.owner - 1, actor, property_hostility(mul32(p.base, index), p.level));
        say(cb, actor, 0, id, 3);
        if (p.owner && p.owner != actor + 1)
            say(cb, p.owner - 1U, 2, id, 1);
        if (!cb.auction_property(standing, std::int32_t(actor))) {
            p.owner = 0;
            p.expiry = 0;
        }
        consume(w, actor, id);
        return used();
    }
    case 9:
    case 10:
    case 11:
    case 12: {
        if (selected->kind == Kind::object) {
            if (!cb.remove_object)
                return missing();
            consume(w, actor, id);
            say(cb, actor, 0, id, 0);
            cb.remove_object(target);
            return used();
        }
        if (facility_code(w, target) && id == 9 && !cb.change_building)
            return missing();
        if (facility_code(w, target) && id != 9 && level(w, target) && !cb.release_hotel)
            return missing();
        consume(w, actor, id);
        say(cb, actor, 0, id, id == 9 ? 3 : 0);
        const auto victim = owner(w, target); // Monster/demolish: the owner speaks after the picture.
        const auto properties =
            (id == 9 || id == 10) ? same_street(w, target) : std::vector<std::uint16_t>{target};
        for (auto code : properties) {
            auto p = property(w, code);
            if (id == 9) {
                if (facility_code(w, code))
                    cb.change_building(code, true);
                else if (p.level < 5) {
                    if (!p.type)
                        ++p.level;
                    else if (!p.level)
                        p.level = 1;
                }
            } else {
                if (p.owner)
                    hostility(w, cb, p.owner - 1, actor, mul32(id == 12 ? 30 : 30 * p.level, index));
                const auto previous = p.level;
                if (id == 12 && !p.type)
                    --p.level;
                else if (id == 12 && facility_code(w, code))
                    --p.level;
                else
                    p.level = 0;
                if (!p.level)
                    p.type = 0;
                if (facility_code(w, code) && previous && !p.level)
                    cb.release_hotel(code);
            }
        }
        if ((id == 11 || id == 12) && victim)
            say(cb, victim - 1U, 2, id, 1);
        return used();
    }
    case 13: {
        say(cb, actor, 0, id, 3); // Before the inventory pick, even if it is then cancelled.
        auto item = use.choice;
        if (!item) {
            if (!cb.choose_inventory)
                return missing();
            item = cb.choose_inventory(actor, target);
        }
        if (!item)
            return result(CardUseStatus::cancelled, "Inventory selection cancelled");
        auto &victim = w.players[target].state;
        if (item->kind == Kind::card && item->value >= 1 && item->value <= 30 &&
            has_card(victim.cards, std::uint8_t(item->value))) {
            consume(w, target, std::uint8_t(item->value));
            receive_card(player.state.cards, w.card_supply, services::card_prices, std::uint8_t(item->value));
        } else if (item->kind == Kind::tool && item->value >= 1 && item->value <= 13 &&
                   victim.tool_amounts[item->value - 1]) {
            (void)consume_tool(victim.tool_amounts, w.tool_supply, std::uint8_t(item->value));
            (void)receive_tool(player.state.tool_amounts, w.tool_supply, std::uint8_t(item->value));
        } else
            return result(CardUseStatus::invalid, "Item is absent from target inventory");
        // Original 0x44192a returns a bare tool ID; the caller still indexes the card-price table.
        hostility(w, cb, target, actor, services::card_prices[item->value - 1]);
        consume(w, actor, id);
        if (cb.stolen)
            cb.stolen(actor, target, *item, use.choice.has_value());
        say(cb, target, 2, id, 1);
        return used();
    }
    case 14:
    case 30:
        consume(w, actor, id);
        if (target != actor)
            say(cb, actor, 0, id, 3);
        if (target < 4) // rich4_card_tingliuka/wuguika: self row 1 (mood 3), victim row 2 (mood 2).
            say(cb, target, target == actor ? 1 : 2, id, target == actor ? 3 : 2);
        if (target < 4) {
            if (id == 14)
                w.players[target].status.days_stopping = target == actor ? 0x80 : 1;
            else
                w.players[target].status.days_tortoise_walking = target == actor ? 2 : 3;
        } else {
            if (id == 14)
                w.special_actors[target - 4].days_stopping = 1;
            else
                w.special_actors[target - 4].days_tortoise_walking = 3;
        }
        return used();
    case 15:
        consume(w, actor, id);
        say(cb, actor, 0, id, 3);
        for (std::size_t i = 0; i < w.players.size(); ++i) {
            auto &p = w.players[i];
            if (i == actor || !p.state.controller_flags || !p.state.x || p.status.days_in_hotel)
                continue;
            hostility(w, cb, i, actor, mul32(150, index));
            p.status.days_sleep_walking = 0;
            p.status.days_sleeping = 5;
            p.status.total_winter_sleep_days += 5;
        }
        for (std::size_t i = 0; i < 4; ++i)
            if (!w.special_actors[i].inactive_state) {
                w.special_actors[i].days_sleep_walking = 0;
                w.special_actors[i].days_sleeping = 5;
            }
        return used();
    case 16:
    case 17: {
        if (id == 17 && !cb.add_prison)
            return missing();
        if (target < 4 &&
            (has_card(w.players[target].state.cards, 21) || has_card(w.players[target].state.cards, 19)) &&
            !cb.try_mianzui_jiahuo)
            return missing();
        consume(w, actor, id);
        say(cb, actor, 0, id, 3);
        if (target >= 4) {
            if (id == 17)
                cb.add_prison(target, 5);
            else if (!w.special_actors[target - 4].days_sleeping)
                w.special_actors[target - 4].days_sleep_walking = 5;
            return used();
        }
        if (id == 16 && w.players[target].status.days_sleeping)
            return used();
        hostility(w, cb, target, actor, mul32(150, index));
        const auto redirected = cb.try_mianzui_jiahuo ? cb.try_mianzui_jiahuo(target) : std::int32_t(target);
        if (redirected < 0)
            return used();
        if (std::size_t(redirected) >= w.players.size())
            return result(CardUseStatus::invalid, "Defense returned invalid player");
        if (id == 16) {
            if (cb.voice) // rich4_card_mengyouka: the victim says event line 21 (mood 1) first.
                cb.voice(std::size_t(redirected), 21);
            sleepwalk(w, std::size_t(redirected), redirected == std::int32_t(actor) ? 4 : 5, true);
        } else {
            cb.add_prison(std::size_t(redirected), redirected == std::int32_t(actor) ? 4 : 5);
            say(cb, std::size_t(redirected), 2, id, 1);
        }
        if (redirected == target && has_card(w.players[target].state.cards, 18)) {
            // rich4_card_passive _rich4_use_card_fuchouka: popup, consume, holder then user speak.
            if (cb.passive_card)
                cb.passive_card(target, 18);
            consume(w, target, 18);
            say(cb, target, 0, 18, 0);
            say(cb, actor, 2, 18, 2);
            if (id == 16)
                sleepwalk(w, actor, 5, false);
            else
                cb.add_prison(actor, 5);
        }
        return used();
    }
    case 22: {
        const auto bomb = player.status.attached_bomb, god = player.status.god_info;
        const auto kind = god && god <= w.objects.size() ? w.objects[god - 1].kind : 0;
        const bool bad = kind == 5 || kind == 6 || kind == 7 || kind == 8 || kind == 10 || kind == 15;
        if (!bomb && !bad)
            return result(CardUseStatus::cancelled, "No attached bomb or removable god");
        if ((bomb && !cb.remove_object) || (bad && !cb.detach_god))
            return missing();
        if (bomb)
            cb.remove_object(bomb);
        if (bad)
            cb.detach_god(actor);
        consume(w, actor, id);
        say(cb, actor, 0, id, 0);
        return used();
    }
    case 23: {
        if (!cb.nearby_instances || !cb.attach_god)
            return missing();
        const auto object = nearest_god(w, actor, cb);
        if (!object)
            return result(CardUseStatus::cancelled, "No nearby attachable god");
        consume(w, actor, id);
        say(cb, actor, 0, id, 0);
        cb.attach_god(actor, *object);
        return used();
    }
    case 24:
    case 25: {
        if (!cb.update_stocks)
            return missing();
        say(cb, actor, 0, id, 0);
        const float old_price = std::bit_cast<float>(w.stocks[target].float_bits[2]);
        w.stocks[target].news_movement = id == 24 ? 0x20 : 2;
        cb.update_stocks(target + 1);
        if (id == 25) {
            const float difference = old_price - std::bit_cast<float>(w.stocks[target].float_bits[2]);
            for (std::size_t i = 0; i < w.players.size(); ++i)
                if (w.players[i].stocks[target].amount)
                    hostility(w, cb, i, actor,
                              double_argument_low_word(
                                  ((numeric::Extended::from(w.players[i].stocks[target].amount) *
                                    numeric::Extended::from(difference)) /
                                   numeric::Extended::from(200))
                                      .double_precision()));
        }
        consume(w, actor, id);
        return used();
    }
    case 26: {
        if (!cb.pay)
            return missing();
        if (has_card(w.players[target].state.cards, 20) && !cb.try_free)
            return missing();
        const auto tax = w.players[target].state.cash / 5;
        if (has_card(w.players[target].state.cards, 19) && tax > 2000 && !cb.try_frame)
            return missing();
        consume(w, actor, id);
        say(cb, actor, 0, id, 0);
        hostility(w, cb, target, actor, mod::display_i32(tax / 100));
        if (has_card(w.players[target].state.cards, 20) &&
            cb.try_free(target, actor, mod::display_i32(tax)))
            return used();
        auto payer = std::int32_t(target);
        if (has_card(w.players[target].state.cards, 19) && tax > 2000) {
            const auto redirect = cb.try_frame(target, 2);
            if (redirect != -1)
                payer = redirect;
        }
        if (payer != std::int32_t(actor)) {
            cb.pay(payer, std::int32_t(actor), w.players.at(std::size_t(payer)).state.cash / 5, 0);
            say(cb, std::size_t(payer), 2, id, 2);
        }
        return used();
    }
    case 27:
    case 28:
        consume(w, actor, id);
        say(cb, actor, 0, id, 0);
        for (auto code : same_street(w, target)) {
            property(w, code).temporary = id == 27 ? 0x50 : 0x51;
            if (id == 28 && facility_code(w, code) && w.facilities[code - 4000].facility_type == 4)
                w.facilities[code - 4000].flags_30 = 0;
        }
        return used();
    case 29:
        consume(w, actor, id);
        say(cb, actor, 0, id, 3);
        break_ally(w, actor);
        break_ally(w, target);
        player.status.allied_player = std::uint8_t(target + 1);
        player.status.allied_days = 7;
        w.players[target].status.allied_player = std::uint8_t(actor + 1);
        w.players[target].status.allied_days = 7;
        say(cb, target, 2, id, 0);
        return used();
    default:
        return result(CardUseStatus::invalid, "Passive or unknown card");
    }
}

} // namespace rich4
