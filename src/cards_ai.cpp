// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/cards.hpp"
#include "rich4/mod_rules.hpp"
#include "rich4/legacy_float.hpp"
#include "rich4/services.hpp"
#include "rich4/stocks.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <string_view>

namespace rich4 {
namespace {
using Kind = CardTargetKind;
constexpr std::array<std::uint8_t, 30> aggression{2, 2, 1, 0, 0, 0, 0, 1, 0, 2, 2, 1, 2, 0, 2,
                                                  1, 2, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 0, 0};
std::int32_t add(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) + std::uint32_t(b));
}
std::int32_t mul(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) * std::uint32_t(b));
}
std::string_view name(const std::string &s) {
    return std::string_view(s).substr(0, s.find('\0'));
}
bool land(const World &w, unsigned code) {
    return code > 2000 && code < 4000 && code - 2000 < w.lands.size();
}
bool facility(const World &w, unsigned code) {
    return code > 4000 && code < 6000 && code - 4000 < w.facilities.size();
}
bool commercial(const World &w, unsigned code) {
    return code > 6000 && code < 8000 && code - 6000 < w.commercials.size();
}
unsigned node_code(const World &w, unsigned node) {
    return node < w.nodes.size() ? w.nodes[node].type_and_index : 0;
}
unsigned standing(const World &w, std::size_t slot) {
    return node_code(w, w.players[slot].state.node_id);
}
unsigned owner(const World &w, unsigned code) {
    return land(w, code)       ? w.lands[code - 2000].economy.owner_code
           : facility(w, code) ? w.facilities[code - 4000].owner_code
                               : 0;
}
unsigned level(const World &w, unsigned code) {
    return land(w, code)       ? w.lands[code - 2000].economy.level
           : facility(w, code) ? w.facilities[code - 4000].level
                               : 0;
}
unsigned base(const World &w, unsigned code) {
    return land(w, code) ? w.lands[code - 2000].land_price : w.facilities[code - 4000].land_price;
}
int enemy(const World &w, std::size_t actor) {
    std::int32_t maximum = 0;
    int result = -1;
    for (std::size_t i = 0; i < w.players.size(); ++i)
        if (i != actor && w.players[i].state.controller_flags) {
            const auto value = std::bit_cast<std::int32_t>(w.players[actor].status.hostility[i]);
            if (value > maximum) {
                maximum = value;
                result = int(i);
            }
        }
    return result;
}
std::uint32_t actor_code(unsigned actor) {
    return 0x8000U | (1U << actor);
}
std::vector<std::size_t> scanned_players(const World &w, std::size_t actor,
                                         const std::vector<std::uint16_t> &scan) {
    std::vector<std::size_t> result;
    for (auto code : scan)
        if (code & 0x8000U)
            for (std::size_t i = 0; i < w.players.size(); ++i)
                if (i != actor && (code & (1U << i)) && w.players[i].state.controller_flags)
                    result.push_back(i);
    return result;
}
bool desirable_property(const World &w, std::size_t actor, int foe, unsigned code) {
    if (foe < 0)
        return false; // Original helper exits before its otherwise general facility branch.
    if (land(w, code)) {
        const auto &p = w.lands[code - 2000];
        if (p.economy.owner_code && p.economy.owner_code != actor + 1 && p.economy.level)
            for (std::size_t i = 1; i < w.lands.size(); ++i)
                if (name(w.lands[i].economy.street_name) == name(p.economy.street_name) &&
                    w.lands[i].economy.owner_code == actor + 1)
                    return true;
        return p.economy.owner_code == unsigned(foe + 1) && p.economy.level >= 2;
    }
    return facility(w, code) && owner(w, code) && owner(w, code) != actor + 1 && level(w, code);
}
std::int32_t acquisition_cost(const World &w, unsigned code) {
    const unsigned building =
        land(w, code) ? w.lands[code - 2000].price_per_level : w.facilities[code - 4000].price_per_level();
    return mul(add(std::int32_t(base(w, code)), mul(std::int32_t(level(w, code)), std::int32_t(building))),
               w.header.price_index);
}
std::int32_t toll(const World &w, unsigned code) {
    std::vector<LandEconomyRecord> records;
    records.reserve(w.lands.size());
    for (std::size_t i = 1; i < w.lands.size(); ++i)
        records.push_back(w.lands[i].economy);
    const auto &p = w.lands[code - 2000].economy;
    // These AI callers always pass land+4, even for chain-store land.
    return land_toll(records, p.owner_code, w.header.price_index, name(p.street_name));
}
std::uint16_t monster_target(const World &w, std::size_t actor, const std::vector<std::uint16_t> &scan) {
    std::array<std::array<std::uint16_t, 4>, 2> best{};
    for (auto code : scan) {
        const bool f = facility(w, code);
        if (!f && !land(w, code))
            continue;
        const auto who = owner(w, code);
        if (!who || who == actor + 1 || who > 4 || level(w, code) < 3)
            continue;
        auto &current = best[f ? 1 : 0][who - 1];
        if (!current || level(w, code) > level(w, current) ||
            (level(w, code) == level(w, current) && base(w, code) > base(w, current)))
            current = code;
    }
    const int foe = enemy(w, actor);
    if (foe >= 0) {
        const auto slot = std::size_t(foe);
        if (best[1][slot])
            return best[1][slot];
        if (best[0][slot])
            return best[0][slot];
    }
    std::array<std::uint16_t, 2> selected{};
    for (std::size_t i = 0; i < w.players.size(); ++i)
        if (i != actor && w.players[i].state.controller_flags)
            for (std::size_t kind = 0; kind < 2; ++kind) {
                const auto code = best[kind][i];
                if (!code || (!kind && level(w, code) < 4))
                    continue;
                auto &current = selected[kind];
                if (!current || level(w, code) > level(w, current) ||
                    (level(w, code) == level(w, current) && base(w, code) > base(w, current)))
                    current = code;
            }
    return selected[1] ? selected[1] : selected[0];
}
std::optional<std::uint16_t> nearest_god(const World &w, std::size_t actor,
                                         const std::vector<std::uint16_t> &scan) {
    float minimum = 10000;
    std::optional<std::uint16_t> chosen;
    for (auto code : scan) {
        const auto id = std::uint16_t((code >> 8U) & 0x7fU);
        if (!(code & 0x8000U) || !id || id > w.objects.size())
            continue;
        const auto &o = w.objects[id - 1];
        if (!o.node_id || o.node_id >= w.nodes.size() || o.attached_player ||
            !((o.kind <= 12 && o.kind != 11) || o.kind == 15))
            continue;
        const auto &n = w.nodes[o.node_id];
        const auto &p = w.players[actor].state;
        const int dx = int(n.x) - p.x, dy = int(n.y) - p.y;
        const float distance = float(std::sqrt(double(add(mul(dx, dx), mul(dy, dy)))));
        if (distance < minimum) {
            minimum = distance;
            chosen = id;
        }
    }
    return chosen;
}
bool friendly_god_id(unsigned id) {
    return id == 1 || id == 2 || id == 3 || id == 4 || id == 12;
}
std::int32_t holding_cost(const StockHolding &stock) {
    return (numeric::Extended::from(stock.amount) *
            numeric::Extended::from(std::bit_cast<float>(stock.raw_cost_basis)))
        .integer();
}

