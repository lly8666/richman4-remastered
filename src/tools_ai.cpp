// SPDX-License-Identifier: GPL-3.0-or-later
// Readable port of mytbk/rich4 asm/rich4_ai_use_tool.asm and the AI branch
// in rich4_ui_use_tool.asm. Original program: Rich4 v3.11 (Softstar).
#include "rich4/tools_ai.hpp"
#include "rich4/mod_rules.hpp"
#include "rich4/board_renderer.hpp"
#include "rich4/legacy_float.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <span>
#include <string_view>

namespace rich4::tools_ai {
namespace {
constexpr std::array<unsigned, 13> aggression{0, 1, 1, 1, 0, 0, 2, 0, 1, 2, 1, 2, 2};
constexpr std::array<unsigned, 5> facility_max_level{1, 5, 5, 1, 5};

std::int32_t add(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) + std::uint32_t(b));
}
std::int32_t multiply(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) * std::uint32_t(b));
}
std::string_view name(const std::string &value) {
    return std::string_view(value).substr(0, value.find('\0'));
}
bool is_land(const World &world, unsigned code) {
    return code > 2000 && code < 4000 && code - 2000 < world.lands.size();
}
bool is_facility(const World &world, unsigned code) {
    return code > 4000 && code < 6000 && code - 4000 < world.facilities.size();
}
unsigned owner(const World &world, unsigned code) {
    if (is_land(world, code)) {
        return world.lands[code - 2000].economy.owner_code;
    }
    return is_facility(world, code) ? world.facilities[code - 4000].owner_code : 0;
}
unsigned level(const World &world, unsigned code) {
    if (is_land(world, code)) {
        return world.lands[code - 2000].economy.level;
    }
    return is_facility(world, code) ? world.facilities[code - 4000].level : 0;
}
board::Point position(const World &world, unsigned code) {
    if (is_land(world, code)) {
        const auto &land = world.lands[code - 2000];
        return {std::bit_cast<std::int16_t>(land.x), std::bit_cast<std::int16_t>(land.y)};
    }
    const auto &facility = world.facilities[code - 4000];
    return {std::bit_cast<std::int16_t>(facility.x), std::bit_cast<std::int16_t>(facility.y)};
}
std::uint16_t actor_code(std::size_t actor) {
    return static_cast<std::uint16_t>(0x8000U | (1U << actor));
}
bool absent(const PlayerStatus &status) {
    return status.days_in_hotel || status.days_disappearing || status.days_in_prison ||
           status.days_in_hospital;
}
std::size_t same_street_count(const World &world, std::size_t actor, const Land &land) {
    return static_cast<std::size_t>(
        std::count_if(world.lands.begin() + 1, world.lands.end(), [&](const Land &other) {
            return other.economy.owner_code == actor + 1 &&
                   name(other.economy.street_name) == name(land.economy.street_name);
        }));
}
std::int32_t toll(const World &world, unsigned code) {
    std::vector<LandEconomyRecord> records;
    for (std::size_t i = 1; i < world.lands.size(); ++i) {
        records.push_back(world.lands[i].economy);
    }
    const auto &land = world.lands[code - 2000].economy;
    // The AI always passes a street pointer, including for a chain-store lot.
    return land_toll(records, land.owner_code, world.header.price_index, name(land.street_name));
}

struct Path {
    std::vector<std::uint16_t> nodes;
    bool crossed_junction = false;
};
Path preview(const World &world, Random &random, std::size_t actor, unsigned count, bool reverse = false) {
    const auto &player = world.players[actor].state;
    auto current = reverse ? player.last_node_id : player.node_id;
    auto previous = reverse ? player.node_id : player.last_node_id;
    Path result;
    for (unsigned step = 0; step < std::min(count, 8U); ++step) {
        std::array<std::uint16_t, 4> candidates{};
        std::size_t length = 0;
        if (current < world.nodes.size()) {
            const auto &node = world.nodes[current];
            for (std::size_t i = 0; i < node.neighbors.size(); ++i) {
                const auto next = node.neighbors[i];
                if (next && next != previous && !(node.flags & (0x40000000U >> i))) {
                    candidates[length++] = next;
                }
            }
        }
        auto next = previous;
        if (length == 1) {
            next = candidates[0];
        } else if (length > 1) {
            next = candidates[random.next() % length];
            result.crossed_junction = true;
        }
        result.nodes.push_back(next);
        previous = current;
        current = next;
    }
    return result;
}

