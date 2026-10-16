// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/events.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {
using namespace rich4;
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class Function> void invalid(Function function) {
    try {
        function();
    } catch (const SaveError &) {
        return;
    }
    throw std::runtime_error("Expected malformed event-state rejection");
}
World fixture() {
    World world;
    world.header.player_count = 4;
    world.header.price_index = 1;
    world.header.date = {5, 10, 2026};
    world.players.resize(4);
    for (std::size_t i = 0; i < 4; ++i) {
        auto &player = world.players[i];
        player.state.slot = i;
        player.state.controller_flags = i == 0 ? 1 : 2;
        player.state.cash = 100000;
        player.state.bank_balance = 100000;
        player.state.dice_count = 1;
    }
    world.lands.resize(4);
    world.facilities.resize(2);
    world.commercials.resize(3);
    for (std::size_t i = 1; i < world.lands.size(); ++i) {
        auto &land = world.lands[i];
        land.id = static_cast<std::uint32_t>(i);
        land.land_price = 2000;
        land.price_per_level = 1000;
        land.economy.street_name = i < 3 ? "\xa4\x40" : "\xa4\x41";
    }
    world.facilities[1].id = 1;
    world.facilities[1].land_price = 10000;
    world.facilities[1].price_per_level() = 1000;
    for (std::size_t i = 1; i < world.commercials.size(); ++i) {
        auto &company = world.commercials[i];
        company.id = static_cast<std::uint32_t>(i);
        company.stock_index = static_cast<std::uint8_t>(i - 1);
        company.funds = 20000;
    }
    world.commercials[1].commerce_type = 4;
    for (auto &stock : world.stocks)
        stock.float_bits = {0x42c80000, 0x42c80000, 0x42c80000, 0, 0};
    std::iota(world.news_deck.begin(), world.news_deck.end(), std::uint8_t(0));
    std::iota(world.fortune_deck.begin(), world.fortune_deck.end(), std::uint8_t(0));
    return world;
}

void selection_rules() {
    auto world = fixture();
    world.prison_roster[4] = 1;
    require(!news_eligible(world, 0), "Special prisoners do not make normal-player release news eligible");
    require(select_news(world).event_id == 6 && select_news(world).skipped == 6,
            "Stored news order skips ineligible entries rather than drawing replacement RNG");
    world.news_cursor = 35;
    const auto selected = select_news(world);
    require(selected.event_id == 35 && selected.next_cursor == 0, "News cursor wraps at36 without reshuffle");
    world.news_deck.fill(0);
    world.news_cursor = 0;
    invalid([&] { (void)select_news(world); });
    world.news_deck[0] = 36;
    invalid([&] { (void)select_news(world); });
    world = fixture();
    require(fortune_eligible(world, 0, 15) == 14, "Original ASM remaps bike fine on foot to pedestrian fine");
    world.players[0].state.traffic_method = 2;
    require(fortune_eligible(world, 0, 10) == 11 && !fortune_eligible(world, 0, 13),
            "Transport event remapping and rejection");
    world.header.map_id = 2;
    require(fortune_eligible(world, 0, 34) == 42, "Map-specific fortune handler offset");
    world.header.stage_id = 1;
    require(!fortune_eligible(world, 0, 34), "Map-specific prison cards unavailable onstage1");
    world.fortune_deck.fill(3);
    world.fortune_cursor = 36;
    Random random(123);
    const auto state = random.state();
    require(apply_fortune(world, random, 0).supported && world.fortune_cursor == 0 &&
                world.players[0].status.days_rejected_by_bank == 30 && random.state() == state,
            "A selected deterministic event changes saved cursor/state without consuming RNG");
}