// One per-card predicate. Parameters retain their original persistent lifetime;
// unsuccessful predicates need not overwrite them. The UI's traversal index is
// preserved for the original price-rise predicate's ESI assignment.
bool predicate(const World &w, Random &random, std::size_t actor, unsigned id, const CardCallbacks &cb,
               CardAiMemory &memory, std::size_t traversal_end) {
    const auto &player = w.players[actor];
    const auto current = standing(w, actor);
    const int foe = enemy(w, actor);
    const auto index = w.header.price_index;
    const auto scan = [&] {
        auto values = cb.nearby_instances ? cb.nearby_instances(actor) : std::vector<std::uint16_t>{};
        memory.parameter2 = std::uint32_t(values.size());
        return values;
    };
    const auto target = [&](unsigned code) {
        memory.parameter0 = code;
        return true;
    };
    switch (id) {
    case 1: {
        __int128 sum = 0;
        std::int32_t n = 0;
        for (const auto &p : w.players)
            if (p.state.controller_flags) {
                sum += p.state.cash;
                ++n;
            }
        return n && sum / n > __int128(player.state.cash) * 10 &&
               player.state.cash < mul(index, 3000);
    }
    case 2:
    case 26: {
        const auto candidates = scanned_players(w, actor, scan());
        const auto qualifies = [&](std::size_t i, int threshold, int factor) {
            return w.players[i].state.cash > mul(index, threshold) &&
                   (id == 26 || __int128(w.players[i].state.cash) > __int128(player.state.cash) * factor);
        };
        if (foe >= 0 &&
            std::find(candidates.begin(), candidates.end(), std::size_t(foe)) != candidates.end() &&
            qualifies(std::size_t(foe), 30000, 2))
            return target(actor_code(unsigned(foe)));
        bool found = false;
        for (std::size_t i = 0; i < w.players.size(); ++i)
            if (std::find(candidates.begin(), candidates.end(), i) != candidates.end() &&
                qualifies(i, 50000, 3)) {
                target(actor_code(unsigned(i)));
                found = true;
            }
        return found;
    }
    case 3:
        return desirable_property(w, actor, foe, current) && acquisition_cost(w, current) < player.state.cash;
    case 4: {
        if ((!land(w, current) && !facility(w, current)) || owner(w, current) != actor + 1 ||
            level(w, current) > 1)
            return false;
        if (land(w, current))
            for (std::size_t i = 1; i < w.lands.size(); ++i)
                if (i != current - 2000 &&
                    name(w.lands[i].economy.street_name) ==
                        name(w.lands[current - 2000].economy.street_name) &&
                    w.lands[i].economy.owner_code == actor + 1)
                    return false;
        for (auto code : scan()) {
            if (land(w, current) ? !land(w, code) : !facility(w, code))
                continue;
            if (land(w, current) && name(w.lands[code - 2000].economy.street_name) ==
                                        name(w.lands[current - 2000].economy.street_name))
                continue;
            if (base(w, code) > base(w, current) && level(w, code) > level(w, current) &&
                desirable_property(w, actor, foe, code))
                return target(code);
        }
        return false;
    }
    case 5:
    case 6:
    case 18:
    case 19:
    case 20:
    case 21:
        return false;
    case 7: {
        if (land(w, current)) {
            const auto &p = w.lands[current - 2000].economy;
            if (p.owner_code != actor + 1)
                return false;
            if (p.chain_store) {
                for (std::size_t i = 1; i < w.lands.size(); ++i)
                    if (i != current - 2000 && name(w.lands[i].economy.street_name) == name(p.street_name) &&
                        w.lands[i].economy.owner_code == actor + 1)
                        return true;
                return false;
            }
            if (p.level != 1)
                return false;
            if (!player.status.rescue_strategy)
                return true;
            for (std::size_t i = 1; i < w.lands.size(); ++i)
                if (i != current - 2000 && name(w.lands[i].economy.street_name) == name(p.street_name) &&
                    (!w.lands[i].economy.owner_code || w.lands[i].economy.owner_code == actor + 1))
                    return false;
            return true;
        }
        if (!facility(w, current))
            return false;
        const auto &p = w.facilities[current - 4000];
        if (p.owner_code == actor + 1 && !p.facility_type && p.level == 1) {
            memory.parameter0 = random.next() % 4 + 1;
            return true;
        }
        if (p.owner_code && p.owner_code != actor + 1 && p.facility_type &&
            (p.level >= 3 || (p.owner_code == unsigned(foe + 1) && p.level >= 2))) {
            memory.parameter0 = 0;
            return true;
        }
        return false;
    }
    case 8:
        return (land(w, current) || facility(w, current)) && owner(w, current) &&
               ((owner(w, current) != actor + 1 && level(w, current) >= 3) ||
                (owner(w, current) == unsigned(foe + 1) && level(w, current) >= 2));
    case 9: {
        struct Street {
            std::string_view name;
            std::uint16_t code, count;
        };
        std::vector<Street> streets;
        for (auto code : scan())
            if (land(w, code)) {
                const auto &p = w.lands[code - 2000].economy;
                if (p.owner_code != actor + 1 || p.chain_store || p.level >= 5)
                    continue;
                auto found = std::find_if(streets.begin(), streets.end(),
                                          [&](const auto &s) { return s.name == name(p.street_name); });
                if (found == streets.end())
                    streets.push_back({name(p.street_name), code, 1});
                else
                    ++found->count;
            }
        std::vector<std::uint16_t> choices;
        for (const auto &s : streets)
            if (s.count >= 3)
                choices.push_back(s.code);
        return !choices.empty() && target(choices[random.next() % choices.size()]);
    }
    case 10: {
        struct Street {
            std::string_view name;
            std::uint16_t code;
            std::array<std::uint8_t, 4> levels{}, counts{};
        };
        std::vector<Street> streets;
        for (auto code : scan())
            if (land(w, code)) {
                const auto &p = w.lands[code - 2000].economy;
                if (p.chain_store || !p.owner_code || p.owner_code > 4)
                    continue;
                auto found = std::find_if(streets.begin(), streets.end(),
                                          [&](const auto &s) { return s.name == name(p.street_name); });
                if (found == streets.end()) {
                    streets.push_back({name(p.street_name), code});
                    found = std::prev(streets.end());
                }
                found->levels[p.owner_code - 1] += p.level;
                ++found->counts[p.owner_code - 1];
            }
        for (const auto &s : streets) {
            if (foe >= 0) {
                if (s.counts[std::size_t(foe)] >= 2 && s.levels[std::size_t(foe)] >= 7 &&
                    s.levels[actor] <= 1)
                    return target(s.code);
            } else if (!s.levels[actor]) {
                unsigned levels = 0, count = 0;
                for (std::size_t i = 0; i < w.players.size(); ++i)
                    if (i != actor && w.players[i].state.controller_flags) {
                        levels += s.levels[i];
                        count += s.counts[i];
                    }
                if (count >= 3 && levels >= 9)
                    return target(s.code);
            }
        }
        return false;
    }
    case 11:
    case 12: {
        const auto instances = scan();
        if (const auto monster = monster_target(w, actor, instances))
            return target(monster);
        if (id == 11)
            return false;
        for (auto code : instances) {
            if (land(w, code)) {
                const auto &p = w.lands[code - 2000].economy;
                if (player.status.rescue_strategy && p.owner_code && p.owner_code != actor + 1 &&
                    p.chain_store) {
                    unsigned count = 0;
                    for (std::size_t i = 1; i < w.lands.size(); ++i)
                        if (w.lands[i].economy.owner_code == p.owner_code && w.lands[i].economy.chain_store)
                            ++count;
                    if (count >= 4)
                        return target(code);
                }
            } else if (facility(w, code)) {
                const auto &p = w.facilities[code - 4000];
                if ((player.state.traffic_method & 3) && p.facility_type == 3 && p.level == 1 &&
                    p.owner_code != actor + 1)
                    return target(code);
            } else if (code & 0x8000U) {
                const auto object = (code >> 8U) & 0x3fU;
                if (!object || object > w.objects.size())
                    continue;
                const auto &p = w.objects[object - 1];
                const auto property = node_code(w, p.node_id);
                if (!land(w, property) && !facility(w, property))
                    continue;
                if (p.kind == 16 && owner(w, property) && owner(w, property) != actor + 1)
                    return target(code);
                if (p.kind == 17 && owner(w, property) == actor + 1)
                    return target(code);
            }
        }
        return false;
    }
    case 13: {
        const auto candidates = scanned_players(w, actor, scan());
        const auto best_card = [&](std::size_t victim, unsigned minimum, bool exact) {
            unsigned chosen = 0, price = 0;
            for (auto card : w.players[victim].state.cards)
                if (card && (exact ? aggression[card - 1] == minimum : aggression[card - 1] >= minimum) &&
                    services::card_prices[card - 1] > price) {
                    chosen = card;
                    price = services::card_prices[card - 1];
                }
            return chosen;
        };
        if (foe >= 0 && std::find(candidates.begin(), candidates.end(), std::size_t(foe)) != candidates.end())
            if (const auto card = best_card(std::size_t(foe), 1, false)) {
                memory.parameter1 = card;
                return target(actor_code(unsigned(foe)));
            }
        unsigned best_price = 0;
        bool found = false;
        for (std::size_t i = 0; i < w.players.size(); ++i)
            if (std::find(candidates.begin(), candidates.end(), i) != candidates.end())
                if (const auto card = best_card(i, 2, true);
                    card && services::card_prices[card - 1] > best_price) {
                    best_price = services::card_prices[card - 1];
                    memory.parameter1 = card;
                    target(actor_code(unsigned(i)));
                    found = true;
                }
        return found;
    }
    case 14: {
        if (!player.status.days_tortoise_walking) {
            if (land(w, current)) {
                const auto &p = w.lands[current - 2000];
                if (p.economy.owner_code == actor + 1 && !p.economy.chain_store && p.economy.level < 5 &&
                    mul(p.price_per_level, index) < player.state.cash &&
                    mod::money_add(player.state.cash, player.state.bank_balance) > 10000 &&
                    player.status.money_fortune_modifier >= 0) {
                    for (std::size_t i = 1; i < w.lands.size(); ++i)
                        if (i != current - 2000 &&
                            name(w.lands[i].economy.street_name) == name(p.economy.street_name) &&
                            w.lands[i].economy.owner_code == actor + 1)
                            return target(actor_code(unsigned(actor)));
                    if (p.economy.level >= 2)
                        return true; // Original omits parameter0 assignment here.
                }
            } else if (facility(w, current)) {
                const auto &p = w.facilities[current - 4000];
                if (p.owner_code == actor + 1 && mul(p.price_per_level(), index) < player.state.cash &&
                    player.state.cash > 10000 && p.facility_type && p.facility_type != 3 && p.level < 5 &&
                    player.status.money_fortune_modifier >= 0)
                    return target(actor_code(unsigned(actor)));
            }
        }
        const auto candidates = scanned_players(w, actor, scan());
        for (std::size_t i = 0; i < w.players.size(); ++i)
            if (std::find(candidates.begin(), candidates.end(), i) != candidates.end()) {
                const auto code = standing(w, i);
                if (facility(w, code)) {
                    const auto &p = w.facilities[code - 4000];
                    if (p.owner_code == actor + 1 && p.facility_type && p.level >= 2)
                        return target(actor_code(unsigned(i)));
                } else if (commercial(w, code) && w.commercials[code - 6000].owner_code == actor + 1)
                    return target(actor_code(unsigned(i)));
            }
        return false;
    }
    case 15:
        return random.next() % 4 == 0;
    case 16:
    case 17: {
        std::vector<std::size_t> choices;
        std::optional<std::size_t> most_hostile;
        for (auto candidate : scanned_players(w, actor, scan()))
            if (!w.players[candidate].status.days_sleeping &&
                !has_card(w.players[candidate].state.cards, 18)) {
                if (int(candidate) == foe)
                    most_hostile = choices.size();
                choices.push_back(candidate);
            }
        if (choices.empty())
            return false;
        const auto selected = most_hostile ? *most_hostile : random.next() % choices.size();
        return target(actor_code(unsigned(choices[selected])));
    }
    case 22: {
        const auto god = player.status.god_info;
        if (god && god <= w.objects.size()) {
            const auto kind = w.objects[god - 1].kind;
            return kind == 5 || kind == 6 || kind == 7 || kind == 8 || kind == 10 || kind == 15;
        }
        const auto bomb = player.status.attached_bomb;
        return bomb && bomb <= w.objects.size() && w.objects[bomb - 1].remaining_days < 13;
    }
    case 23: {
        if (friendly_god_id(player.status.god_info))
            return false;
        const auto instances =
            cb.nearby_instances ? cb.nearby_instances(actor) : std::vector<std::uint16_t>{};
        const auto chosen = nearest_god(w, actor, instances);
        return chosen && friendly_god_id(*chosen) && target(*chosen);
    }
    case 24:
    case 25: {
        if (!cb.market_closed || cb.market_closed())
            return false;
        const auto best_stock = [&](std::size_t holder, bool require_company) {
            std::int32_t value = 0;
            int chosen = -1;
            for (std::size_t i = 0; i < w.stocks.size(); ++i) {
                const auto amount = holding_cost(w.players[holder].stocks[i]);
                if (amount <= value || w.stocks[i].suspension_days ||
                    (require_company && !w.stocks[i].flags_4) || (id == 25 && player.stocks[i].amount))
                    continue;
                const auto movement = stocks::trend(w, i);
                if ((id == 24 && movement == stocks::PriceTrend::limit_up) ||
                    (id == 25 && movement == stocks::PriceTrend::limit_down))
                    continue;
                value = amount;
                chosen = int(i);
            }
            return chosen;
        };
        if (id == 24) {
            const auto stock = best_stock(actor, false);
            return stock >= 0 && target(unsigned(stock));
        }
        std::array<std::uint8_t, 4> companies{};
        for (std::size_t i = 1; i < w.commercials.size(); ++i)
            if (w.commercials[i].owner_code && w.commercials[i].owner_code <= 4)
                ++companies[w.commercials[i].owner_code - 1];
        int company_owner = -1;
        unsigned greatest = 0;
        for (std::size_t i = 0; i < 4; ++i)
            if (i != actor && companies[i] > greatest) {
                greatest = companies[i];
                company_owner = int(i);
            }
        const auto business_stock = company_owner >= 0 && std::size_t(company_owner) < w.players.size()
                                        ? best_stock(std::size_t(company_owner), true)
                                        : -1;
        const auto enemy_stock = foe >= 0 ? best_stock(std::size_t(foe), false) : -1;
        const auto chosen = enemy_stock >= 0 ? enemy_stock : business_stock;
        return chosen >= 0 && target(unsigned(chosen));
    }
    case 27: {
        std::string_view previous;
        bool have_previous = false;
        unsigned facility_score = 0;
        std::size_t land_cursor = traversal_end;
        for (auto code : scan()) {
            if (land(w, code)) {
                const auto street = name(w.lands[code - 2000].economy.street_name);
                if (have_previous && street == previous)
                    continue;
                unsigned total = 0, owned = 0, own_levels = 0, other_levels = 0;
                bool enemy_present = false;
                for (land_cursor = 1; land_cursor < w.lands.size(); ++land_cursor) {
                    const auto &p = w.lands[land_cursor].economy;
                    if (name(p.street_name) != street)
                        continue;
                    ++total;
                    if (p.owner_code == actor + 1) {
                        ++owned;
                        own_levels += p.level;
                    } else if (p.owner_code)
                        other_levels += p.level;
                    if (foe >= 0 && p.owner_code == unsigned(foe + 1)) {
                        enemy_present = true;
                        break;
                    }
                }
                if (!enemy_present && own_levels >= 7 && other_levels <= 3 &&
                    double(float(owned) / float(total)) >= 0.66)
                    return target(code);
                previous = street;
                have_previous = true;
            } else if (facility(w, code)) {
                const auto &p = w.facilities[code - 4000];
                if (p.owner_code == actor + 1 && p.facility_type && p.facility_type != 4 && p.level >= 3 &&
                    p.level > facility_score) {
                    facility_score = unsigned(land_cursor);
                    target(code);
                }
            }
        }
        return facility_score != 0;
    }
    case 28: {
        if (!cb.lookahead_nodes)
            return false;
        const auto route = cb.lookahead_nodes(actor, 6);
        std::string_view previous;
        bool have_previous = false;
        for (auto node : route.nodes) {
            const auto code = node_code(w, node);
            if (land(w, code)) {
                const auto street = name(w.lands[code - 2000].economy.street_name);
                if (have_previous && street == previous)
                    continue;
                unsigned opponent_levels = 0;
                bool owns_any = false;
                for (std::size_t i = 1; i < w.lands.size(); ++i) {
                    const auto &p = w.lands[i].economy;
                    if (name(p.street_name) != street)
                        continue;
                    if (p.owner_code == actor + 1) {
                        owns_any = true;
                        break;
                    }
                    if (p.owner_code)
                        opponent_levels += p.level;
                }
                if (!owns_any && opponent_levels >= 7)
                    return target(code);
                previous = street;
                have_previous = true;
            } else if (facility(w, code) && foe >= 0) {
                const auto &p = w.facilities[code - 4000];
                if (p.owner_code == unsigned(foe + 1) && p.facility_type && p.level >= 3)
                    return target(code);
            }
        }
        return false;
    }
    case 29: {
        const auto instances = scan();
        const auto candidates = scanned_players(w, actor, instances);
        std::size_t winner = w.players.size();
        unsigned greatest = 0;
        for (std::size_t i = 0; i < w.players.size(); ++i)
            if (i != actor && w.players[i].state.controller_flags) {
                unsigned count = 0;
                for (std::size_t j = 1; j < w.lands.size(); ++j)
                    if (w.lands[j].economy.owner_code == i + 1)
                        ++count;
                for (std::size_t j = 1; j < w.facilities.size(); ++j)
                    if (w.facilities[j].owner_code == i + 1)
                        ++count;
                if (count > greatest) {
                    greatest = count;
                    winner = i;
                }
            }
        return winner < w.players.size() && int(winner) != foe &&
               w.players[winner].status.allied_player != actor + 1 &&
               std::find(candidates.begin(), candidates.end(), winner) != candidates.end() &&
               target(actor_code(unsigned(winner)));
    }
    case 30: {
        if (!cb.lookahead_nodes)
            return false;
        const auto route = cb.lookahead_nodes(actor, 3);
        bool danger = route.crossed_junction;
        std::int32_t sum = 0;
        unsigned opportunities = 0;
        if (!danger)
            for (auto node : route.nodes) {
                const auto code = node_code(w, node);
                if (land(w, code)) {
                    const auto &p = w.lands[code - 2000];
                    if (!p.economy.owner_code || (p.economy.owner_code == actor + 1 &&
                                                  !p.economy.chain_store && p.economy.level < 5)) {
                        sum = add(sum, p.economy.owner_code ? p.price_per_level : p.land_price);
                        ++opportunities;
                    }
                    if (p.economy.owner_code && p.economy.owner_code != actor + 1 &&
                        toll(w, code) > mul(index, 1000)) {
                        danger = true;
                        break;
                    }
                } else if (facility(w, code)) {
                    const auto &p = w.facilities[code - 4000];
                    if (!p.owner_code || (p.owner_code == actor + 1 && p.facility_type &&
                                          p.facility_type != 3 && p.level < 5)) {
                        sum = add(sum, p.owner_code ? p.price_per_level() : p.land_price);
                        ++opportunities;
                    }
                    if (p.owner_code && p.owner_code != actor + 1 && p.facility_type &&
                        p.facility_type != 4 && p.level) {
                        danger = true;
                        break;
                    }
                } else if (commercial(w, code) && w.commercials[code - 6000].owner_code &&
                           w.commercials[code - 6000].owner_code != actor + 1) {
                    danger = true;
                    break;
                }
            }
        if (!danger && double(sum) * 1.5 < player.state.cash && opportunities >= 2 &&
            mod::money_add(player.state.cash, player.state.bank_balance) > 10000 &&
            player.status.money_fortune_modifier >= 0)
            return target(actor_code(unsigned(actor)));
        for (auto victim : scanned_players(w, actor, scan())) {
            const auto next = cb.lookahead_nodes(victim, 3);
            if (next.crossed_junction)
                continue;
            sum = 0;
            opportunities = 0;
            bool rejected = false;
            for (auto node : next.nodes) {
                const auto code = node_code(w, node);
                if (land(w, code)) {
                    const auto &p = w.lands[code - 2000].economy;
                    if (p.owner_code == actor + 1) {
                        sum = add(sum, toll(w, code));
                        ++opportunities;
                    }
                    if (!p.owner_code || p.owner_code == victim + 1) {
                        rejected = true;
                        break;
                    }
                } else if (facility(w, code)) {
                    const auto &p = w.facilities[code - 4000];
                    if (p.owner_code == actor + 1 && p.facility_type && p.facility_type != 4 && p.level) {
                        sum = add(sum, p.toll_by_level.at(p.level));
                        ++opportunities;
                    }
                    if (!p.owner_code || p.owner_code == victim + 1) {
                        rejected = true;
                        break;
                    }
                } else if (commercial(w, code)) {
                    const auto &p = w.commercials[code - 6000];
                    if (p.owner_code == actor + 1) {
                        sum = add(sum, p.toll_fee);
                        ++opportunities;
                    }
                    if (!p.owner_code || p.owner_code == victim + 1) {
                        rejected = true;
                        break;
                    }
                }
            }
            if (!rejected && sum >= mul(index, 10000) && opportunities >= 2)
                return target(actor_code(unsigned(victim)));
        }
        return false;
    }
    default:
        return false;
    }
}
std::optional<CardTarget> decode_target(std::uint32_t code) {
    if (code & 0x8000U) {
        if (code & 255U)
            return CardTarget{Kind::actor, std::uint16_t(std::countr_zero(code & 255U))};
        if (code & 0x7f00U)
            return CardTarget{Kind::object, std::uint16_t((code >> 8U) & 0x7fU)};
        return std::nullopt;
    }
    if (code > 2000 && code < 6000)
        return CardTarget{Kind::property, std::uint16_t(code)};
    return std::nullopt;
}
} // namespace

