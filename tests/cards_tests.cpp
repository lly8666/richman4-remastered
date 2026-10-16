// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/calendar.hpp"
#include "rich4/cards.hpp"
#include <algorithm>
#include <bit>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace rich4;
void require(bool v, const char *message) {
    if (!v)
        throw std::runtime_error(message);
}
World fixture() {
    World w;
    w.header.price_index = 2;
    w.header.date = {31, 1, 2000};
    w.players.resize(4);
    w.nodes.resize(8);
    w.lands.resize(4);
    w.facilities.resize(3);
    for (std::size_t i = 0; i < 4; ++i) {
        auto &p = w.players[i].state;
        p.slot = i;
        p.controller_flags = 1;
        p.x = 100 + 10 * i;
        p.y = 100;
        p.node_id = i + 1;
        p.cash = 10000;
        p.bank_balance = 50000;
        p.dice_count = 1;
    }
    w.nodes[1].type_and_index = 2001;
    w.nodes[2].type_and_index = 2002;
    w.nodes[3].type_and_index = 4001;
    for (std::size_t i = 1; i < w.lands.size(); ++i) {
        auto &p = w.lands[i];
        p.land_price = 1000;
        p.price_per_level = 500;
        p.economy.street_name = i == 3 ? "other" : "road";
        p.economy.owner_code = std::uint8_t(i);
        p.economy.level = std::uint8_t(i);
        p.expired_date = 0x12340000 + unsigned(i);
    }
    for (std::size_t i = 1; i < w.facilities.size(); ++i) {
        auto &p = w.facilities[i];
        p.land_price = 2000;
        p.price_per_level() = 1000;
        p.owner_code = std::uint8_t(i);
        p.level = 3;
        p.facility_type = 1;
    }
    w.special_actors[0].x = 150;
    return w;
}
CardResult play(World &w, std::uint8_t id, std::optional<CardTarget> target = {},
                const CardCallbacks &cb = {}, std::optional<CardTarget> choice = {}) {
    w.players[0].state.cards = {id};
    Random random(71);
    auto r = apply_card(w, random, 0, {id, target, choice}, cb);
    require(random.state() == 71, "manual card effects must not add random draws");
    return r;
}
void target_filters_and_passive() {
    auto w = fixture();
    Random random(3);
    for (std::uint8_t id = 18; id <= 21; ++id) {
        w.players[0].state.cards = {id};
        const auto r = apply_card(w, random, 0, {id}, {});
        require(r.status == CardUseStatus::invalid && w.players[0].state.cards[0] == id,
                "passive cards cannot be manually consumed");
    }
    auto targets = legal_card_targets(w, 0, 11);
    require(std::find(targets.begin(), targets.end(), CardTarget{CardTargetKind::property, 2001}) ==
                targets.end(),
            "monster excludes own buildings");
    require(std::find(targets.begin(), targets.end(), CardTarget{CardTargetKind::property, 2002}) !=
                targets.end(),
            "monster includes opponent buildings");
    require(legal_card_targets(w, 0, 2).size() == 3, "cash equalization excludes self and special actors");
    require(legal_card_targets(w, 0, 6).size() == 5, "turn-around includes self and visible special actors");
    CardCallbacks cb;
    cb.nearby_instances = [](auto) { return std::vector<std::uint16_t>{2002, 0x8006}; };
    require(legal_card_targets(w, 0, 11, cb) == std::vector<CardTarget>{{CardTargetKind::property, 2002}},
            "viewport filtering property");
    require(legal_card_targets(w, 0, 2, cb).size() == 2, "combined actor masks retain both slots");
    cb.choose_target = [](auto, auto) { return std::optional<CardTarget>{}; };
    require(play(w, 2, {}, cb).status == CardUseStatus::cancelled && w.players[0].state.cards[0] == 2,
            "cancel preserves hand");
    require(play(w, 11, CardTarget{CardTargetKind::property, 2001}).status == CardUseStatus::invalid &&
                w.players[0].state.cards[0] == 11,
            "invalid target preserves hand");
}
void mod_wide_equalizing_cards() {
    auto w = fixture();
    w.players[0].state.cash = 10'000'000'000LL;
    w.players[1].state.cash = 30'000'000'000LL;
    w.players[2].state.cash = 50'000'000'000LL;
    w.players[3].state.cash = 70'000'000'000LL;
    require(play(w, 1).used(), "cash equalizer should succeed above signed32");
    for (const auto &player : w.players)
        require(player.state.cash == 40'000'000'000LL,
                "rich equalizer must conserve 160b cash across four players");
    w = fixture();
    w.players[0].state.cash = 15'100'000'000LL;
    w.players[1].state.cash = 44'900'000'000LL;
    require(play(w, 2, CardTarget{CardTargetKind::actor, 1}).used() &&
                w.players[0].state.cash == 30'000'000'000LL &&
                w.players[1].state.cash == 30'000'000'000LL,
            "poor equalizer must not wrap 60b cash sum into signed32");
}
void money_and_ownership() {
    auto w = fixture();
    w.players[0].state.cash = 100;
    w.players[1].state.cash = 102;
    w.players[2].state.cash = 106;
    w.players[3].state.controller_flags = 0;
    require(play(w, 1).used(), "uniform wealth used");
    require(w.players[0].state.cash == 102 && w.players[2].state.cash == 102 &&
                w.players[3].state.cash == 10000,
            "uniform cash divides active count and drops remainder");
    w = fixture();
    w.players[0].state.cash = 1;
    w.players[1].state.cash = 1002;
    require(play(w, 2, CardTarget{CardTargetKind::actor, 1}).used(), "pair average used");
    require(w.players[0].state.cash == 501 && w.players[1].state.cash == 501 &&
                w.players[1].status.hostility[0] == 5,
            "pair average and hostility");
    w = fixture();
    w.nodes[1].type_and_index = 2002;
    w.lease_mode = 5;
    CardCallbacks cb;
    cb.pay = [&](auto from, auto to, auto amount, auto flags) {
        require(flags == 0, "purchase credits bank");
        w.players[from].state.cash -= amount;
        w.players[to].state.bank_balance += amount;
    };
    require(play(w, 3, {}, cb).used(), "purchase used");
    require(w.lands[2].economy.owner_code == 1 && w.players[0].state.cash == 6000 &&
                w.players[1].state.bank_balance == 54000,
            "purchase exact base plus building value");
    require(w.lands[2].expired_date == calendar::add_packed_interval(0x07d0011f, 0x100),
            "purchase renews configured packed lease");
    w = fixture();
    const auto old_expiry = w.lands[1].expired_date;
    require(play(w, 4, CardTarget{CardTargetKind::property, 2002}).used(), "exchange land used");
    require(w.lands[1].economy.owner_code == 2 && w.lands[2].economy.owner_code == 1 &&
                w.lands[1].expired_date == old_expiry,
            "exchange only ownership");
    w.lands[2].economy.chain_store = 1;
    require(play(w, 5, CardTarget{CardTargetKind::property, 2002}).used(), "exchange house used");
    require(w.lands[1].economy.level == 2 && w.lands[1].economy.chain_store == 1 &&
                w.lands[1].economy.owner_code == 2,
            "exchange only building type/level");
    cb.auction_property = [](auto code, auto seller) {
        require(code == 2001 && seller == 0, "auction-card seller is actor");
        return false;
    };
    require(play(w, 8, {}, cb).used() && !w.lands[1].economy.owner_code && !w.lands[1].expired_date &&
                w.lands[1].economy.level == 2,
            "unsold auction clears ownership and expiry, retains house");
}
void buildings_and_street_effects() {
    auto w = fixture();
    w.lands[1].economy.level = 4;
    require(play(w, 7).used() && w.lands[1].economy.level == 1 && w.lands[1].economy.chain_store == 1,
            "chain conversion clamps level");
    w = fixture();
    w.lands[2].economy.chain_store = 1;
    require(play(w, 9, CardTarget{CardTargetKind::property, 2001}).used(), "angel used");
    require(w.lands[1].economy.level == 2 && w.lands[2].economy.level == 2 && w.lands[3].economy.level == 3,
            "angel same street and chain restriction");
    require(play(w, 10, CardTarget{CardTargetKind::property, 2001}).used() && !w.lands[1].economy.level &&
                !w.lands[2].economy.level && !w.lands[2].economy.chain_store &&
                w.lands[2].economy.owner_code == 2,
            "devil destroys same street without confiscating");
    w = fixture();
    require(play(w, 11, CardTarget{CardTargetKind::property, 2002}).used() && !w.lands[2].economy.level &&
                w.players[1].status.hostility[0] == 120,
            "monster hostility depends on removed levels");
    w = fixture();
    w.lands[2].economy.chain_store = 1;
    require(play(w, 12, CardTarget{CardTargetKind::property, 2002}).used() && !w.lands[2].economy.level &&
                !w.lands[2].economy.chain_store,
            "demolition clears chain building completely");
    require(play(w, 27, CardTarget{CardTargetKind::property, 2001}).used() &&
                w.lands[1].temporary_state == 0x50 && w.lands[2].temporary_state == 0x50 &&
                !w.lands[3].temporary_state,
            "rise price marks whole street");
    w.facilities[1].facility_type = 4;
    w.facilities[1].flags_30 = 7;
    require(play(w, 28, CardTarget{CardTargetKind::property, 4001}).used() &&
                w.facilities[1].temporary_state == 0x51 && !w.facilities[1].flags_30,
            "seal clears research facility flag");
}
void statuses_and_defense() {
    auto w = fixture();
    require(play(w, 14, CardTarget{CardTargetKind::actor, 0}).used() &&
                w.players[0].status.days_stopping == 0x80,
            "self stop uses release marker");
    require(play(w, 30, CardTarget{CardTargetKind::actor, 0}).used() &&
                w.players[0].status.days_tortoise_walking == 2,
            "self tortoise lasts two");
    require(play(w, 30, CardTarget{CardTargetKind::actor, 4}).used() &&
                w.special_actors[0].days_tortoise_walking == 3,
            "special actor tortoise lasts three");
    w.players[1].status.days_in_hotel = 1;
    w.players[2].status.days_sleep_walking = 3;
    require(play(w, 15).used() && !w.players[1].status.days_sleeping &&
                w.players[2].status.days_sleeping == 5 && !w.players[2].status.days_sleep_walking &&
                w.special_actors[0].days_sleeping == 5,
            "hibernation excludes hotel and clears sleepwalk");
    w = fixture();
    w.players[1].state.cards = {21, 18};
    CardCallbacks cb;
    cb.try_mianzui_jiahuo = [&](auto target) {
        consume_card(w.players[target].state.cards, w.card_supply, 21);
        return -1;
    };
    require(play(w, 16, CardTarget{CardTargetKind::actor, 1}, cb).used() &&
                !w.players[1].status.days_sleep_walking && !w.players[0].status.days_sleep_walking &&
                w.players[1].state.cards[0] == 18,
            "immunity prevents sleepwalk and revenge");
    w = fixture();
    w.players[1].state.cards = {18};
    w.players[1].state.traffic_method = 2;
    w.players[1].state.dice_count = 3;
    w.players[0].state.traffic_method = 1;
    w.players[0].state.dice_count = 2;
    require(play(w, 16, CardTarget{CardTargetKind::actor, 1}).used(), "sleepwalk with revenge used");
    require(w.players[1].status.days_sleep_walking == 5 && w.players[1].status.flags_102 == 2 &&
                w.players[1].state.tool_amounts[5] == 1 && w.players[1].state.dice_count == 1,
            "sleepwalker transport saved and returned");
    require(w.players[0].status.days_sleep_walking == 5 && w.players[0].state.tool_amounts[4] == 1 &&
                !w.players[0].status.total_winter_sleep_days && !w.players[1].state.cards[0],
            "revenge has distinct total-day accounting");
    w = fixture();
    w.players[1].state.cards = {19, 18};
    cb.try_mianzui_jiahuo = [](auto) { return 2; };
    cb.add_prison = [&](auto p, auto n) { w.players[p].status.days_in_prison += n; };
    require(play(w, 17, CardTarget{CardTargetKind::actor, 1}, cb).used() &&
                w.players[2].status.days_in_prison == 5 && !w.players[0].status.days_in_prison,
            "redirection prevents original victim revenge");
    w = fixture();
    w.players[0].status.allied_player = 3;
    w.players[2].status.allied_player = 1;
    w.players[1].status.allied_player = 4;
    w.players[3].status.allied_player = 2;
    require(play(w, 29, CardTarget{CardTargetKind::actor, 1}).used() &&
                w.players[0].status.allied_player == 2 && w.players[1].status.allied_player == 1 &&
                w.players[0].status.allied_days == 7 && !w.players[2].status.allied_player &&
                !w.players[3].status.allied_player,
            "alliance replaces both old pairs");
}
void inventory_objects_stocks_and_tax() {
    auto w = fixture();
    w.players[1].state.cards = {1};
    require(
        play(w, 13, CardTarget{CardTargetKind::actor, 1}, {}, CardTarget{CardTargetKind::card, 1}).used() &&
            w.players[0].state.cards[0] == 1 && !w.players[1].state.cards[0] &&
            w.players[1].status.hostility[0] == 200,
        "robbery transfers chosen card");
    w = fixture();
    w.players[1].state.tool_amounts[4] = 1;
    require(
        play(w, 13, CardTarget{CardTargetKind::actor, 1}, {}, CardTarget{CardTargetKind::tool, 5}).used() &&
            w.players[0].state.tool_amounts[4] == 1 && !w.players[1].state.tool_amounts[4] &&
            w.players[1].status.hostility[0] == 20,
        "robbed tool uses original card-price hostility accident");
    w = fixture();
    CardCallbacks cb;
    w.players[0].status.god_info = 2;
    w.objects[1].kind = 5;
    w.players[0].status.attached_bomb = 20;
    cb.remove_object = [&](auto id) {
        require(id == 20, "bomb removal ID");
        w.players[0].status.attached_bomb = 0;
    };
    cb.detach_god = [&](auto p) { w.players[p].status.god_info = 0; };
    require(play(w, 22, {}, cb).used() && !w.players[0].status.attached_bomb && !w.players[0].status.god_info,
            "send-god removes both attached effects");
    w = fixture();
    w.objects[0].kind = 5;
    w.objects[0].node_id = 5;
    w.nodes[5].x = 110;
    w.nodes[5].y = 100;
    w.objects[1].kind = 1;
    w.objects[1].node_id = 6;
    w.nodes[6].x = 150;
    w.nodes[6].y = 100;
    cb.nearby_instances = [](auto) { return std::vector<std::uint16_t>{0x8200, 0x8100}; };
    cb.attach_god = [&](auto p, auto id) { w.players[p].status.god_info = std::uint8_t(id); };
    require(play(w, 23, {}, cb).used() && w.players[0].status.god_info == 1,
            "summon chooses nearest attachable god, including harmful kind");
    w = fixture();
    cb = {};
    cb.market_closed = [] { return false; };
    cb.update_stocks = [&](auto one) {
        require(one == 1, "stock one-based update");
        w.stocks[0].float_bits[2] = std::bit_cast<std::uint32_t>(90.F);
    };
    require(play(w, 24, CardTarget{CardTargetKind::stock, 0}, cb).used() && w.stocks[0].news_movement == 0x20,
            "red card sets limit-up movement");
    require(play(w, 25, CardTarget{CardTargetKind::stock, 0}, cb).used() && w.stocks[0].news_movement == 2,
            "black card sets limit-down movement");
    w = fixture();
    w.players[1].state.cash = 20003;
    w.players[1].state.cards = {19};
    cb = {};
    cb.try_frame = [](auto p, auto mode) {
        require(p == 1 && mode == 2, "tax frame mode");
        return 2;
    };
    cb.pay = [&](auto payer, auto payee, auto amount, auto flags) {
        require(payer == 2 && payee == 0 && amount == 2000 && flags == 0, "tax recomputed after redirect");
    };
    require(play(w, 26, CardTarget{CardTargetKind::actor, 1}, cb).used() &&
                w.players[1].status.hostility[0] == 40,
            "tax hostility precedes redirect");
}
// Speech lines follow the original handlers' _rich4_player_say call order (docs/ui/messages.md §1.4).
void speech_order() {
    std::vector<std::string> log;
    CardCallbacks cb;
    cb.say = [&](auto speaker, auto row, auto card, auto mood) {
        log.push_back("say " + std::to_string(speaker) + "/" + std::to_string(row) + "/" +
                      std::to_string(card) + "/" + std::to_string(mood));
    };
    cb.voice = [&](auto slot, auto category) {
        log.push_back("voice " + std::to_string(slot) + "/" + std::to_string(category));
    };
    cb.turn_around = [&](auto slot) { log.push_back("turn " + std::to_string(slot)); };
    cb.passive_card = [&](auto slot, auto card) {
        log.push_back("popup " + std::to_string(slot) + "/" + std::to_string(card));
    };
    const auto expect = [&](std::vector<std::string> wanted, const char *message) {
        require(log == wanted, message);
        log.clear();
    };
    auto w = fixture();
    require(play(w, 6, CardTarget{CardTargetKind::actor, 1}, cb).used(), "turn card used");
    expect({"say 0/0/6/3", "turn 1", "say 1/2/6/2"}, "turn card: user, then victim");
    require(play(w, 6, CardTarget{CardTargetKind::actor, 0}, cb).used(), "self turn used");
    expect({"say 0/0/6/3", "turn 0", "say 0/1/6/0"}, "turn card on self uses row 1");
    w = fixture();
    w.nodes[1].type_and_index = 2002;
    cb.pay = [&](auto, auto, auto, auto) { log.push_back("pay"); };
    require(play(w, 3, {}, cb).used(), "purchase used");
    expect({"say 0/0/3/3", "pay", "say 1/2/3/1"}, "purchase: user, payment, seller");
    w = fixture();
    require(play(w, 4, CardTarget{CardTargetKind::property, 2002}, cb).used(), "exchange used");
    expect({"say 0/0/4/3", "say 1/2/4/2"}, "favourable exchange: user and loser");
    w = fixture();
    w.lands[2].economy.level = 0;
    require(play(w, 4, CardTarget{CardTargetKind::property, 2002}, cb).used(), "bad exchange used");
    expect({}, "unfavourable exchange is silent");
    w = fixture();
    require(play(w, 11, CardTarget{CardTargetKind::property, 2002}, cb).used(), "monster used");
    expect({"say 0/0/11/0", "say 1/2/11/1"}, "monster: user, then owner");
    require(play(w, 14, CardTarget{CardTargetKind::actor, 0}, cb).used(), "self stop used");
    expect({"say 0/1/14/3"}, "self stop says only row 1");
    require(play(w, 14, CardTarget{CardTargetKind::actor, 4}, cb).used(), "actor stop used");
    expect({"say 0/0/14/3"}, "special actor target has no victim line");
    w = fixture();
    w.players[1].state.cards = {18};
    cb.add_prison = [&](auto slot, auto) { log.push_back("prison " + std::to_string(slot)); };
    require(play(w, 17, CardTarget{CardTargetKind::actor, 1}, cb).used(), "frame used");
    expect({"say 0/0/17/3", "prison 1", "say 1/2/17/1", "popup 1/18", "say 1/0/18/0", "say 0/2/18/2",
            "prison 0"},
           "frame with revenge: victim, then revenge popup and lines, then the user is jailed");
    w = fixture();
    require(play(w, 16, CardTarget{CardTargetKind::actor, 1}, cb).used(), "sleepwalk used");
    expect({"say 0/0/16/3", "voice 1/21"}, "sleepwalk victim says event line 21");
}
} // namespace
int main() {
    try {
        target_filters_and_passive();
        mod_wide_equalizing_cards();
        money_and_ownership();
        buildings_and_street_effects();
        statuses_and_defense();
        inventory_objects_stocks_and_tax();
        speech_order();
        std::cout << "cards tests passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