void boundaries_are_explicit() {
    auto world = fixture();
    Random random(12);
    world.news_deck[0] = 7;
    const auto before = random.state();
    require(!apply_news_id(world, random, 7).supported && random.state() == before,
            "Missing mandatory auction remains an explicit gate in the handler and does not consume RNG");
    {
        auto deck = world;
        Random deck_random(12);
        const auto result = apply_news(deck, deck_random);
        require(result.supported && result.event_id != 7 && deck.news_cursor != 0,
                "A news card whose handler cannot run is skipped like an ineligible card, never repeated");
    }
    world.facilities[1].level = 1;
    require(!news_eligible(world, 15) && !apply_news_id(world, random, 15).supported &&
                random.state() == before,
            "News 15 eligibility matches its handler (built ordinary land), so built facilities alone no "
            "longer select the original's divide-by-zero case");
    world.lands[1].economy.level = 1;
    require(news_eligible(world, 15), "News 15 stays eligible once an ordinary land is built");
    world.lands[1].economy.level = 0;
    world.news_deck[0] = 15;
    world.news_deck[1] = 15;
    {
        Random deck_random(12);
        const auto result = apply_news(world, deck_random);
        require(result.supported && result.event_id != 15 && world.news_cursor > 2,
                "Facility-only boards skip news 15 and the deck moves on");
    }
    world = fixture();
    world.players[1].state.cards[0] = 3;
    require(!apply_fortune_id(world, random, 0, 5).supported && world.players[1].state.cards[0] == 3,
            "Human card selection cannot silently become random theft");
}

void world_news() {
    auto world = fixture();
    Random random(1);
    world.prison_roster[1] = 1;
    world.players[1].status.days_in_prison = 126;
    require(apply_news_id(world, random, 1).supported && world.players[1].status.days_in_prison == 1,
            "Sentence extension adds3 and masks original highbit");
    require(apply_news_id(world, random, 0).supported && world.players[1].status.days_in_prison == 0x80 &&
                world.prison_roster[1] == 0,
            "Release uses pending-release marker, not immediate relocation");
    world = fixture();
    for (std::size_t i = 1; i < world.lands.size(); ++i) {
        world.lands[i].economy.owner_code = 1;
        world.lands[i].economy.level = 1;
    }
    world.players[2].status.days_in_hotel = 4;
    world.facilities[1].level = 1;
    world.facilities[1].owner_code = 2;
    world.facilities[1].facility_type = 1;
    const auto initial_rng = random.state();
    require(apply_news_id(world, random, 18).supported && random.state() != initial_rng,
            "Street damage consumes a target choice");
    const auto destroyed_lands = std::count_if(world.lands.begin() + 1, world.lands.end(),
                                               [](const auto &land) { return land.economy.level == 0; });
    require(destroyed_lands == 1 || destroyed_lands == 2 || world.facilities[1].level == 0,
            "Damage follows selected street group or selected facility");
    world = fixture();
    world.lands.resize(2);
    world.facilities.resize(1);
    require(apply_news_id(world, random, 14).supported && world.lands[1].land_price == 1399,
            "Original x87 truncate retains below-integer binary64 0.7 boundary");
    world = fixture();
    world.lands[1].economy.owner_code = 2;
    world.lands[2].economy.owner_code = 1;
    require(apply_news_id(world, random, 8).supported && world.players[0].state.cash == 110000 &&
                world.players[1].state.cash == 100000,
            "Property award preserves first-player tie break");
    world.players[0].state.loan = 1;
    world.players[1].state.bank_balance = 12345;
    require(apply_news_id(world, random, 23).supported && world.players[0].state.bank_balance == 100000 &&
                world.players[1].state.bank_balance == 13579,
            "Bank reward excludes debtors and truncates10percent");
    require(apply_news_id(world, random, 22).supported && world.players[2].status.days_bank_no_loans == 15,
            "Bank restriction is assigned to all active players");
    world = fixture();
    world.commercials.resize(2);
    world.commercials[1].funds = 15000;
    world.commercials[1].funds_change = 100;
    std::uint32_t updated = 0;
    EventCallbacks callbacks;
    callbacks.update_stocks = [&](std::uint32_t id) { updated = id; };
    require(apply_news_id(world, random, 35, callbacks).supported && world.commercials[1].funds == 30000 &&
                world.commercials[1].funds_change == 30100 && updated == 1 &&
                world.stocks[0].news_movement == 0x10,
            "Profit-doubling event adds twice old profit to cumulative counter and emits exact stock flag");
    world.stock_history_cursor = 0;
    world.stocks[0].suspension_days = 1;
    require(apply_news_id(world, random, 28).supported && world.stocks[0].suspension_days == 0,
            "Resume event selects an actually suspended stock");
}