// Both selection routines use overwriting writes, unlike 0x40a45c's OR buffer.
class Selection {
  public:
    Selection(const World &world, board::Point center)
        : camera_{center, static_cast<std::uint8_t>(world.camera_rotation & 7U)}, pixels_(440 * 440) {}

    void insert(board::Point point, std::uint16_t code) {
        const auto projected = board::project(point, camera_);
        if (!projected) {
            return;
        }
        const int x = projected->x;
        const int y = projected->y - 40;
        if (x >= 0 && x < 440 && y >= 0 && y < 440) {
            pixels_[std::size_t(y) * 440 + std::size_t(x)] = code;
        }
    }

    std::vector<std::uint16_t> scan(int radius = -1) const {
        const int first = radius < 0 ? 0 : 220 - radius;
        const int last = radius < 0 ? 440 : 220 + radius;
        std::vector<std::uint16_t> result;
        for (int y = first; y < last; ++y) {
            for (int x = first; x < last; ++x) {
                const auto code = pixels_[std::size_t(y) * 440 + std::size_t(x)];
                if (code) {
                    result.push_back(code);
                }
            }
        }
        return result;
    }

  private:
    board::Camera camera_;
    std::vector<std::uint16_t> pixels_;
};

std::vector<std::uint16_t> visible_roads(const World &world, std::size_t actor) {
    const auto &player = world.players[actor].state;
    Selection selection(world, {player.x, player.y});
    for (std::size_t i = 1; i < world.nodes.size(); ++i) {
        const auto &node = world.nodes[i];
        if (!(node.flags & 0x00ffff00U)) {
            selection.insert({node.x, node.y}, static_cast<std::uint16_t>(i));
        }
    }
    return selection.scan();
}

std::vector<std::uint16_t> owned_area(const World &world, std::size_t actor, board::Point center,
                                      int radius) {
    Selection selection(world, center);
    const auto &player = world.players[actor];
    if (!absent(player.status)) {
        selection.insert(
            {std::bit_cast<std::int16_t>(player.state.x), std::bit_cast<std::int16_t>(player.state.y)},
            actor_code(actor));
    }
    for (std::size_t i = 1; i < world.lands.size(); ++i) {
        if (world.lands[i].economy.owner_code) {
            selection.insert(position(world, unsigned(2000 + i)), static_cast<std::uint16_t>(2000 + i));
        }
    }
    for (std::size_t i = 1; i < world.facilities.size(); ++i) {
        if (world.facilities[i].owner_code) {
            selection.insert(position(world, unsigned(4000 + i)), static_cast<std::uint16_t>(4000 + i));
        }
    }
    return selection.scan(radius);
}

bool occupied_confinement(const World &world, const MapNode &node) {
    const auto kind = node.flags & 0xffU;
    const auto &roster = kind == 4 ? world.prison_roster : world.hospital_roster;
    return (kind == 4 || kind == 5) &&
           std::any_of(roster.begin(), roster.begin() + 4, [](auto value) { return value != 0; });
}

int enemy(const World &world, Random &random, std::size_t actor) {
    int chosen = -1;
    std::int32_t maximum = 0;
    for (std::size_t i = 0; i < world.players.size(); ++i) {
        if (i != actor && world.players[i].state.controller_flags) {
            const auto value = std::bit_cast<std::int32_t>(world.players[actor].status.hostility[i]);
            if (value > maximum) {
                chosen = static_cast<int>(i);
                maximum = value;
            }
        }
    }
    if (chosen != -1) {
        return chosen;
    }
    std::vector<int> active;
    for (std::size_t i = 0; i < world.players.size(); ++i) {
        if (i != actor && world.players[i].state.controller_flags && !absent(world.players[i].status)) {
            active.push_back(static_cast<int>(i));
        }
    }
    return active.empty() ? -1 : active[random.next() % active.size()];
}

Result selected(unsigned tool, std::uint16_t target = 0, std::uint16_t destination = 0) {
    return {true, Decision{tool, target, destination}, {}};
}
Result missing_instances() {
    return {false, std::nullopt, "Tool AI needs the original rendered-instance selection."};
}

