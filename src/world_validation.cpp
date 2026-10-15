// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/world_validation.hpp"
#include "rich4/mod_rules.hpp"
#include "rich4/calendar.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace rich4 {
namespace {

[[noreturn]] void fail(const std::string &field, std::string_view reason) {
    throw std::invalid_argument("Invalid playable world: " + field + " " + std::string(reason));
}

std::string element(std::string_view table, std::size_t index) {
    return std::string(table) + "[" + std::to_string(index) + "]";
}

void check(bool condition, const std::string &field, std::string_view reason) {
    if (!condition)
        fail(field, reason);
}

template <class Record> void table_ids(const std::vector<Record> &table, std::string_view name) {
    check(!table.empty(), std::string(name), "must include the index-zero sentinel");
    // The sentinel is archival storage, not a live map record.
    for (std::size_t i = 1; i < table.size(); ++i)
        check(table[i].id == i, element(name, i) + ".id", "must equal its table index");
}

template <std::size_t N>
void deck(const std::array<std::uint8_t, N> &values, std::uint32_t cursor, std::string_view name,
          std::string_view cursor_name) {
    check(cursor < N, std::string(cursor_name), "is outside the deck");
    std::array<bool, N> used{};
    for (std::size_t i = 0; i < N; ++i) {
        const auto field = element(name, i);
        check(values[i] < N, field, "contains an unknown event ID");
        check(!used[values[i]], field, "duplicates an event ID; original decks are permutations");
        used[values[i]] = true;
    }
}

float finite_float(std::uint32_t bits, const std::string &field) {
    const auto value = std::bit_cast<float>(bits);
    check(std::isfinite(value), field, "must be a finite binary32 value");
    return value;
}

void property_reference(const World &world, std::uint16_t code, const std::string &field,
                        bool require_property = false) {
    if (code > 2000 && code < 4000) {
        check(std::size_t(code - 2000) < world.lands.size(), field, "refers to an absent land");
    } else if (code > 4000 && code < 6000) {
        check(std::size_t(code - 4000) < world.facilities.size(), field, "refers to an absent facility");
    } else if (!require_property && code > 6000 && code < 8000) {
        check(std::size_t(code - 6000) < world.commercials.size(), field, "refers to an absent commercial");
    } else if (require_property) {
        fail(field, "must identify a land or facility");
    }
}

} // namespace