std::optional<CardUse> choose_ai_card(const World &w, Random &random, std::size_t actor,
                                      const CardCallbacks &cb, CardAiMemory *persistent) {
    if (actor >= w.players.size())
        return std::nullopt;
    const auto &p = w.players[actor];
    if (!(p.state.controller_flags & 6U) || !(p.status.card_strategy & 1U))
        return std::nullopt;
    const auto count = card_count(p.state.cards);
    if (!count)
        return std::nullopt;
    std::size_t position = count > 8 ? random.next() % count : 0;
    std::array<std::uint8_t, 8> cards{};
    for (auto &card : cards) {
        card = p.state.cards[position++];
        if (count > 8 && position == count)
            position = 0;
    }
    CardAiMemory scratch;
    auto &memory = persistent ? *persistent : scratch;
    for (auto id : cards) {
        if (!id)
            break;
        if (id > 30)
            continue;
        const int difference = int(aggression[id - 1]) - p.status.rescue_strategy;
        if (difference >= 2 || (difference == 1 && random.next() % 3 != 0))
            continue;
        if (!predicate(w, random, actor, id, cb, memory, position))
            continue;
        CardUse use{id, {}, {}};
        if (id == 24 || id == 25)
            use.target = CardTarget{Kind::stock, std::uint16_t(memory.parameter0)};
        else if (id == 7)
            use.choice = CardTarget{Kind::property, std::uint16_t(memory.parameter0)};
        else if (id != 1 && id != 3 && id != 8 && id != 15 && id != 22 && id != 23)
            use.target = decode_target(memory.parameter0);
        if (id == 13)
            use.choice = CardTarget{Kind::card, std::uint16_t(memory.parameter1)};
        return use;
    }
    return std::nullopt;
}
} // namespace rich4