Result predicate(const World &world, Random &random, std::size_t actor, unsigned tool,
                 const Callbacks &callbacks) {
    const auto &player = world.players[actor];
    const auto self = actor + 1;
    const auto cash = player.state.cash;
    const auto funds = mod::money_add(cash, player.state.bank_balance);
    const auto fortune = player.status.money_fortune_modifier;
    const auto traffic = player.state.traffic_method & 3U;
    if (tool == 5 || tool == 6) {
        if ((tool == 5 ? traffic == 0 : traffic < 2) && random.next() % 4 == 0) {
            return selected(tool);
        }
        return {};
    }
    if (tool == 12) {
        if (traffic != 3 && random.next() % 15 <= player.status.rescue_strategy) {
            return selected(tool);
        }
        return {};
    }
    if (tool == 10) {
        return {};
    }
    if (tool == 1) {
        const auto path = preview(world, random, actor, 4);
        if (path.crossed_junction) {
            return {};
        }
        for (auto id : path.nodes) {
            if (id >= world.nodes.size()) {
                continue;
            }
            const auto &node = world.nodes[id];
            const unsigned object = (node.flags >> 16U) & 0x3fU;
            if (!object || object > world.objects.size()) {
                continue;
            }
            const auto kind = world.objects[object - 1].kind;
            const auto code = node.type_and_index;
            if ((kind >= 5 && kind <= 8) || kind == 11 || (kind == 17 && owner(world, code) == self)) {
                return selected(tool);
            }
            const auto cost = is_land(world, code)       ? toll(world, code)
                              : is_facility(world, code) ? 10000000
                                                         : 0;
            if (kind == 16 && owner(world, code) && owner(world, code) != self &&
                cost > multiply(3000, world.header.price_index)) {
                return selected(tool);
            }
        }
        return {};
    }
    if (tool == 2) {
        const auto path = preview(world, random, actor, 4);
        if (!path.crossed_junction) {
            for (auto id : path.nodes) {
                if (id >= world.nodes.size()) {
                    continue;
                }
                const auto &node = world.nodes[id];
                if (node.flags & 0x003fff00U) {
                    continue;
                }
                const auto code = node.type_and_index;
                bool purchase = false;
                unsigned price = 0;
                if (is_land(world, code)) {
                    const auto &land = world.lands[code - 2000];
                    purchase = !land.economy.owner_code &&
                               (same_street_count(world, actor, land) >= 2 || land.economy.level != 0);
                    price = land.land_price;
                } else if (is_facility(world, code)) {
                    const auto &facility = world.facilities[code - 4000];
                    purchase = !facility.owner_code;
                    price = facility.land_price;
                } else if ((node.flags & 0xffU) == 15 && player.state.points > 200) {
                    return selected(tool, id);
                }
                if (purchase && funds > 10000 && fortune >= 0 && !player.status.days_sleeping &&
                    multiply(static_cast<std::int32_t>(price), world.header.price_index) < cash) {
                    return selected(tool, id);
                }
            }
        }
    }
    if (tool >= 2 && tool <= 4) {
        const auto path = preview(world, random, actor, 6, true);
        const auto roads = visible_roads(world, actor);
        std::vector<std::uint16_t> candidates;
        std::uint16_t best = 0;
        std::int32_t highest_toll = 0;
        for (auto id : roads) {
            if (std::find(path.nodes.begin(), path.nodes.end(), id) == path.nodes.end()) {
                continue;
            }
            const auto &node = world.nodes[id];
            const auto code = node.type_and_index;
            if (tool == 2) {
                if (is_land(world, code) && owner(world, code) == self) {
                    const auto cost = toll(world, code);
                    if (cost > highest_toll && cost > multiply(6000, world.header.price_index)) {
                        best = id;
                        highest_toll = cost;
                    }
                }
            } else if (occupied_confinement(world, node)) {
                return selected(tool, id);
            } else if (tool == 4 || (owner(world, code) && owner(world, code) != self)) {
                candidates.push_back(id);
            }
        }
        if (best) {
            return selected(tool, best);
        }
        if (!candidates.empty()) {
            return selected(tool, candidates[random.next() % candidates.size()]);
        }
        return {};
    }
    if (tool == 7) {
        const auto foe = enemy(world, random, actor);
        if (foe < 0) {
            return {};
        }
        if (!callbacks.nearby_instances) {
            return missing_instances();
        }
        std::uint16_t target = 0;
        for (auto code : callbacks.nearby_instances(actor)) {
            if ((code & 0x8000U) && (code & (1U << foe))) {
                target = code;
                break;
            }
        }
        if (!target) {
            return {};
        }
        const auto &enemy_player = world.players[std::size_t(foe)].state;
        const auto area = owned_area(world, actor, {enemy_player.x, enemy_player.y}, 100);
        const auto carry = callbacks.ownership_carry ? callbacks.ownership_carry() : 0;
        for (auto code : area) {
            if ((code & 0x8000U) || owner(world, code) == self || carry == 1) {
                return {};
            }
        }
        return selected(tool, target);
    }
    if (tool == 8) {
        const auto god = player.status.god_info;
        if (god == 7 || god == 8 || god == 15 || player.status.days_sleeping || funds < 10000 ||
            fortune < 0) {
            return {};
        }
        const auto path = preview(world, random, actor, 6);
        if (path.crossed_junction) {
            return {};
        }
        unsigned highest_level = 0;
        std::uint16_t best = 0;
        for (std::size_t step = 0; step < path.nodes.size(); ++step) {
            const auto id = path.nodes[step];
            if (id >= world.nodes.size()) {
                continue;
            }
            const auto &node = world.nodes[id];
            if (node.flags & 0xf000U) {
                continue;
            }
            const unsigned object = (node.flags >> 16U) & 0x3fU;
            if (object && object <= world.objects.size()) {
                const auto kind = world.objects[object - 1].kind;
                if ((kind >= 5 && kind <= 8) || kind == 10 || kind == 11 || (kind >= 16 && kind <= 18)) {
                    continue;
                }
            }
            const auto code = node.type_and_index;
            const auto dice = static_cast<std::uint16_t>(step + 1);
            if (is_land(world, code)) {
                const auto &land = world.lands[code - 2000];
                if (!land.economy.owner_code && same_street_count(world, actor, land) > 1 &&
                    double(cash) > double(land.land_price) * 2.5) {
                    return selected(tool, dice);
                }
                if (land.economy.owner_code == self && !land.economy.chain_store && land.economy.level < 5 &&
                    land.economy.level > highest_level && same_street_count(world, actor, land) > 1 &&
                    double(cash) > double(land.price_per_level) * 2.5) {
                    highest_level = land.economy.level;
                    best = dice;
                }
            } else if (is_facility(world, code)) {
                const auto &facility = world.facilities[code - 4000];
                if ((!facility.owner_code && double(cash) > double(facility.land_price) * 2.5) ||
                    (facility.owner_code == self && facility.facility_type != 0 &&
                     facility.facility_type != 3 && facility.level < 5 &&
                     double(cash) > double(facility.price_per_level()) * 2.5)) {
                    return selected(tool, dice);
                }
            }
        }
        return best ? selected(tool, best) : Result{};
    }
    if (tool == 9) {
        if (!callbacks.nearby_instances) {
            return missing_instances();
        }
        std::uint16_t best = 0;
        unsigned highest_toll = 0;
        for (auto code : callbacks.nearby_instances(actor)) {
            unsigned cost = 0;
            if (is_land(world, code)) {
                const auto &land = world.lands[code - 2000].economy;
                if (land.owner_code == self && !land.chain_store && land.level < 5) {
                    cost = land.toll_by_level[land.level];
                }
            } else if (is_facility(world, code)) {
                const auto &facility = world.facilities[code - 4000];
                if (facility.owner_code == self && facility.facility_type < facility_max_level.size() &&
                    facility.level < facility_max_level[facility.facility_type]) {
                    cost = facility.toll_by_level[facility.level];
                }
            }
            if (cost > highest_toll) {
                best = code;
                highest_toll = cost;
            }
        }
        return best ? selected(tool, best) : Result{};
    }
    if (tool == 11) {
        std::uint16_t best = 0;
        unsigned highest_level = 0;
        for (auto id : visible_roads(world, actor)) {
            const auto code = world.nodes[id].type_and_index;
            if (owner(world, code) || level(world, code) < 3 || level(world, code) <= highest_level) {
                continue;
            }
            unsigned price = 0;
            if (is_land(world, code) && !world.lands[code - 2000].economy.chain_store) {
                price = world.lands[code - 2000].price_per_level;
            } else if (is_facility(world, code) && world.facilities[code - 4000].facility_type != 0) {
                price = world.facilities[code - 4000].price_per_level();
            } else {
                continue;
            }
            if (multiply(static_cast<std::int32_t>(price), world.header.price_index) < cash) {
                highest_level = level(world, code);
                best = id;
            }
        }
        return best && funds > 10000 && fortune >= 0 ? selected(tool, actor_code(actor), best) : Result{};
    }
    if (tool == 13) {
        std::vector<std::uint16_t> targets;
        for (std::size_t i = 1; i < world.lands.size(); ++i) {
            const auto &land = world.lands[i].economy;
            if (land.owner_code && land.owner_code != self && land.level) {
                targets.push_back(static_cast<std::uint16_t>(2000 + i));
            }
        }
        for (std::size_t i = 1; i < world.facilities.size(); ++i) {
            const auto &facility = world.facilities[i];
            if (facility.owner_code && facility.owner_code != self && facility.level) {
                targets.push_back(static_cast<std::uint16_t>(4000 + i));
            }
        }
        if (targets.empty()) {
            return {};
        }
        const auto alive =
            std::count_if(world.players.begin(), world.players.end(),
                          [](const WorldPlayer &value) { return value.state.controller_flags != 0; });
        const auto threshold =
            (numeric::Extended::from(1) / numeric::Extended::from(double(alive + 2))).single();
        for (unsigned attempt = 0; attempt < 10; ++attempt) {
            const auto target = targets[random.next() % targets.size()];
            const auto area = owned_area(world, actor, position(world, target), -1);
            bool self_visible = false;
            unsigned owned_count = 0, other_count = 0, owned_levels = 0, other_levels = 0;
            auto carry = callbacks.ownership_carry ? callbacks.ownership_carry() : 0;
            for (auto code : area) {
                if (code & 0x8000U) {
                    self_visible = true;
                    break;
                }
                if (owner(world, code) == self || carry == 1) {
                    ++owned_count;
                    owned_levels += level(world, code);
                } else {
                    ++other_count;
                    other_levels += level(world, code);
                }
                // The next iteration receives (code-4000)*8 for land, or the
                // facility table pointer. Neither can equal one.
                carry = 0;
            }
            const auto ratio = [](unsigned numerator, unsigned denominator) {
                if (!denominator) {
                    return numerator ? std::numeric_limits<float>::infinity()
                                     : std::numeric_limits<float>::quiet_NaN();
                }
                return (numeric::Extended::from(double(numerator)) /
                        numeric::Extended::from(double(denominator)))
                    .single();
            };
            // x87 unordered comparisons set CF, so NaN passes the original JAE.
            if (!self_visible && !(ratio(owned_count, other_count) >= threshold) &&
                !(ratio(owned_levels, other_levels) >= threshold)) {
                return selected(tool, target);
            }
        }
        return {};
    }
    return {};
}
} // namespace