void validate_playable_world(const World &world) {
    const auto count = world.players.size();
    check(count >= 1 && count <= 4, "players", "must contain one to four normal players");
    check(world.header.player_count == count, "header.player_count", "does not match players");
    check(world.header.human_player_count <= count, "header.human_player_count", "exceeds player count");
    check(world.header.current_player < count, "header.current_player", "refers to an absent player");
    check(world.header.map_id < 4, "header.map_id", "must be 0..3");
    check(world.header.stage_id < 2, "header.stage_id", "must be 0..1");
    check(calendar::is_valid({world.header.date.year, world.header.date.month, world.header.date.day}),
          "header.date", "is outside the original calendar domain");
    check(world.header.price_index > 0, "header.price_index", "must be positive");
    check(world.lease_mode < 6, "lease_mode", "must be 0..5");
    check(world.camera_rotation < 8, "camera_rotation", "must be 0..7");
    table_ids(world.nodes, "nodes");
    table_ids(world.lands, "lands");
    table_ids(world.facilities, "facilities");
    table_ids(world.commercials, "commercials");
    table_ids(world.landscapes, "landscapes");
    check(world.nodes.size() > 1 && world.nodes.size() <= 65536, "nodes",
          "must have addressable non-sentinel nodes");

    const auto node_ref = [&](std::uint16_t id, const std::string &field) {
        check(id < world.nodes.size(), field, "refers to an absent node");
    };
    const auto owner_ref = [&](unsigned owner, const std::string &field) {
        check(owner <= count, field, "refers to an absent normal player");
    };
    std::array<bool, 46> referenced_objects{};
    const auto object_ref = [&](unsigned id, const std::string &field) {
        check(id <= world.objects.size(), field, "refers to an absent object");
        if (id)
            referenced_objects[id - 1] = true;
    };
    for (std::size_t i = 1; i < world.nodes.size(); ++i) {
        const auto &node = world.nodes[i];
        const auto path = element("nodes", i);
        for (std::size_t j = 0; j < node.neighbors.size(); ++j)
            node_ref(node.neighbors[j], element(path + ".neighbors", j));
        property_reference(world, node.type_and_index, path + ".type_and_index");
        object_ref((node.flags >> 16U) & 0xffU, path + ".flags.object_id");
    }
    for (std::size_t i = 0; i < count; ++i) {
        const auto &player = world.players[i];
        const auto &state = player.state;
        const auto path = element("players", i);
        check(state.slot == i, path + ".state.slot", "must equal its player index");
        check(state.character < 12, path + ".state.character", "must be 0..11");
        check(state.direction < 8, path + ".state.direction", "must be 0..7");
        node_ref(state.node_id, path + ".state.node_id");
        node_ref(state.last_node_id, path + ".state.last_node_id");
        check(state.dice_count <= 3 && (state.dice_count != 0 || state.controller_flags == 0),
              path + ".state.dice_count", "must be 1..3 for a participating player");
        // traffic_method's upper bits encode flight duration. Status days have
        // original 0x80 expiry and 0x40 disappearance mode bits, not plain counts.
        for (std::size_t j = 0; j < state.cards.size(); ++j)
            check(state.cards[j] <= 30, element(path + ".state.cards", j), "contains an unknown card ID");
        object_ref(player.status.god_info, path + ".status.god_info");
        object_ref(player.status.attached_bomb, path + ".status.attached_bomb");
        owner_ref(player.status.allied_player, path + ".status.allied_player");
        for (std::size_t j = 0; j < player.stocks.size(); ++j) {
            const auto &holding = player.stocks[j];
            const auto field = element(path + ".stocks", j);
            check(holding.amount >= 0, field + ".amount", "must be nonnegative");
            check(finite_float(holding.raw_cost_basis, field + ".raw_cost_basis") >= 0,
                  field + ".raw_cost_basis", "must be nonnegative");
        }
    }
    for (std::size_t i = 0; i < world.special_actors.size(); ++i) {
        const auto &actor = world.special_actors[i];
        const auto path = element("special_actors", i);
        // Confined and unused robot slots retain values that are not read until
        // a release/placement routine reconstructs their position and facing.
        if (actor.inactive_state != 0)
            continue;
        check(actor.direction < 8, path + ".direction", "must be 0..7 for an active actor");
        check(actor.owner < count, path + ".owner", "refers to an absent normal player");
        node_ref(actor.node_id, path + ".node_id");
        node_ref(actor.last_node_id, path + ".last_node_id");
    }
    for (std::size_t i = 0; i < world.objects.size(); ++i) {
        const auto &object = world.objects[i];
        const auto path = element("objects", i);
        if (!referenced_objects[i] && object.node_id == 0 && object.attached_player == 0)
            continue;
        check(object.kind >= 1 && object.kind <= 20, path + ".kind", "must be 1..20 for an active object");
        check(object.direction < 8, path + ".direction", "must be 0..7 for an active object");
        node_ref(object.node_id, path + ".node_id");
        owner_ref(object.attached_player, path + ".attached_player");
    }
    for (std::size_t i = 1; i < world.lands.size(); ++i) {
        const auto &land = world.lands[i];
        const auto path = element("lands", i);
        owner_ref(land.economy.owner_code, path + ".economy.owner_code");
        check(land.economy.level <= 5, path + ".economy.level", "must be 0..5");
    }
    for (std::size_t i = 1; i < world.facilities.size(); ++i) {
        const auto &facility = world.facilities[i];
        const auto path = element("facilities", i);
        owner_ref(facility.owner_code, path + ".owner_code");
        check(facility.level <= 5, path + ".level", "must be 0..5");
        if (facility.level != 0)
            check(facility.facility_type <= 4, path + ".facility_type",
                  "must be 0..4 for a constructed facility");
    }
    for (std::size_t i = 1; i < world.commercials.size(); ++i) {
        const auto &company = world.commercials[i];
        const auto path = element("commercials", i);
        owner_ref(company.owner_code, path + ".owner_code");
        for (std::size_t j = 0; j < company.shareholder_order.size(); ++j)
            owner_ref(company.shareholder_order[j], element(path + ".shareholder_order", j));
    }
    deck(world.news_deck, world.news_cursor, "news_deck", "news_cursor");
    deck(world.fortune_deck, world.fortune_cursor, "fortune_deck", "fortune_cursor");
    for (std::size_t i = 0; i < world.lottery_tickets.size(); ++i)
        owner_ref(world.lottery_tickets[i], element("lottery_tickets", i));
    check(world.stock_history_cursor >= 0 && world.stock_history_cursor < 144, "stock_history_cursor",
          "must be 0..143");
    (void)finite_float(world.stock_market_movement_bits, "stock_market_movement_bits");
    for (std::size_t i = 0; i < world.stocks.size(); ++i) {
        const auto &stock = world.stocks[i];
        const auto path = element("stocks", i);
        check(stock.flags_4 < world.commercials.size(), path + ".flags_4", "refers to an absent commercial");
        for (std::size_t j = 0; j < stock.float_bits.size(); ++j) {
            const auto field = element(path + ".float_bits", j);
            const auto value = finite_float(stock.float_bits[j], field);
            if (j <= 2)
                check(value >= 0 && value <= mod::max_stock_price, field, "is outside the MOD stock price domain");
        }
        for (std::size_t j = 0; j < world.stock_history_bits[i].size(); ++j)
            (void)finite_float(world.stock_history_bits[i][j], element(element("stock_history_bits", i), j));
    }
    for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t j = 0; j < world.offers[i].size(); ++j) {
            const auto &offer = world.offers[i][j];
            const auto path = element(element("offers", i), j);
            switch (offer.kind) {
            case OfferKind::empty:
                break; // Other bytes are retained, not reset by the original.
            case OfferKind::stock:
                check(offer.item < world.stocks.size(), path + ".item", "refers to an absent stock");
                break;
            case OfferKind::property:
                property_reference(world, offer.item, path + ".item", true);
                break;
            case OfferKind::tool:
                check(offer.item >= 1 && offer.item <= 13, path + ".item", "must be a tool ID 1..13");
                break;
            case OfferKind::card:
                check(offer.item >= 1 && offer.item <= 30, path + ".item", "must be a card ID 1..30");
                break;
            default:
                fail(path + ".kind", "contains an unknown listing kind");
            }
        }
    }
}

} // namespace rich4
