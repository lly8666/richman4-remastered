// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/calendar.hpp"
#include "rich4/gods.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace {
using namespace rich4;
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
World fixture() {
    World world;
    world.header.player_count = 4;
    world.header.price_index = 1;
    world.players.resize(4);
    world.nodes.resize(3);
    world.lands.resize(2);
    world.facilities.resize(2);
    for (std::size_t slot = 0; slot < 4; ++slot) {
        world.players[slot].state.controller_flags = slot == 0 ? 1 : 2;
        world.players[slot].state.node_id = 1;
        world.players[slot].state.cash = 10000;
    }
    for (std::size_t i = 0; i < world.objects.size(); ++i)
        world.objects[i].kind = static_cast<std::uint8_t>(i < 14 ? i + 1 : i < 16 ? 15 : 16 + (i - 16) / 10);
    world.card_supply.fill(3);
    world.tool_supply.fill(10);
    return world;
}
void attachment() {
    auto world = fixture();
    Random random(0);
    gods::Callbacks callbacks;
    world.objects[0].node_id = 1;
    world.nodes[1].flags = 0xff011234U;
    const auto no_callback = gods::attach(world, random, 0, 1, 1, callbacks);
    require(!no_callback.supported && world.players[0].status.god_info == 0 &&
                world.objects[0].node_id == 1 && random.state() == 0,
            "Missing financial boundary cannot partially attach or consume randomness");
    std::vector<std::tuple<int, int, int, unsigned>> payments;
    callbacks.pay = [&](int payer, int receiver, int amount, unsigned flags) {
        payments.emplace_back(payer, receiver, amount, flags);
    };
    callbacks.choose_money = [](auto kind) { return kind == 1 ? 701 : 6789; };
    std::vector<std::uint8_t> spoken;
    callbacks.voice = [&](auto, auto category) { spoken.push_back(category); };
    require(gods::attach(world, random, 0, 1, 1, callbacks).applied &&
                world.players[0].status.god_info == 1 && world.objects[0].attached_player == 1 &&
                world.objects[0].remaining_days == 7 && world.nodes[1].flags == 0xff001234U &&
                world.players[0].status.god_luck_modifier == -100 &&
                world.players[0].status.money_fortune_modifier == 100,
            "Attachment moves object to player, clears exact node object byte and adds original modifiers");
    require(
        payments.size() == 3 && payments[0] == std::make_tuple(1, 0, 701, 1U) && spoken.back() == 8,
        "Small wealth god charges other active players to recipient cash with unindexed slot-machine amount");
    unsigned removed = 0;
    callbacks.remove_object = [&](auto id) {
        removed = id;
        world.players[0].status.god_info = 0;
        world.objects[id - 1].node_id = 0;
        world.objects[id - 1].attached_player = 0;
    };
    require(gods::detach(world, 0, callbacks).applied && removed == 1,
            "Detachment delegates full remove/respawn exactly once");
    world = fixture();
    world.players[0].status.god_luck_modifier = -32700;
    require(gods::attach(world, random, 0, 2, 0, callbacks).applied && world.players[0].state.cash == 16789 &&
                world.players[0].status.cumulative_income == 6789 &&
                world.players[0].status.god_luck_modifier == 32636,
            "Large wealth god credits cash/income and wraps signed16 modifier");
    world = fixture();
    world.players[0].state.cards = {1, 2, 3, 4};
    require(gods::attach(world, random, 0, 8, 0, callbacks).applied && world.players[0].state.cards[0] == 2 &&
                world.players[0].state.cards[1] == 4 && world.players[0].state.cards[2] == 0,
            "Large poverty-card god uses original alternating-slot half removal");
    world = fixture();
    world.players[0].state.cards = {1, 3};
    world.players[0].state.points = 500;
    world.players[0].state.traffic_method = 1;
    world.players[0].state.dice_count = 2;
    require(gods::attach(world, random, 0, 16, 0, callbacks).applied &&
                world.players[0].status.god_info == 16 && world.objects[15].remaining_days == 13 &&
                world.players[0].state.points == 500 && world.players[0].state.cards[0] == 0 &&
                world.players[0].state.traffic_method == 0,
            "Second kind15 object stores its ID, sells transport/cards without crediting returned points");
}
// Display only: the original first plays the god film, then its caption,
// then reports individual gained/lost card names. Callback must not consume RNG.
void card_reward_and_loss_presentations() {
    for (const auto kind : {3U, 4U, 7U, 8U, 15U}) {
        auto world = fixture();
        auto &cards = world.players[0].state.cards;
        if (kind >= 7) {
            cards[0] = 1; cards[1] = 2; cards[2] = 3; cards[3] = 4;
        }
        const auto before = card_count(cards);
        std::vector<int> order;
        std::uint8_t reported_kind = 0, first = 0, second = 0;
        unsigned removed = 0;
        gods::Callbacks callbacks;
        callbacks.appeared = [&](std::size_t slot, std::uint8_t got) {
            require(slot == 0 && got == kind, "God movie kind");
            order.push_back(1);
        };
        callbacks.arrival = [&](std::size_t slot, std::uint8_t got) {
            require(slot == 0 && got == kind, "God caption kind");
            order.push_back(2);
        };
        callbacks.cards_changed = [&](std::size_t slot, std::uint8_t got,
                                       std::uint8_t a, std::uint8_t b, unsigned count) {
            require(slot == 0, "God inventory notice target");
            order.push_back(3);
            reported_kind = got; first = a; second = b; removed = count;
        };
        Random random(11);
        require(gods::attach(world, random, 0, static_cast<std::uint16_t>(kind), 0, callbacks).applied,
                "God must attach for presentation test");
        require(order == std::vector<int>({1, 2, 3}) && reported_kind == kind,
                "Card change notification must follow the god movie and caption");
        const auto after = card_count(world.players[0].state.cards);
        if (kind == 3 || kind == 4)
            require(after > before && first > 0 && (kind == 3 || second > 0) && removed == 0,
                    "Fortune gods must name the cards that were actually awarded");
        else if (kind == 7)
            require(before == after + 1 && first > 0 && removed == 1,
                    "Small poverty god must name the removed card");
        else
            require(before == after + removed && removed > 0 && first == 0 && second == 0,
                    "Half/all-loss gods must state the actual discarded card count");
    }
}
void encounters() {
    auto world = fixture();
    Random random(1);
    gods::Callbacks callbacks;
    unsigned removed = 0, hospital = 0;
    callbacks.remove_object = [&](auto id) {
        removed = id;
        world.objects[id - 1].node_id = 0;
    };
    callbacks.hospital = [&](auto slot, auto days) {
        require(slot == 0, "Injured actor");
        hospital = days;
    };
    require(!gods::touch(world, random, 0, 14, false, callbacks).applied &&
                world.players[0].state.points == 0,
            "Walking past a gift does not collect it");
    world.players[0].state.points = 65500;
    require(gods::touch(world, random, 0, 14, true, callbacks).applied &&
                world.players[0].state.points == 464 && removed == 14,
            "Stopping collects500 points with original16bit wrap");
    world.tool_supply.fill(0);
    removed = 0;
    require(!gods::touch(world, random, 0, 13, true, callbacks).applied && removed == 0,
            "Empty shared tool supply leaves gift on map");
    require(gods::touch(world, random, 0, 17, false, callbacks).stop_movement && removed == 17,
            "Barrier type16 stops movement even before finalstep");
    require(!gods::touch(world, random, 0, 27, false, callbacks).applied && hospital == 0,
            "Mine type17 only explodes at a stop");
    require(gods::touch(world, random, 0, 27, true, callbacks).stop_movement && hospital == 3,
            "Mine removes object then delegates injury and3-day hospital");
    hospital = 0;
    world.players[0].state.traffic_method = 2;
    require(gods::touch(world, random, 0, 11, true, callbacks).applied && hospital == 0 && removed == 11,
            "Vehicle avoids dog injury but the dog is still removed");
    world.nodes[1].flags = 0x00250100;
    require(gods::touch(world, random, 0, 37, true, callbacks).applied &&
                world.players[0].status.attached_bomb == 37 && world.objects[36].attached_player == 1 &&
                world.objects[36].remaining_days == 38 && world.nodes[1].flags == 0x100,
            "Stopped player picks up timed bomb, preserving player occupancy");
    require(!gods::touch(world, random, 0, 15, true, callbacks).applied,
            "Beggar kind15 has no walking-pickup handler");
}
void property_rules() {
    auto world = fixture();
    Random random(0);
    gods::Callbacks callbacks;
    world.nodes[1].type_and_index = 2001;
    auto &land = world.lands[1];
    land.economy.owner_code = 2;
    land.economy.level = 3;
    land.land_price = 1000;
    unsigned upgraded = 0, downgraded = 0;
    std::vector<int> hostility;
    callbacks.upgrade_property = [&](auto code) {
        upgraded = code;
        return 0x81U;
    };
    callbacks.downgrade_property = [&](auto code) { downgraded = code; };
    callbacks.hostility = [&](auto from, auto target, int value) {
        require(from == 1 && target == 0, "God hostility direction");
        hostility.push_back(value);
    };
    world.players[0].status.god_info = 9;
    require(gods::after_property(world, 0, 1, callbacks).applied && upgraded == 2001,
            "Angel delegates exact building type selection/upgrade");
    world.players[0].status.god_info = 10;
    world.header.price_index = 3;
    require(gods::after_property(world, 0, 1, callbacks).applied && downgraded == 2001 &&
                hostility.back() == 180,
            "Demon applies land hostility60*index then downgrades");
    world.players[0].status.god_info = 12;
    require(gods::after_property(world, 0, 1, callbacks).applied && land.economy.owner_code == 1 &&
                hostility.back() == 0,
            "Land god preserves original double-lowword hostility ABI bug instead of substituting intended "
            "value");
    land.economy.owner_code = 0;
    world.lease_mode = 5;
    world.header.date = {30, 10, 2026};
    require(gods::after_property(world, 0, 1, callbacks).applied &&
                land.expired_date == calendar::pack({2026, 11, 30}),
            "Land god sets lease only on originally unowned property");
    world.players[0].status.god_info = 3;
    unsigned said = 99;
    callbacks.voice = [&](auto, auto category) { said = category; };
    require(gods::after_successful_purchase(world, random, 0, 1, callbacks).applied && said == 15 &&
                random.state() == 0,
            "Blessing upgrade to topfloor uses fixed category with no random voice draw");
}
std::vector<std::uint32_t> parse(std::string line, std::size_t columns = 6) {
    std::replace(line.begin(), line.end(), ',', ' ');
    std::istringstream stream(line);
    std::vector<std::uint32_t> row;
    std::uint32_t value;
    while (stream >> value)
        row.push_back(value);
    require(stream.eof() && row.size() == columns, "Malformed spinner fixture");
    return row;
}
void spinner_oracle(const std::filesystem::path &directory) {
    std::ifstream file(directory / "spinner.csv");
    require(bool(file), "Missing original spinner capture");
    std::string line;
    std::getline(file, line);
    require(line == "kind,seed,stop_frame,amount,random_after,frames" ||
                line == "kind,seed,stop_frame,amount,random_after,frames\r",
            "Original spinner fixture header");
    constexpr std::array<std::uint32_t, 12> seeds{
        0, 1, 2, 3, 42, 12345, 0x7fffffff, 0x80000000, 0xffffffff, 0x12345678, 0xdeadbeef, 987654321};
    constexpr std::array<unsigned, 9> stops{0, 1, 9, 10, 11, 39, 40, 41, 100};
    constexpr std::array<std::uint8_t, 4> kinds{1, 2, 5, 6};
    std::size_t count = 0;
    while (std::getline(file, line)) {
        const auto row = parse(line);
        require(count < 432 && row[0] == kinds[count / 108] && row[1] == seeds[(count / 9) % 12] &&
                    row[2] == stops[count % 9],
                "Spinner fixture must cover every scheduledcase once in order");
        Random random(row[1]);
        gods::MoneySpinner spinner(static_cast<std::uint8_t>(row[0]));
        unsigned frames = 0;
        while (!spinner.finished()) {
            require(frames < 1000, "Spinner cannot hang");
            spinner.tick(random, row[2] == 0);
            ++frames;
            if (frames == row[2])
                spinner.stop();
        }
        if (spinner.amount() != static_cast<int>(row[3]) || random.state() != row[4] || frames != row[5])
            throw std::runtime_error("Spinner original mismatch atrow" + std::to_string(count) + " amount" +
                                     std::to_string(spinner.amount()) + " expected" + std::to_string(row[3]) +
                                     " frames" + std::to_string(frames) + " expected" +
                                     std::to_string(row[5]));
        ++count;
    }
    require(count == 432, "Empty or truncated spinner comparison cannotpass");
    std::cout << "Matched432 original spinner outcomes, frames and RNGstates\n";
}
void attach_oracle(const std::filesystem::path &directory) {
    std::ifstream file(directory / "attach.csv");
    require(bool(file), "Missing original attachment capture");
    std::string line;
    std::getline(file, line);
    require(line.starts_with("object,actor,seed,node_flags,rng,god,"), "Attachment fixture header");
    constexpr std::array<std::uint32_t, 4> seeds{0, 1, 42, 0xdeadbeef};
    std::size_t count = 0;
    while (std::getline(file, line)) {
        const auto expected = parse(line, 98);
        require(count < 256 && expected[0] == count / 16 + 1 && expected[1] == (count / 4) % 4 &&
                    expected[2] == seeds[count % 4],
                "Attachment capture must cover every object/player/seed once");
        const auto id = static_cast<std::uint16_t>(expected[0]);
        const auto actor = expected[1];
        auto world = fixture();
        Random random(expected[2]);
        gods::Callbacks callbacks;
        std::vector<std::uint32_t> payment_values;
        callbacks.pay = [&](int payer, int recipient, int amount, unsigned flags) {
            payment_values.insert(payment_values.end(),
                                  {static_cast<std::uint32_t>(payer), static_cast<std::uint32_t>(recipient),
                                   static_cast<std::uint32_t>(amount), flags});
        };
        callbacks.choose_money = [](auto kind) {
            return kind == 1 ? 701 : kind == 2 ? 6789 : kind == 5 ? 500 : 1234;
        };
        for (auto &player : world.players) {
            player.state.node_id = 2;
            player.state.traffic_method = 1;
            player.state.dice_count = 2;
            player.state.points = 65500;
            player.status.god_luck_modifier = 32700;
            player.status.money_fortune_modifier = -32700;
            player.status.other_fortune_modifier = 100;
            player.state.cards = {1, 3, 5, 7, 9};
            for (std::size_t i = 0; i < 15; ++i)
                player.state.tool_amounts[i] = static_cast<std::uint8_t>(i % 3);
        }
        world.nodes[1].flags = 0xff010abc;
        world.objects[id - 1].node_id = 1;
        require(gods::attach(world, random, actor, id, 1, callbacks).supported,
                "Attachment adapter boundary");
        const auto &player = world.players[actor];
        const auto &object = world.objects[id - 1];
        std::vector<std::uint32_t> actual{id,
                                          actor,
                                          expected[2],
                                          world.nodes[1].flags,
                                          random.state(),
                                          player.status.god_info,
                                          std::bit_cast<std::uint16_t>(player.status.god_luck_modifier),
                                          std::bit_cast<std::uint16_t>(player.status.money_fortune_modifier),
                                          std::bit_cast<std::uint16_t>(player.status.other_fortune_modifier),
                                          std::bit_cast<std::uint32_t>(player.state.cash),
                                          player.status.cumulative_income,
                                          player.state.points,
                                          player.state.traffic_method,
                                          player.state.dice_count,
                                          object.node_id,
                                          object.remaining_days,
                                          object.attached_player};
        actual.insert(actual.end(), player.state.cards.begin(), player.state.cards.end());
        actual.insert(actual.end(), player.state.tool_amounts.begin(), player.state.tool_amounts.end());
        actual.insert(actual.end(), world.card_supply.begin(), world.card_supply.end());
        actual.insert(actual.end(), world.tool_supply.begin(), world.tool_supply.end());
        actual.push_back(static_cast<std::uint32_t>(payment_values.size() / 4));
        payment_values.resize(12);
        actual.insert(actual.end(), payment_values.begin(), payment_values.end());
        if (actual != expected) {
            const auto mismatch = static_cast<std::size_t>(
                std::mismatch(actual.begin(), actual.end(), expected.begin()).first - actual.begin());
            throw std::runtime_error("Original attach mismatch row" + std::to_string(count) + " column" +
                                     std::to_string(mismatch) + " actual" + std::to_string(actual[mismatch]) +
                                     " expected" + std::to_string(expected[mismatch]));
        }
        ++count;
    }
    require(count == 256, "Incomplete attachment capture cannotpass");
    std::cout << "Matched256 original attachment state/inventory/payment-boundary cases\n";
}
} // namespace
int main(int argc, char **argv) {
    try {
        attachment();
        card_reward_and_loss_presentations();
        encounters();
        property_rules();
        if (argc == 3 && std::string(argv[1]) == "--oracle") {
            spinner_oracle(argv[2]);
            attach_oracle(argv[2]);
        } else if (argc != 1)
            throw std::invalid_argument("Usage:gods_tests [--oracle capture-directory]");
        std::cout << "God rules tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
