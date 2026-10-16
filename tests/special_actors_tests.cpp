// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/special_actors.hpp"
#include <algorithm>
#include <bit>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
using namespace rich4;
using namespace rich4::special_actors;
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
World fixture() {
    World world;
    world.players.resize(4);
    world.nodes.resize(6);
    world.lands.resize(4);
    world.facilities.resize(2);
    world.commercials.resize(2);
    world.header.current_player = 2;
    world.header.price_index = 3;
    world.card_supply.fill(3);
    world.tool_supply.fill(10);
    for (std::size_t i = 0; i < world.players.size(); ++i) {
        auto &player = world.players[i].state;
        player.controller_flags = 2;
        player.points = static_cast<std::uint16_t>(65500 + i);
        player.bank_balance = static_cast<std::int32_t>(10003 + i * 101);
        player.cards = {1, 3, 5, 7, 9};
    }
    for (std::size_t i = 1; i < world.nodes.size(); ++i) {
        auto &node = world.nodes[i];
        node.id = static_cast<std::uint32_t>(i);
        node.x = static_cast<std::int16_t>(i * 32);
        node.y = 32;
        node.neighbors = {static_cast<std::uint16_t>(i == 5 ? 1 : i + 1),
                          static_cast<std::uint16_t>(i == 1 ? 5 : i - 1), 0, 0};
    }
    for (auto &actor : world.special_actors) {
        actor.node_id = 1;
        actor.last_node_id = 5;
        actor.x = 32;
        actor.y = 32;
        actor.owner = 0;
    }
    for (std::size_t id = 1; id < world.lands.size(); ++id) {
        auto &land = world.lands[id];
        land.economy.owner_code = 2;
        land.economy.street_name = id == 3 ? "other" : "\xb0\xea\xa5\xf3";
        land.land_price = static_cast<std::uint16_t>(id * 1000);
        land.removed_building = 0xfffffff9;
    }
    world.facilities[1].owner_code = 2;
    world.facilities[1].land_price = 5000;
    world.facilities[1].removed_building = 12345;
    world.commercials[1].owner_code = 2;
    world.commercials[1].funds = -17;
    for (std::size_t i = 0; i < 46; ++i)
        world.objects[i].kind = static_cast<std::uint8_t>(i < 14 ? i + 1 : i < 16 ? 15 : 16 + (i - 16) / 10);
    return world;
}
void routes_and_turns() {
    auto world = fixture();
    Random random(42), expected(42);
    world.nodes[1].flags = 0x1000;
    require(move_one_step(world, random, 4) == 2, "Forward step avoids previous node");
    (void)expected.next();
    require(random.state() == expected.state(), "One-option route consumes original RNG");
    require(!(world.nodes[1].flags & 0x1000) && (world.nodes[2].flags & 0x1000),
            "Only this special occupancy moves");
    require(world.special_actors[0].x == 64 && world.special_actors[0].last_node_id == 1,
            "Movement reaches exact node endpoint");
    world.nodes[2].flags |= 0x40000000U;
    require(move_one_step(world, random, 4) == 1 && random.state() == expected.state(),
            "Blocked forward route reverses without RNG");
    world = fixture();
    world.special_actors[0].inactive_state = 1;
    world.special_actors[0].days_sleeping = 2;
    require(turn(world, random, 4, {}).moved_steps == 0 && world.special_actors[0].days_sleeping == 2,
            "Inactive actors do not age or roll");
    world.special_actors[0].inactive_state = 0;
    world.special_actors[0].days_sleeping = 1;
    require(turn(world, random, 4, {}).moved_steps == 0 && world.special_actors[0].days_sleeping == 128,
            "Final sleep turn remains blocked");
    world.special_actors[0].days_tortoise_walking = 1;
    const auto one = turn(world, random, 4, {});
    (void)expected.next();
    require(one.planned_steps == 1 && one.moved_steps == 1 && random.state() == expected.state(),
            "Tortoise skips roll but consumes route draw");
    require(world.header.current_player == 2, "Special turns do not redirect ordinary player callbacks");
    world = fixture();
    random.seed(1);
    expected.seed(1);
    const unsigned steps = expected.next() % 9 + 2U;
    for (unsigned i = 0; i < steps; ++i)
        (void)expected.next();
    const auto complete = turn(world, random, 7, {});
    require(complete.planned_steps == steps && complete.moved_steps == steps &&
                random.state() == expected.state(),
            "Original 2..10 roll plus each forward route");
    for (unsigned input = 0; input < 256; ++input) {
        SpecialActor actor;
        actor.days_sleeping = actor.days_sleep_walking = actor.days_stopping = actor.days_tortoise_walking =
            static_cast<std::uint8_t>(input);
        age_status(actor);
        const auto value = (input & 128U) ? 0U : input == 1 ? 128U : input ? input - 1 : 0;
        require(actor.days_sleeping == value && actor.days_sleep_walking == value &&
                    actor.days_stopping == value && actor.days_tortoise_walking == value,
                "Every counter byte has bounded original expiry semantics");
    }
}
void encounters() {
    auto world = fixture();
    Random random(42);
    Callbacks callbacks;
    callbacks.remove_object = [&](auto id) {
        world.objects.at(id - 1).node_id = 0;
        world.nodes[1].flags &= 0xff00ffffU;
    };
    callbacks.hospital = [&](auto slot, auto days) {
        require(days == 3, "Mine sends special actor to hospital for3days");
        world.special_actors.at(slot - 4).inactive_state = 2;
    };
    world.nodes[1].flags = 0x0600; // Two occupants; first wins even if second has more points.
    auto result = interact(world, random, 4, false, callbacks);
    require(world.players[1].state.points == 32751 && world.players[0].state.points == 32714 &&
                world.players[2].state.points == 65502,
            "NPC4 takes floor half from lowest other slot with u16 wrapping");
    require(result.effects.size() == 1 && result.effects[0].target == 1,
            "Point theft produces structured target");
    world = fixture();
    world.players[1].state.controller_flags = 0;
    world.nodes[1].flags = 0x600;
    require(interact(world, random, 4, false, callbacks).effects.empty(),
            "Inactive lowest occupant suppresses theft rather than searching next");
    world = fixture();
    world.nodes[1].flags = (17U << 16) | 0x200; // barrier first, then role action
    result = interact(world, random, 4, false, callbacks);
    require(!result.stop_movement && world.players[0].state.tool_amounts[1] == 1 &&
                result.effects.size() == 2,
            "NPC4 collects barrier while passing then steals points");
    world = fixture();
    world.nodes[1].flags = (27U << 16) | 0x200;
    result = interact(world, random, 6, false, callbacks);
    require(!result.stop_movement, "Mine does not hit a passing NPC6");
    result = interact(world, random, 6, true, callbacks);
    require(result.stop_movement && world.special_actors[2].inactive_state == 2,
            "Stopped mine confines special actor");
    world = fixture();
    world.nodes[1].flags = 13U << 16;
    world.tool_supply.fill(0);
    (void)interact(world, random, 4, false, callbacks);
    require((world.nodes[1].flags & 0xff0000) == 0, "NPC4 consumes an empty gift unlike ordinary player");
    world = fixture();
    world.nodes[1].flags = (14U << 16) | 0x200;
    world.special_actors[0].days_sleep_walking = 128;
    require(interact(world, random, 4, true, callbacks).effects.empty() &&
                (world.nodes[1].flags & 0xff0000) != 0,
            "Sleepwalking suppresses collection and theft");
    world = fixture();
    world.nodes[1].flags = 4;
    world.special_actors[0].flags = 1;
    (void)interact(world, random, 4, false, callbacks);
    require(world.special_actors[0].flags == 129, "First home encounter arms return bit");
    callbacks.prison = [&](auto slot, auto days) {
        require(slot == 4 && days == 0, "Home callback is special slot and zero days");
        world.special_actors[0].inactive_state = 1;
    };
    require(interact(world, random, 4, false, callbacks).stop_movement,
            "Second home encounter returns special actor and stops turn");
}
void finance_and_cards() {
    auto world = fixture();
    Random random(42), expected(42);
    Callbacks callbacks;
    std::vector<std::array<std::int32_t, 4>> payments;
    callbacks.pay = [&](auto payer, auto recipient, auto amount, auto flags) {
        payments.push_back({payer, recipient, amount, static_cast<std::int32_t>(flags)});
    };
    world.nodes[1].flags = 0x200 | 14;
    const auto card = world.players[1].state.cards[expected.next() % 5];
    (void)interact(world, random, 5, false, callbacks);
    require(random.state() == expected.state() && card_count(world.players[1].state.cards) == 4 &&
                card_count(world.players[0].state.cards) == 6 && has_card(world.players[0].state.cards, card),
            "NPC5 steals exactly one random card before ATM");
    require(payments.size() == 3 && payments[0] == std::array<std::int32_t, 4>{1, 0, 2020, 5} &&
                payments[2][2] == 2061,
            "ATM theft uses20percent original bank-first cash-recipient flags");
    payments.clear();
    world.nodes[1].flags = 0;
    world.nodes[1].type_and_index = 2001;
    (void)interact(world, random, 6, false, callbacks);
    require(payments.empty(), "Property collection requires stopping");
    (void)interact(world, random, 6, true, callbacks);
    require(payments.back() == std::array<std::int32_t, 4>{1, 0, 9000, 0},
            "NPC6 sums same-owner same-Big5-street base prices only");
    (void)interact(world, random, 7, true, callbacks);
    require(payments.back()[2] == -7, "NPC7 uses exact signed last-toll bits without price multiplier");
    world.nodes[1].type_and_index = 4001;
    (void)interact(world, random, 6, true, callbacks);
    require(payments.back()[2] == 15000, "NPC6 facility base price");
    world.nodes[1].type_and_index = 6001;
    (void)interact(world, random, 7, true, callbacks);
    require(payments.back() == std::array<std::int32_t, 4>{101, 0, -17, 0},
            "NPC7 commercial encoded payer and signed balance");
}
std::vector<std::uint32_t> row(std::ifstream &file) {
    std::string line;
    require(bool(std::getline(file, line)), "Truncated original NPC capture");
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    std::istringstream input(line);
    std::string cell;
    std::vector<std::uint32_t> values;
    while (std::getline(input, cell, ',')) {
        std::uint32_t value = 0;
        const auto parsed = std::from_chars(cell.data(), cell.data() + cell.size(), value);
        require(parsed.ec == std::errc{} && parsed.ptr == cell.data() + cell.size(),
                "Invalid original capture number");
        values.push_back(value);
    }
    return values;
}
std::ifstream capture(const std::filesystem::path &path, const char *header) {
    std::ifstream file(path);
    require(bool(file), "Missing original NPC capture");
    std::string line;
    require(bool(std::getline(file, line)), "Empty original NPC capture");
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    require(line == header, "Original NPC capture schema mismatch");
    return file;
}
void equal(const std::vector<std::uint32_t> &actual, const std::vector<std::uint32_t> &expected) {
    require(actual.size() == expected.size(), "Original capture column count mismatch");
    if (actual != expected) {
        const auto offset = static_cast<std::size_t>(
            std::mismatch(actual.begin(), actual.end(), expected.begin()).first - actual.begin());
        std::string coordinates;
        for (unsigned i = 0; i < 5 && i < actual.size(); ++i)
            coordinates += std::to_string(actual[i]) + ",";
        throw std::runtime_error("Original NPC mismatch case(" + coordinates + ") column" +
                                 std::to_string(offset) + " actual" + std::to_string(actual[offset]) +
                                 " expected" + std::to_string(expected[offset]));
    }
}
void finished(std::ifstream &file) {
    std::string extra;
    require(!std::getline(file, extra), "Unexpected duplicate/extra capture record");
}
void oracle(const std::filesystem::path &directory) {
    constexpr std::array<std::uint32_t, 4> seeds{0, 1, 42, 0xdeadbeef};
    auto status = capture(directory / "status.csv", "actor,before,sleep,walk,stop,tortoise");
    for (unsigned slot = 4; slot < 8; ++slot)
        for (unsigned value = 0; value < 256; ++value) {
            SpecialActor state;
            state.days_sleeping = state.days_sleep_walking = state.days_stopping =
                state.days_tortoise_walking = static_cast<std::uint8_t>(value);
            age_status(state);
            equal({slot, value, state.days_sleeping, state.days_sleep_walking, state.days_stopping,
                   state.days_tortoise_walking},
                  row(status));
        }
    finished(status);
    auto routes = capture(directory / "routes.csv",
                          "actor,seed,mask,node,previous,x,y,direction,rng,oldflags,newflags");
    for (unsigned slot = 4; slot < 8; ++slot)
        for (auto seed : seeds)
            for (unsigned mask = 0; mask < 16; ++mask) {
                auto world = fixture();
                Random random(seed);
                world.nodes[1].neighbors[2] = 3;
                world.nodes[1].neighbors[3] = 4;
                world.nodes[1].flags = 0x1000U << (slot - 4);
                for (unsigned bit = 0; bit < 4; ++bit)
                    if (mask & (1U << bit))
                        world.nodes[1].flags |= 0x40000000U >> bit;
                const auto node = move_one_step(world, random, slot);
                const auto &state = world.special_actors[slot - 4];
                equal({slot, seed, mask, node, state.last_node_id, state.x, state.y, state.direction,
                       random.state(), world.nodes[1].flags, world.nodes[node].flags},
                      row(routes));
            }
    finished(routes);
    auto interactions =
        capture(directory / "interactions.csv", "actor,seed,scenario,stopped,sleepwalking,rng,steps,points/"
                                                "cards/tools/supplies/payment/removal/confinement");
    for (unsigned slot = 4; slot < 8; ++slot)
        for (auto seed : seeds)
            for (unsigned scenario = 0; scenario < 28; ++scenario)
                for (unsigned stopped = 0; stopped < 2; ++stopped)
                    for (const auto sleeping : std::array<std::uint8_t, 2>{0, 128}) {
                        auto world = fixture();
                        Random random(seed);
                        Callbacks callbacks;
                        world.special_actors[slot - 4].days_sleep_walking = sleeping;
                        std::vector<std::uint32_t> payments, boundaries;
                        callbacks.pay = [&](auto payer, auto recipient, auto amount, auto flags) {
                            payments.insert(payments.end(), {std::bit_cast<std::uint32_t>(payer),
                                                             std::bit_cast<std::uint32_t>(recipient),
                                                             static_cast<std::uint32_t>(amount), flags});
                        };
                        callbacks.remove_object = [&](auto id) {
                            boundaries.insert(boundaries.end(), {1, id, 0});
                        };
                        callbacks.prison = [&](auto actor, auto days) {
                            boundaries.insert(boundaries.end(), {2, static_cast<std::uint32_t>(actor), days});
                        };
                        callbacks.hospital = [&](auto actor, auto days) {
                            boundaries.insert(boundaries.end(), {3, static_cast<std::uint32_t>(actor), days});
                        };
                        std::uint32_t flags = 0;
                        if (scenario == 1 || scenario == 2)
                            flags = 0x600;
                        if (scenario == 2)
                            world.players[1].state.controller_flags = 0;
                        if (scenario == 3)
                            flags = 0x300;
                        if (scenario == 4)
                            flags = 14;
                        if (scenario >= 5 && scenario <= 7)
                            world.nodes[1].type_and_index =
                                static_cast<std::uint16_t>((scenario - 4) * 2000 + 1);
                        if (scenario >= 8 && scenario <= 25) {
                            const auto kind = scenario - 7;
                            const auto id = kind <= 15 ? kind : kind == 16 ? 17U : kind == 17 ? 27U : 37U;
                            flags = (id << 16) | 0x200;
                        }
                        if (scenario == 26 || scenario == 27) {
                            flags = scenario == 26 ? 4 : 5;
                            world.special_actors[slot - 4].flags =
                                static_cast<std::uint8_t>(scenario == 26 ? 0x81 : 0x82);
                        }
                        world.nodes[1].flags = flags;
                        const auto result = interact(world, random, slot, stopped != 0, callbacks);
                        require(result.supported, "Complete NPC oracle callbacks");
                        std::vector<std::uint32_t> actual{slot,
                                                          seed,
                                                          scenario,
                                                          stopped,
                                                          sleeping,
                                                          random.state(),
                                                          stopped || result.stop_movement ? 0U : 2U};
                        for (const auto &player : world.players) {
                            actual.push_back(player.state.points);
                            actual.insert(actual.end(), player.state.cards.begin(), player.state.cards.end());
                            actual.insert(actual.end(), player.state.tool_amounts.begin(),
                                          player.state.tool_amounts.end());
                        }
                        actual.insert(actual.end(), world.card_supply.begin(), world.card_supply.end());
                        actual.insert(actual.end(), world.tool_supply.begin(), world.tool_supply.end());
                        actual.push_back(static_cast<std::uint32_t>(payments.size() / 4));
                        payments.resize(12);
                        actual.insert(actual.end(), payments.begin(), payments.end());
                        actual.push_back(static_cast<std::uint32_t>(boundaries.size() / 3));
                        boundaries.resize(9);
                        actual.insert(actual.end(), boundaries.begin(), boundaries.end());
                        equal(actual, row(interactions));
                    }
    finished(interactions);
    std::cout << "Matched1024 original status cases,256 routes and1792 interactions including RNG and "
                 "callback arguments\n";
}
} // namespace
int main(int argc, char **argv) {
    try {
        routes_and_turns();
        encounters();
        finance_and_cards();
        if (argc == 3 && std::string(argv[1]) == "--oracle")
            oracle(argv[2]);
        else if (argc != 1)
            throw std::invalid_argument("Usage:special_actors_tests [--oracle directory]");
        std::cout << "Special actor rules tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