void money_and_gods() {
    auto world = fixture();
    Random random(1);
    std::vector<std::tuple<std::int32_t, std::int32_t, std::int32_t, std::uint32_t>> payments;
    EventCallbacks callbacks;
    callbacks.pay = [&](std::int32_t from, std::int32_t to, std::int32_t amount, std::uint32_t flags) {
        payments.emplace_back(from, to, amount, flags);
        // Tests assert delegation, not a duplicate implementation of session finance.
    };
    callbacks.set_loan_due_date = [&](std::size_t actor) { world.players[actor].status.loan_due_date = 123; };
    world.players[0].status.days_assurance = 2;
    const auto start = random.state();
    require(apply_fortune_id(world, random, 0, 18, callbacks).supported && payments.size() == 2 &&
                payments[0] == std::make_tuple(0, -1, 600, 0U) &&
                payments[1] == std::make_tuple(101, 0, 600, 1U) && random.state() == start,
            "Expense delegates cash/bank/bankruptcy before requested-loss insurance");
    payments.clear();
    world.players[0].status.money_fortune_modifier = -1;
    require(apply_fortune_id(world, random, 0, 18, callbacks).supported && std::get<2>(payments[0]) == 1200,
            "Negative financial modifier doubles adverse expense");
    payments.clear();
    world.players[0].status.money_fortune_modifier = 101;
    require(apply_fortune_id(world, random, 0, 18, callbacks).supported && payments.empty(),
            "High modifier prevents expenses");
    require(apply_fortune_id(world, random, 0, 20, callbacks).supported &&
                world.players[0].state.cash == 102000 && world.players[0].status.cumulative_income == 2000,
            "Same high modifier doubles beneficial cash and income");
    world.players[0].status.money_fortune_modifier = 0;
    world.players[0].state.loan = 200;
    require(apply_fortune_id(world, random, 0, 2, callbacks).supported &&
                world.players[0].state.loan == 10200 && world.players[0].status.loan_due_date == 123,
            "Fraudulent loan adds rather than replacing outstanding loan");
    world.players[0].state.loan = 3'000'000'000LL;
    require(apply_fortune_id(world, random, 0, 2, callbacks).supported &&
                world.players[0].state.loan == 3'000'010'000LL,
            "fortune debt event cannot truncate outstanding MOD loan above 2.1b");
    world.players[0].status.days_rejected_by_bank = 250;
    require(apply_fortune_id(world, random, 0, 3, callbacks).supported &&
                world.players[0].status.days_rejected_by_bank == 24,
            "Bank rejection adds30 with byte wrap, unlike incorrect recoveredC assignment");
    payments.clear();
    world.players[1].state.bank_balance = 2147483647;
    require(apply_fortune_id(world, random, 0, 4, callbacks).supported &&
                std::get<2>(payments[0]) == 214748367,
            "Bank theft uses stored binary32 0.1, not exact decimal10percent or double rounding");
    world = fixture();
    world.players[0].state.bank_balance = 15'100'000'000LL;
    require(apply_news_id(world, random, 23, callbacks).supported &&
                world.players[0].state.bank_balance == 16'610'000'000LL &&
                world.players[0].state.cash == 100000,
            "news bank bonus must credit 1.51b without truncating 15.1b existing balance");
    world = fixture();
    world.header.price_index = 3;
    world.lands[1].economy.owner_code = 1;
    world.lands[1].land_price = 39;
    world.lands[1].price_per_level = 0;
    payments.clear();
    require(apply_news_id(world, random, 12, callbacks).supported &&
                payments[0] == std::make_tuple(0, -1, 3, 0U),
            "Property tax truncates before multiplying price index");
    std::vector<std::uint8_t> spoken;
    callbacks.voice = [&](std::size_t, std::uint8_t category) { spoken.push_back(category); };
    const auto before_voice = random.state();
    require(apply_fortune_id(world, random, 0, 21, callbacks).supported &&
                spoken == std::vector<std::uint8_t>{8} && random.state() == before_voice,
            "Inflation scales voice thresholds as well as reward;6000 atindex3 is still base2000");
}

