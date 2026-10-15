// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/tools.hpp"
#include "rich4/mod_rules.hpp"
#include "rich4/direction.hpp"
#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

namespace rich4::tools {
namespace {
Result skipped(const char *reason) {
    return {true, false, reason};
}
Result missing(const char *reason) {
    return {false, false, reason};
}
bool road(const World &w, unsigned id) {
    return id > 0 && id < 2000 && id < w.nodes.size();
}
std::uint8_t between(const World &w, unsigned from, unsigned to) {
    const auto &a = w.nodes.at(from);
    const auto &b = w.nodes.at(to);
    return direction::from_delta(b.x - a.x, b.y - a.y);
}
unsigned first_neighbor(const MapNode &node) {
    for (auto n : node.neighbors)
        if (n)
            return n;
    return 0;
}
Result equip(World &world, std::size_t slot, unsigned tool) {
    auto &p = world.players.at(slot);
    auto &state = p.state;
    const unsigned transport = tool == 5 ? 1 : tool == 6 ? 2 : 3;
    if ((transport == 3 ? (state.traffic_method & 3U) : state.traffic_method) == transport)
        return skipped("已經使用這種交通工具");
    if (state.traffic_method == 1)
        mod::grant_equipped_tool(state.tool_amounts[4]);
    else if (state.traffic_method == 2)
        mod::grant_equipped_tool(state.tool_amounts[5]);
    if (tool == 12) {
        p.status.flags_100 = state.traffic_method;
        p.status.flags_101 = state.dice_count;
        state.traffic_method = 0x1f;
        state.dice_count = 1;
    } else {
        state.traffic_method = static_cast<std::uint8_t>(transport);
        state.dice_count = static_cast<std::uint8_t>(transport + 1);
    }
    --state.tool_amounts[tool - 1]; // Reserved in equipped transport, not returned to supply.
    return {true, true, {}};
}
Result place(World &w, std::size_t slot, unsigned tool, unsigned node_id) {
    if (!road(w, node_id))
        return skipped("請選擇道路");
    auto &node = w.nodes[node_id];
    if (node.flags & 0x00ffff00U)
        return skipped("道路已被佔用");
    const unsigned kind = tool + 14;
    const unsigned first = tool == 2 ? 16 : tool == 3 ? 26 : 36;
    for (unsigned i = first; i < first + 10; ++i) {
        auto &object = w.objects[i];
        if (object.node_id)
            continue;
        object.kind = static_cast<std::uint8_t>(kind);
        object.node_id = static_cast<std::uint16_t>(node_id);
        object.remaining_days = 0;
        object.attached_player = 0;
        object.direction = between(w, first_neighbor(node), node_id);
        node.flags |= (i + 1) << 16;
        --w.players.at(slot).state.tool_amounts[tool - 1];
        return {true, true, {}};
    }
    return skipped("沒有可放置的道具");
}
Result move_property(World &w, unsigned source, unsigned destination) {
    if (source == destination)
        return skipped("請選擇另一塊空地");
    if (source > 2000 && source < 4000 && destination > 2000 && destination < 4000 &&
        source - 2000 < w.lands.size() && destination - 2000 < w.lands.size()) {
        auto &a = w.lands[source - 2000];
        auto &b = w.lands[destination - 2000];
        if (b.economy.owner_code || b.economy.level)
            return skipped("目的地必須是空地");
        b.economy.owner_code = a.economy.owner_code;
        b.economy.level = a.economy.level;
        b.economy.chain_store = a.economy.chain_store;
        b.expired_date = a.expired_date;
        a.economy.owner_code = 0;
        a.economy.level = 0;
        a.economy.chain_store = 0;
        a.expired_date = 0;
        a.removed_building = 0;
        return {true, true, {}};
    }
    if (source > 4000 && source < 6000 && destination > 4000 && destination < 6000 &&
        source - 4000 < w.facilities.size() && destination - 4000 < w.facilities.size()) {
        auto &a = w.facilities[source - 4000];
        auto &b = w.facilities[destination - 4000];
        if (b.owner_code || b.level)
            return skipped("目的地必須是空地");
        b.owner_code = a.owner_code;
        b.level = a.level;
        b.facility_type = a.facility_type;
        b.expired_date = a.expired_date;
        a.owner_code = 0;
        a.level = 0;
        a.facility_type = 0;
        a.expired_date = 0;
        a.removed_building = 0;
        return {true, true, {}};
    }
    return skipped("請選擇同類型的土地");
}
Result teleport(World &w, std::size_t actor, unsigned source, unsigned destination,
                const Callbacks &callbacks) {
    if (source > 2000 && source < 6000)
        return move_property(w, source, destination);
    if (!(source & 0x8000U) || !road(w, destination))
        return skipped("請選擇物件和目的地");
    const auto object_id = (source >> 8) & 63U;
    if (object_id && object_id <= w.objects.size() && w.objects[object_id - 1].attached_player)
        source = 0x8000U | (1U << (w.objects[object_id - 1].attached_player - 1));
    auto &node = w.nodes[destination];
    if (source & 255U) {
        const auto slot = static_cast<unsigned>(std::countr_zero(source & 255U));
        if (slot >= 8 || (slot < 4 && (slot >= w.players.size() || !w.players[slot].state.controller_flags)))
            return skipped("角色無法移動");
        std::uint8_t facing =
            slot < 4 ? w.players[slot].state.direction : w.special_actors[slot - 4].direction;
        std::vector<std::uint16_t> neighbors;
        for (unsigned i = 0; i < 4; ++i)
            if (node.neighbors[i] && !(node.flags & (0x40000000U >> i)))
                neighbors.push_back(node.neighbors[i]);
        if (neighbors.empty())
            return skipped("目的地沒有通路");
        unsigned smallest = 8;
        std::size_t selected = 0;
        for (std::size_t i = 0; i < neighbors.size(); ++i) {
            const auto candidate = between(w, destination, neighbors[i]);
            unsigned delta = static_cast<unsigned>(std::abs(int(facing) - int(candidate)));
            if (delta > 4)
                delta = 8 - delta;
            if (delta < smallest) {
                smallest = delta;
                selected = i;
            }
        }
        facing = between(w, destination, neighbors[selected]);
        auto last = neighbors.front();
        for (auto n : neighbors)
            if (n != neighbors[selected]) {
                last = n;
                break;
            }
        const auto bit = 0x100U << slot;
        if (slot < 4) {
            if (slot == actor && callbacks.before_teleport)
                callbacks.before_teleport(slot);
            auto &p = w.players[slot].state;
            w.nodes.at(p.node_id).flags &= ~bit;
            p.node_id = static_cast<std::uint16_t>(destination);
            p.last_node_id = last;
            p.direction = facing;
            p.x = static_cast<std::uint16_t>(node.x);
            p.y = static_cast<std::uint16_t>(node.y);
            for (auto &object : w.objects)
                if (object.attached_player == slot + 1)
                    object.node_id = p.node_id;
        } else {
            auto &p = w.special_actors[slot - 4];
            w.nodes.at(p.node_id).flags &= ~bit;
            p.node_id = static_cast<std::uint16_t>(destination);
            p.last_node_id = last;
            p.direction = facing;
            p.x = static_cast<std::uint16_t>(node.x);
            p.y = static_cast<std::uint16_t>(node.y);
        }
        node.flags |= bit;
        return {true, true, {}};
    }
    if (!object_id || object_id > w.objects.size())
        return skipped("物件不存在");
    auto &object = w.objects[object_id - 1];
    if (!object.node_id || (node.flags & 0x00ff0000U))
        return skipped("目的地已有物件");
    w.nodes.at(object.node_id).flags &= 0xff00ffffU;
    object.node_id = static_cast<std::uint16_t>(destination);
    node.flags |= object_id << 16;
    object.direction = between(w, first_neighbor(node), destination);
    return {true, true, {}};
}
} // namespace
std::uint32_t selection_flags(unsigned tool, std::uint16_t source) {
    switch (tool) {
    case 2:
        return 1;
    case 3:
        return 0x10001;
    case 4:
        return 0x20001;
    case 7:
        return 0x300c0;
    case 9:
        return 0x2090006;
    case 13:
        return 0x400c0;
    case 11:
        if (!source)
            return 0x1200036;
        if (source > 2000 && source < 4000)
            return 0x2090802;
        if (source > 4000 && source < 6000)
            return 0x2090804;
        return 0x2090001;
    default:
        return 0;
    }
}
Result use(World &w, Random &, std::size_t slot, unsigned tool, std::uint16_t target,
           std::uint16_t destination, const Callbacks &callbacks) {
    if (slot >= w.players.size() || tool < 1 || tool > 13)
        throw std::invalid_argument("invalid tool action");
    if (!w.players[slot].state.tool_amounts[tool - 1])
        return skipped("沒有這項道具");
    // Speech (_tool_strings[character][tool-1], mood 0). The original speaks before the map pick
    // for tools 2-4, 7, 9, 11 and 13; the native pick happens before this call, so those lines are
    // said once the use is known to apply (still before the throw / missile / build pictures).
    const auto say = [&] {
        if (callbacks.say)
            callbacks.say(slot, tool - 1);
    };
    if (tool == 5 || tool == 6 || tool == 12) {
        auto result = equip(w, slot, tool);
        if (result.applied) // rich4_tool_jiche/qiche/gongchengche: after mounting.
            say();
        return result;
    }
    if (tool >= 2 && tool <= 4) {
        auto result = place(w, slot, tool, target);
        if (result.applied)
            say();
        return result;
    }
    if (tool == 11) {
        auto result = teleport(w, slot, target, destination, callbacks);
        if (result.applied) {
            (void)consume_tool(w.players[slot].state.tool_amounts, w.tool_supply, 11);
            say();
        }
        return result;
    }
    if (tool == 1) {
        if (!callbacks.robot)
            return missing("機器娃娃行動尚未接入");
        (void)consume_tool(w.players[slot].state.tool_amounts, w.tool_supply, 1);
        say(); // rich4_tool_jiqiwawa: after consuming, before the doll clears the road.
        callbacks.robot(slot);
        return {true, true, {}};
    }
    if (tool == 8) {
        if (!target || target > 6)
            return skipped("選擇一至六點");
        if (!callbacks.remote_dice)
            return missing("遙控骰子尚未接入");
        if (w.players[slot].state.controller_flags == 1)
            say(); // rich4_tool_yaokongtouzi: only the human dice dialog speaks; computers are silent.
        callbacks.remote_dice(slot, target);
        (void)consume_tool(w.players[slot].state.tool_amounts, w.tool_supply, 8);
        return {true, true, {}};
    }
    if (tool == 10) {
        if (!callbacks.restore_last_state)
            return missing("時光機尚未接入");
        say(); // rich4_tool_shiguangji: speaks before trying to restore.
        if (!callbacks.restore_last_state())
            return skipped("沒有可恢復的回合");
        (void)consume_tool(w.active_player().state.tool_amounts, w.tool_supply, 10);
        return {true, true, {}};
    }
    if (!target)
        return skipped("已取消");
    if (tool == 9) {
        if (!((target > 2000 && target < 4000 && target - 2000 < w.lands.size()) ||
              (target > 4000 && target < 6000 && target - 4000 < w.facilities.size())))
            return skipped("請選擇房屋");
        if (!callbacks.upgrade)
            return missing("機器工人尚未接入");
        (void)consume_tool(w.players[slot].state.tool_amounts, w.tool_supply, 9);
        say();
        callbacks.upgrade(target);
        return {true, true, {}};
    }
    if (tool == 7 || tool == 13) {
        if (!callbacks.blast)
            return missing("飛彈區域效果尚未接入");
        (void)consume_tool(w.players[slot].state.tool_amounts, w.tool_supply,
                           static_cast<std::uint8_t>(tool));
        say();
        callbacks.blast(target, tool == 13, slot);
        return {true, true, {}};
    }
    return missing("未知道具");
}
bool dismount(World &w, std::size_t slot) {
    auto &p = w.players.at(slot).state;
    if (!p.traffic_method)
        return false;
    if (p.traffic_method == 1)
        mod::grant_equipped_tool(p.tool_amounts[4]);
    else if (p.traffic_method == 2)
        mod::grant_equipped_tool(p.tool_amounts[5]);
    p.traffic_method = 0;
    p.dice_count = 1;
    return true;
}
} // namespace rich4::tools