Result evaluate(const World &world, Random &random, std::size_t actor, unsigned tool,
                const Callbacks &callbacks) {
    if (actor >= world.players.size() || actor >= 4 || tool == 0 || tool > aggression.size()) {
        return {false, std::nullopt, "Invalid tool AI actor or tool ID."};
    }
    const int preference = int(aggression[tool - 1]) - world.players[actor].status.rescue_strategy;
    if (preference >= 2 || (preference == 1 && random.next() % 3 != 0)) {
        return {};
    }
    return predicate(world, random, actor, tool, callbacks);
}

Result choose(const World &world, Random &random, std::size_t actor, const Callbacks &callbacks) {
    if (actor >= world.players.size() || actor >= 4) {
        return {false, std::nullopt, "Invalid tool AI actor."};
    }
    const auto &player = world.players[actor];
    if (!(player.state.controller_flags & 6U) || !(player.status.card_strategy & 2U)) {
        return {};
    }
    std::vector<unsigned> available;
    for (unsigned tool = 1; tool <= 13; ++tool) {
        if (tool != 10 && player.state.tool_amounts[tool - 1]) {
            available.push_back(tool);
        }
    }
    if (available.empty()) {
        return {};
    }
    std::size_t index = available.size() > 4 ? random.next() % available.size() : 0;
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        auto result = evaluate(world, random, actor, available[index], callbacks);
        if (!result.supported || result.decision) {
            return result;
        }
        index = (index + 1) % available.size();
    }
    return {};
}
} // namespace rich4::tools_ai