void physical_and_inventory() {
    auto world = fixture();
    Random random(5);
    EventCallbacks callbacks;
    std::size_t target = 99;
    std::uint8_t days = 0, mode = 255;
    callbacks.try_mianzui_jiahuo = [](std::size_t) { return 2; };
    callbacks.disappear = [&](std::size_t actor, std::uint8_t d, std::uint8_t m) {
        target = actor;
        days = d;
        mode = m;
    };
    callbacks.add_prison = [&](std::size_t actor, std::uint8_t d) {
        target = actor;
        days = d;
    };
    callbacks.add_hospital = callbacks.add_prison;
    world.players[0].status.other_fortune_modifier = -1;
    require(apply_fortune_id(world, random, 0, 7, callbacks).supported && target == 2 && days == 6 &&
                mode == 1,
            "Physical modifier doubles duration before immunity/redirect and absence callback");
    require(apply_fortune_id(world, random, 0, 48, callbacks).supported && days == 18,
            "Last map-specific prison variant preserves nine-day base sentence");
    world = fixture();
    world.players[0].state.cards = {3, 1};
    world.players[0].state.tool_amounts[4] = 2;
    world.players[0].state.traffic_method = 1;
    world.players[0].state.dice_count = 2;
    world.players[0].state.points = 65530;
    const auto initial_rng = random.state();
    require(apply_fortune_id(world, random, 0, 32).supported && world.players[0].state.points == 65535 &&
                world.tool_supply[4] == 3 && world.players[0].state.traffic_method == 0 &&
                world.players[0].state.cards[0] == 0 && random.state() == initial_rng,
            "MOD: transport converts before sale; points saturate instead of wrapping; speech consumes "
            "no RNG");
    world = fixture();
    world.players[1].state.cards[0] = 4;
    callbacks.choose_card = [](std::size_t, std::size_t) { return std::uint8_t(0); };
    const auto before_cancel = random.state();
    require(apply_fortune_id(world, random, 0, 5, callbacks).supported &&
                world.players[1].state.cards[0] == 4 && world.players[0].state.cards[0] == 0 &&
                random.state() != before_cancel,
            "Human may cancel card theft while original final-voice draw still occurs");
}

void every_handler_is_explicit() {
    // Generated state makes every original handler's prerequisites available.
    // Shared effects are recorded/delegated; their own modules test the actual
    // payment, auction, stock and physical rules, not these simple callbacks.
    for (int family = 0; family < 2; ++family)
        for (std::uint8_t id = 0; id < (family == 0 ? 36 : 49); ++id) {
            auto world = fixture();
            Random random(11);
            world.lands[1].economy.owner_code = 1;
            world.lands[1].economy.level = 1;
            world.lands[2].economy.owner_code = 1;
            world.lands[2].economy.level = 0;
            world.players[1].state.cards[0] = 3;
            world.players[0].stocks[0].amount = 100;
            world.stocks[0].suspension_days = 2;
            world.commercials[1].owner_code = 1;
            world.commercials[2].owner_code = 2;
            EventCallbacks callbacks;
            callbacks.pay = [](auto, auto, auto, auto) {};
            callbacks.add_prison = [](auto, auto) {};
            callbacks.add_hospital = callbacks.add_prison;
            callbacks.disappear = [](auto, auto, auto) {};
            callbacks.try_mianzui_jiahuo = [](std::size_t actor) { return static_cast<std::int32_t>(actor); };
            callbacks.set_loan_due_date = [](auto) {};
            callbacks.sell_stock = [](auto, auto, auto, auto) {};
            callbacks.auction = [](auto) {};
            callbacks.choose_card = [](auto, auto) { return std::uint8_t(3); };
            callbacks.area_damage = [](auto, auto, auto, auto) {};
            callbacks.update_stocks = [](auto) {};
            const auto result = family == 0 ? apply_news_id(world, random, id, callbacks)
                                            : apply_fortune_id(world, random, 0, id, callbacks);
            require(result.supported && result.event_id == id && !result.message.empty(),
                    "Every original news/fortune ID has an explicit handler, not a generic random fallback");
        }
}
} // namespace
int main() {
    try {
        selection_rules();
        boundaries_are_explicit();
        world_news();
        money_and_gods();
        physical_and_inventory();
        every_handler_is_explicit();
        std::cout << "Event rule tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
