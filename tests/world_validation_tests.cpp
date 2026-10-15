// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/world_validation.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <numeric>
#include <stdexcept>
#include <string>

namespace {
using namespace rich4;
using Bytes = std::vector<std::uint8_t>;
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void put16(Bytes &bytes, std::size_t offset, std::uint16_t value) {
    bytes.at(offset) = static_cast<std::uint8_t>(value);
    bytes.at(offset + 1) = static_cast<std::uint8_t>(value >> 8U);
}
void put32(Bytes &bytes, std::size_t offset, std::uint32_t value) {
    put16(bytes, offset, static_cast<std::uint16_t>(value));
    put16(bytes, offset + 2, static_cast<std::uint16_t>(value >> 16U));
}

World fixture() {
    // Independently constructed map; no commercial game data in this test.
    constexpr std::array<std::uint32_t, 5> counts{4, 2, 1, 1, 2};
    constexpr std::array<std::uint32_t, 5> strides{40, 52, 56, 52, 28};
    std::array<std::size_t, 5> offsets{};
    Bytes map(40);
    for (std::size_t i = 0; i < counts.size(); ++i) {
        offsets[i] = map.size();
        put32(map, i * 8, counts[i]);
        put32(map, i * 8 + 4, static_cast<std::uint32_t>(map.size()));
        map.resize(map.size() + (counts[i] + 1) * strides[i]);
    }
    for (std::uint16_t i = 1; i <= 4; ++i) {
        const auto offset = offsets[0] + i * 40;
        put16(map, offset, static_cast<std::uint16_t>(i * 100));
        put16(map, offset + 2, 200);
        put16(map, offset + 24, static_cast<std::uint16_t>(i == 1 ? 4 : i - 1));
        put16(map, offset + 26, static_cast<std::uint16_t>(i == 4 ? 1 : i + 1));
    }
    put16(map, offsets[0] + 40 + 32, 2001);
    NewGameOptions options;
    options.players.resize(2);
    return World::from_map_resource(map, options);
}

void expect_rejection(const World &base, const std::function<void(World &)> &mutate, std::string_view field) {
    auto world = base;
    mutate(world);
    try {
        validate_playable_world(world);
    } catch (const std::invalid_argument &error) {
        if (std::string_view(error.what()).find(field) == std::string_view::npos)
            throw std::runtime_error("Wrong validation diagnostic for " + std::string(field) + ": " +
                                     error.what());
        return;
    }
    throw std::runtime_error("Expected rejection for " + std::string(field));
}

void semantic_rejections() {
    const auto base = fixture();
    const auto bad = [&](auto mutate, std::string_view path) { expect_rejection(base, mutate, path); };
    bad([](World &w) { w.header.map_id = 256; }, "header.map_id");
    bad([](World &w) { w.header.stage_id = 2; }, "header.stage_id");
    bad([](World &w) { w.header.date = {30, 2, 2000}; }, "header.date");
    bad([](World &w) { w.header.current_player = 2; }, "header.current_player");
    bad([](World &w) { w.header.player_count = 4; }, "header.player_count");
    bad([](World &w) { w.header.human_player_count = 3; }, "header.human_player_count");
    bad([](World &w) { w.header.price_index = 0; }, "header.price_index");
    bad([](World &w) { w.players.clear(); }, "players");
    bad([](World &w) { w.camera_rotation = 8; }, "camera_rotation");
    bad([](World &w) { w.lease_mode = 6; }, "lease_mode");
    bad([](World &w) { w.nodes.clear(); }, "nodes");
    bad([](World &w) { w.lands.clear(); }, "lands");
    bad([](World &w) { w.nodes[4].id = 0; }, "nodes[4].id");
    bad([](World &w) { w.nodes[4].neighbors[3] = 99; }, "nodes[4].neighbors[3]");
    bad([](World &w) { w.nodes[4].type_and_index = 2003; }, "nodes[4].type_and_index");
    bad([](World &w) { w.nodes[4].type_and_index = 4002; }, "nodes[4].type_and_index");
    bad([](World &w) { w.nodes[4].type_and_index = 6002; }, "nodes[4].type_and_index");
    bad([](World &w) { w.nodes[4].flags = 47U << 16U; }, "nodes[4].flags.object_id");
    bad([](World &w) { w.players[0].state.character = 255; }, "players[0].state.character");
    bad(
        [](World &w) {
            w.players[1].state.character = 12;
            w.players[1].state.controller_flags = 0;
        },
        "players[1].state.character"); // Inactive normal players still appear in account/name UI.
    bad([](World &w) { w.players[1].state.direction = 8; }, "players[1].state.direction");
    bad([](World &w) { w.players[1].state.slot = 0; }, "players[1].state.slot");
    bad([](World &w) { w.players[1].state.node_id = 5; }, "players[1].state.node_id");
    bad([](World &w) { w.players[1].state.last_node_id = 5; }, "players[1].state.last_node_id");
    bad([](World &w) { w.players[1].state.dice_count = 0; }, "players[1].state.dice_count");
    bad([](World &w) { w.players[1].state.cards[14] = 31; }, "players[1].state.cards[14]");
    bad([](World &w) { w.players[1].status.god_info = 47; }, "players[1].status.god_info");
    bad([](World &w) { w.players[1].status.attached_bomb = 255; }, "players[1].status.attached_bomb");
    bad([](World &w) { w.players[1].status.allied_player = 3; }, "players[1].status.allied_player");
    bad(
        [](World &w) {
            w.special_actors[2].inactive_state = 0;
            w.special_actors[2].direction = 8;
        },
        "special_actors[2].direction");
    bad(
        [](World &w) {
            w.special_actors[2].inactive_state = 0;
            w.special_actors[2].node_id = 5;
        },
        "special_actors[2].node_id");
    bad(
        [](World &w) {
            w.special_actors[2].inactive_state = 0;
            w.special_actors[2].owner = 2;
        },
        "special_actors[2].owner");
    bad(
        [](World &w) {
            w.objects[45].node_id = 4;
            w.objects[45].kind = 255;
        },
        "objects[45].kind");
    bad(
        [](World &w) {
            w.objects[45].node_id = 4;
            w.objects[45].direction = 8;
        },
        "objects[45].direction");
    bad([](World &w) { w.objects[45].node_id = 5; }, "objects[45].node_id");
    bad([](World &w) { w.objects[45].attached_player = 3; }, "objects[45].attached_player");
    bad(
        [](World &w) {
            w.players[0].status.god_info = 46;
            w.objects[45].kind = 0;
        },
        "objects[45].kind");
    bad(
        [](World &w) {
            w.lands[2].x = 65535;
            w.lands[2].economy.level = 6;
        },
        "lands[2].economy.level");
    bad([](World &w) { w.lands[2].economy.owner_code = 3; }, "lands[2].economy.owner_code");
    bad([](World &w) { w.facilities[1].level = 6; }, "facilities[1].level");
    bad(
        [](World &w) {
            w.facilities[1].level = 1;
            w.facilities[1].facility_type = 5;
        },
        "facilities[1].facility_type");
    bad([](World &w) { w.commercials[1].shareholder_order[3] = 3; }, "commercials[1].shareholder_order[3]");
    bad([](World &w) { w.news_cursor = 36; }, "news_cursor");
    bad([](World &w) { w.fortune_cursor = 37; }, "fortune_cursor");
    bad([](World &w) { w.news_deck[35] = 36; }, "news_deck[35]");
    bad([](World &w) { w.fortune_deck[36] = w.fortune_deck[0]; }, "fortune_deck[36]");
    bad([](World &w) { w.lottery_tickets[35] = 3; }, "lottery_tickets[35]");
    bad([](World &w) { w.stock_history_cursor = -1; }, "stock_history_cursor");
    bad([](World &w) { w.stock_history_cursor = 144; }, "stock_history_cursor");
    bad([](World &w) { w.stocks[11].float_bits[2] = 0x7f800000; }, "stocks[11].float_bits[2]");
    bad([](World &w) { w.stocks[11].float_bits[1] = std::bit_cast<std::uint32_t>(1000001.0F); },
        "stocks[11].float_bits[1]");
    bad([](World &w) { w.stock_history_bits[11][143] = 0x7fc00001; }, "stock_history_bits[11][143]");
    bad([](World &w) { w.stocks[11].flags_4 = 2; }, "stocks[11].flags_4");
    bad([](World &w) { w.players[1].stocks[11].amount = -1; }, "players[1].stocks[11].amount");
    bad([](World &w) { w.players[1].stocks[11].raw_cost_basis = 0x7f800000; },
        "players[1].stocks[11].raw_cost_basis");
    bad(
        [](World &w) {
            w.offers[0][6].kind = OfferKind::card;
            w.offers[0][6].item = 31;
        },
        "offers[0][6].item");
    bad(
        [](World &w) {
            w.offers[1][6].kind = OfferKind::property;
            w.offers[1][6].item = 4002;
        },
        "offers[1][6].item");
    bad([](World &w) { w.offers[1][0].kind = static_cast<OfferKind>(255); }, "offers[1][0].kind");
}

void valid_legacy_states() {
    auto world = fixture();
    validate_playable_world(world);
    auto &player = world.players[0];
    player.state.node_id = player.state.last_node_id = player.state.x = player.state.y = 0;
    player.state.cash = -100;
    player.state.bank_balance = INT32_MIN;
    player.state.loan = INT32_MAX;
    player.status.saved_direction = 15;
    player.status.days_in_prison = 0x80;
    player.status.days_disappearing = 0xc0;
    player.state.tool_amounts[13] = 0xff;
    player.state.tool_amounts[14] = 0xff;
    world.special_actors[0].inactive_state = 1;
    world.special_actors[0].direction = 255;
    world.special_actors[0].node_id = 65535;
    world.special_actors[0].owner = 255;
    world.special_actors[1].inactive_state = 0;
    world.special_actors[1].flags = 0x82;
    world.objects[45].kind = 255;
    world.objects[45].direction = 255;
    world.objects[45].motion_bits.fill(0x7fa12345); // Unused object record has no live interpretation.
    world.facilities[1].facility_type = 255;        // No building sprite when level is zero.
    world.lands[1].economy.chain_store = 255;       // Original test is nonzero, not ==1.
    world.lands[1].direction = 255;                 // Property facing is masked by original renderer.
    world.commercials[1].stock_index = 0xff;        // Unquoted company.
    world.offers[0][0].item = 65535;
    world.offers[0][0].price = -1;                         // Empty slot's payload is retained.
    world.offers[3][0].kind = static_cast<OfferKind>(255); // Outside the configured normal player count.
    world.lands[0].economy.level = 255; // Sentinel records are not rendered or used for tolls.
    world.header.date = {29, 2, 2100};  // Original divisible-by-four calendar, not Gregorian.
    for (const auto flight : std::array<std::uint8_t, 8>{0, 7, 11, 15, 19, 23, 27, 31}) {
        player.state.traffic_method = flight;
        validate_playable_world(world);
    }
    world.players[1].state.controller_flags = 0;
    world.players[1].state.dice_count = 0;
    validate_playable_world(world);
}

void archival_boundary() {
    auto world = fixture();
    const auto original = world.export_save();
    validate_playable_world(world);
    require(world.export_save() == original, "Validation must not mutate any legacy save bytes");
    auto corrupt = original;
    corrupt.at(0x10 + 19) = 255; // Real reproducer: valid SAVE structure, invalid character resource.
    const auto save = SaveFile::decode(corrupt);
    require(save.encode() == corrupt, "Binary codec must remain lossless for forensic inspection");
    const auto loaded = World::from_save(save);
    expect_rejection(loaded, [](World &) {}, "players[0].state.character");
    require(loaded.export_save() == corrupt, "Rejected world must remain byte-identical");
}

void validate_file(const char *path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error(std::string("Cannot open fixture: ") + path);
    const Bytes bytes((std::istreambuf_iterator<char>(input)), {});
    const auto world = World::from_save(SaveFile::decode(bytes));
    validate_playable_world(world);
    require(world.export_save() == bytes, "Read-only validation changed supplied save");
    std::cout << "Validated legacy save without changing bytes: " << path << '\n';
}
} // namespace

int main(int argc, char **argv) {
    try {
        semantic_rejections();
        valid_legacy_states();
        archival_boundary();
        for (int i = 1; i < argc; ++i)
            validate_file(argv[i]);
        std::cout << "World validation tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
