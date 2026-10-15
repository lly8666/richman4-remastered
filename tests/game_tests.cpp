// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/calendar.hpp"
#include "rich4/game.hpp"
#include "rich4/gods.hpp"
#include "rich4/holidays.hpp"
#include "rich4/random.hpp"
#include "rich4/trading.hpp"
#include "rich4/world.hpp"

#include <algorithm>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
rich4::World world(unsigned count = 4, unsigned land_count = 1) {
    std::vector<std::uint8_t> map(40);
    const std::array<unsigned, 5> counts{8, land_count, 1, 1, 2}, strides{40, 52, 56, 52, 28};
    const auto put32 = [&](std::size_t offset, std::uint32_t integer) {
        for (unsigned i = 0; i < 4; ++i)
            map.at(offset + i) = static_cast<std::uint8_t>(integer >> (i * 8));
    };
    for (unsigned i = 0; i < 5; ++i) {
        const auto offset = map.size();
        put32(i * 8, counts[i]);
        put32(i * 8 + 4, static_cast<std::uint32_t>(offset));
        map.resize(offset + (counts[i] + 1) * strides[i]);
    }
    for (unsigned i = 1; i <= 8; ++i) {
        const auto offset = 40 + i * 40;
        put32(offset, (64 + i * 32) | (128U << 16));
        put32(offset + 24, (i == 8 ? 1U : i + 1) | ((i == 1 ? 8U : i - 1) << 16));
        put32(offset + 36, 1);
    }
    rich4::NewGameOptions options;
    options.players.resize(count);
    auto value = rich4::World::from_map_resource(map, options);
    for (auto &stock : value.stocks)
        stock.flags_4 = 0; // Tests assign explicit commercial links only when needed.
    value.header = {0x26, {5, 10, 2026}, 0, 0, count, 1, 0, 200000, 1};
    value.nodes.resize(9);
    value.lands.resize(land_count + 1);
    for (unsigned i = 1; i < value.nodes.size(); ++i) {
        auto &node = value.nodes[i];
        node.id = i;
        node.x = static_cast<std::int16_t>(64 + i * 32);
        node.y = 128;
        node.neighbors = {static_cast<std::uint16_t>(i == 8 ? 1 : i + 1),
                          static_cast<std::uint16_t>(i == 1 ? 8 : i - 1), 0, 0};
        node.flags = 1;
    }
    value.players.resize(count);
    for (unsigned i = 0; i < count; ++i) {
        value.players[i].status.card_strategy = 0;
        value.players[i].status.stock_investment_ratio = 0;
        auto &p = value.players[i].state;
        p.slot = i;
        p.node_id = static_cast<std::uint16_t>(i + 1);
        p.last_node_id = static_cast<std::uint16_t>(i ? i : 8);
        p.x = static_cast<std::uint16_t>(value.nodes[p.node_id].x);
        p.y = 128;
        p.controller_flags = static_cast<std::uint8_t>(i ? 2 : 1);
        p.cash = 100000;
        p.bank_balance = 10000;
        p.dice_count = 1;
        p.cards.fill(0);
        p.tool_amounts.fill(0);
        value.nodes[p.node_id].flags |= 0x100U << i;
    }
    value.lands[1].id = 1;
    value.lands[1].land_price = 1000;
    value.lands[1].price_per_level = 400;
    value.lands[1].economy.street_name = "street";
    value.lands[1].economy.toll_by_level = {150, 300, 600, 1200, 2400, 4800};
    return rich4::World::from_save(rich4::SaveFile::decode(value.export_save()));
}
void property_nodes(rich4::World &value) {
    for (std::size_t i = 1; i < value.nodes.size(); ++i) {
        value.nodes[i].flags &= ~0xffU;
        value.nodes[i].type_and_index = 2001;
    }
}
void until(rich4::GameSession &game, const std::function<bool()> &done) {
    for (unsigned i = 0; i < 100000 && !done(); ++i)
        game.tick(1);
    require(done(), "state machine failed to reach expected state");
}
void first_landing(rich4::GameSession &game) {
    require(game.roll_dice(), "human roll rejected");
    until(game, [&] {
        return game.phase() == rich4::GamePhase::awaiting_decision ||
               game.phase() == rich4::GamePhase::advancing_turn || game.phase() == rich4::GamePhase::finished;
    });
}
void dice_and_paths() {
    rich4::GameSession game(world(), 1);
    rich4::Random expected(1);
    const unsigned face = expected.next() % 6 + 1;
    first_landing(game);
    for (unsigned i = 0; i < face; ++i)
        (void)expected.next();
    require(game.dice().size() == 1 && game.dice()[0] == face, "original die modulo differs");
    require(game.world().players[0].state.node_id == 1 + face, "walking distance differs");
    require(game.random_state() == expected.state(), "single path must consume one random call per step");
    auto value = world();
    value.players[0].state.last_node_id = 2;
    value.nodes[1].neighbors = {2, 3, 4, 0};
    value.nodes[1].flags |= 0x20000000U;
    rich4::GameSession blocked(std::move(value), 0);
    first_landing(blocked);
    require(blocked.world().players[0].state.node_id == 4, "last node or blocked edge was selected");
    value = world();
    value.players[0].state.last_node_id = 2;
    value.nodes[1].neighbors = {2, 0, 0, 0};
    rich4::GameSession fallback(std::move(value), 0);
    first_landing(fallback);
    rich4::Random once(0);
    (void)once.next();
    require(fallback.world().players[0].state.node_id == 2 && fallback.random_state() == once.state(),
            "fallback must return to last node without consuming randomness");
}
void parachute_marks_a_spawned_player() {
    auto value = world();
    auto &state = value.players[0].state;
    value.nodes[state.node_id].flags &= ~0x100U;
    state.x = state.y = 0;
    state.character = 7;
    rich4::GameSession game(std::move(value), 1);
    const auto effects = game.drain_effects();
    const auto &placed = game.world().players[0].state;
    require(placed.x != 0 && !effects.empty() && effects[0].kind == rich4::EffectKind::parachute &&
                effects[0].object_kind == 7 && effects[0].target.x == placed.x && effects[0].target.y == placed.y,
            "spawned player must parachute onto the chosen node (fcn_00418c55)");
}
void dice_effect_is_presentation_only() {
    rich4::GameSession game(world(), 1), silent(world(), 1);
    require(game.roll_dice(), "human roll rejected");
    const auto effects = game.drain_effects();
    require(effects.size() == 1 && effects[0].kind == rich4::EffectKind::dice_roll, "dice effect missing");
    require(effects[0].dice == game.dice() && effects[0].player == 0, "dice effect values differ from roll");
    require(effects[0].target.x == game.world().players[0].state.x, "dice effect anchor is not the actor");
    require(game.drain_effects().empty(), "effects drain once");
    // Draining (or never draining) effects must not change rules or randomness.
    until(game, [&] {
        return game.phase() == rich4::GamePhase::awaiting_decision ||
               game.phase() == rich4::GamePhase::advancing_turn || game.phase() == rich4::GamePhase::finished;
    });
    first_landing(silent);
    require(game.random_state() == silent.random_state() &&
                game.world().players[0].state.node_id == silent.world().players[0].state.node_id,
            "effect queue changed the rule outcome");
}
void tool_and_card_effects() {
    const auto find = [](const std::vector<rich4::EffectEvent> &effects, rich4::EffectKind kind) {
        return std::find_if(effects.begin(), effects.end(), [kind](const auto &e) { return e.kind == kind; });
    };
    // Roadblock placement flies from the actor to the chosen road node.
    auto value = world();
    value.players[0].state.tool_amounts[1] = 1;
    rich4::GameSession roadblock(value, 5);
    const auto before = roadblock.random_state();
    require(roadblock.use_tool(2, 5), "roadblock rejected");
    auto effects = roadblock.drain_effects();
    const auto thrown = find(effects, rich4::EffectKind::object_throw);
    require(thrown != effects.end() && thrown->object_kind == 16 && thrown->node == 5 && thrown->from &&
                thrown->from->x == roadblock.world().players[0].state.x &&
                thrown->target.x == roadblock.world().nodes[5].x,
            "roadblock throw effect");
    require(roadblock.random_state() == before, "throw effect consumed randomness");
    // Missile: movie starts before the resulting hospital/damage presentation; rule mutation is synchronous.
    value = world();
    value.players[0].state.tool_amounts[6] = 1;
    rich4::GameSession missile(value, 5);
    require(missile.use_tool(7) && missile.pending_decision(), "missile target prompt");
    // Target options carry the subject the app names (players, lands); roads stay 道路, and the
    // prompt keeps no "目的" (items.cpp picks the destination cursor on it).
    for (const auto &target : missile.pending_decision()->options) {
        if (target.value >= 2000 && target.value < 6000)
            require(target.subject && target.subject->kind == rich4::CardTargetKind::property &&
                        target.subject->value == target.value,
                    "property target lacks its subject");
        else if (target.value > 0 && target.value < 2000)
            require(!target.subject && target.label == "道路", "road target label");
        else if (target.value & 0x8000)
            require(target.subject.has_value(), "actor/object target lacks its subject");
    }
    require(missile.pending_decision()->prompt == "選擇目標", "tool target prompt is Traditional");
    // The map target farthest from every player, so nobody is sent to the (absent) hospital.
    std::size_t option = missile.pending_decision()->options.size();
    int best = -1;
    for (std::size_t i = 0; i < missile.pending_decision()->options.size(); ++i)
        if (const auto &at = missile.pending_decision()->options[i].location) {
            int nearest = 1 << 30;
            for (const auto &p : missile.world().players)
                nearest = std::min(nearest, std::abs(at->x - p.state.x) + std::abs(at->y - p.state.y));
            if (nearest > best) {
                best = nearest;
                option = i;
            }
        }
    require(option < missile.pending_decision()->options.size(), "missile has no map target");
    const auto location = *missile.pending_decision()->options[option].location;
    require(missile.respond(option), "missile target rejected");
    effects = missile.drain_effects();
    const auto hit = find(effects, rich4::EffectKind::missile);
    require(hit != effects.end() && hit->target.x == location.x && hit->target.y == location.y,
            "missile effect target");
    // Demolish card on an owned building.
    value = world();
    value.players[0].state.cards[0] = 12;
    value.lands[1].x = static_cast<std::uint16_t>(value.players[0].state.x);
    value.lands[1].y = 160;
    value.lands[1].economy.owner_code = 2;
    value.lands[1].economy.level = 3;
    rich4::GameSession demolish(value, 5);
    const auto targets = demolish.card_targets(12);
    require(!targets.empty(), "demolish card has no target");
    require(demolish.use_card({12, targets[0], std::nullopt}), "demolish card rejected");
    effects = demolish.drain_effects();
    const auto picture = find(effects, rich4::EffectKind::demolish);
    require(picture != effects.end() && picture->property == targets[0].value && picture->target.y == 160,
            "demolish effect");
}
// Missile impact is visibly committed on film frame 9. The actor can already be
// hospitalised in the rule state, but the presentation must play the missile before
// its ambulance and hospital announcement. Check both blast variants.
void missile_presentation_order() {
    for (unsigned tool : {7U, 13U}) {
        auto value = world();
        value.players[0].state.tool_amounts[tool - 1] = 1;
        // The synthetic map has no hospital until one is assigned. It is far enough
        // from the target not to interfere with the struck player's initial location.
        value.nodes[8].flags = (value.nodes[8].flags & ~0xffU) | 5U;
        rich4::GameSession game(std::move(value), 8);
        require(game.use_tool(tool, 0x8002), "targeted missile should apply");
        const auto events = game.drain_events();
        const auto effects = game.drain_effects();
        const auto kind = tool == 7 ? rich4::EffectKind::missile : rich4::EffectKind::nuclear_missile;
        const auto movie = std::find_if(effects.begin(), effects.end(),
                                         [kind](const auto &event) { return event.kind == kind; });
        require(movie != effects.end(), "blast movie missing from presentation");
        const auto ambulance = std::find_if(effects.begin(), effects.end(), [](const auto &event) {
            return event.kind == rich4::EffectKind::ambulance;
        });
        require(ambulance != effects.end() && movie < ambulance &&
                    movie->after_events <= ambulance->after_events,
                "missile must precede the ambulance in the presentation queue");
        const auto hospital = std::find_if(events.begin(), events.end(), [](const auto &event) {
            return event.message.find("住院") != std::string::npos;
        });
        require(hospital != events.end() &&
                    movie->after_events <= std::size_t(hospital - events.begin()),
                "missile must precede hospital settlement announcement");
        require(game.world().players[1].status.days_in_hospital > 0,
                "missile must still hospitalize the struck player");
    }
}
// 土地公 attaches as an object. Standing on OWN LAND still presents the
// ordinary upgrade dialog; only unowned purchases are suppressed (the god
// later claims such land in its post-property hook). Object ID != kind is
// independently exercised to prevent accidental comparisons against IDs.
void land_god_own_upgrade() {
    for (const unsigned object_id : {12U, 18U}) {
        auto w = world();
        property_nodes(w);
        w.lands[1].economy.owner_code = 1;
        w.lands[1].economy.level = 2;
        auto &object = w.objects.at(object_id - 1);
        object.kind = 12; // 土地公
        object.node_id = w.players[0].state.node_id;
        object.attached_player = 1;
        w.players[0].status.god_info = static_cast<std::uint8_t>(object_id);
        rich4::GameSession game(std::move(w), 5);
        first_landing(game);
        require(game.pending_decision() && game.pending_decision()->kind == rich4::DecisionKind::upgrade_land,
                "land god must not suppress an own-land upgrade choice");
        require(game.respond(0) && game.world().lands[1].economy.level == 3,
                "land god must allow the ordinary paid upgrade on owned land");
    }
}
void property_and_rent() {
    auto value = world();
    property_nodes(value);
    value.header.price_index = 2;
    value.lands[1].economy.level = 2;
    rich4::GameSession buy(std::move(value), 0);
    first_landing(buy);
    require(buy.pending_decision()->amount == 3600, "purchase base and per-level fields reversed");
    require(buy.respond(0), "buy decision failed");
    require(buy.world().lands[1].economy.owner_code == 1 && buy.world().players[0].state.cash == 96400,
            "purchase did not update owner/cash");
    value = world();
    property_nodes(value);
    value.lands[1].economy.owner_code = 1;
    rich4::GameSession upgrade(std::move(value), 0);
    first_landing(upgrade);
    require(upgrade.pending_decision()->kind == rich4::DecisionKind::upgrade_land &&
                upgrade.pending_decision()->amount == 400,
            "upgrade price differs");
    require(upgrade.respond(0) && upgrade.world().lands[1].economy.level == 1,
            "upgrade did not advance one level");
    value = world();
    property_nodes(value);
    value.lands[1].economy.owner_code = 2;
    value.players[0].state.cash = 100;
    value.players[0].state.bank_balance = 1000;
    rich4::GameSession rent(std::move(value), 0);
    first_landing(rent);
    require(rent.world().players[0].state.cash == 0 && rent.world().players[0].state.bank_balance == 950,
            "rent must spend cash before bank");
    require(rent.world().players[1].state.bank_balance == 10150 &&
                rent.world().players[1].state.cash == 100000,
            "rent must credit owner's bank, not cash");
    require(rent.world().players[0].status.cumulative_spending == 150 &&
                rent.world().players[1].status.cumulative_income == 150,
            "rent accounting differs");
    require(rent.world().lands[1].removed_building == 150, "last requested toll was not preserved");
    {
        // The original toll box (presentation only) comes before the payer/owner speech.
        const auto events = rent.drain_events();
        const auto box = std::find_if(events.begin(), events.end(), [](const auto &event) {
            return event.announcement == rich4::Announcement::toll;
        });
        require(box != events.end() && box->property == 2001 && box->subject == 1 && box->player == 0 &&
                    box->amount == 150 && box->second_subject == -1,
                "toll announcement lacks land, owner or amount");
        const auto paid = std::find_if(events.begin(), events.end(), [](const auto &event) {
            return event.kind == rich4::GameEventKind::rent_paid;
        });
        require(paid != events.end() && box < paid, "toll announcement must precede the payment");
    }
    value = world();
    property_nodes(value);
    value.lands[1].economy.owner_code = 2;
    value.players[1].status.god_info = 15;
    rich4::GameSession immune(std::move(value), 0);
    first_landing(immune);
    require(immune.world().players[0].state.cash == 100000, "owner god immunity ignored");
}
void alliance_rent() {
    auto value = world(4, 2);
    property_nodes(value);
    value.lands[1].economy.owner_code = 2;
    value.lands[1].economy.toll_by_level.fill(1000);
    value.lands[2] = value.lands[1];
    value.lands[2].id = 2;
    value.lands[2].economy.owner_code = 3;
    value.lands[2].economy.toll_by_level.fill(2000);
    value.players[1].status.allied_player = 3;
    value.players[2].status.allied_player = 2;
    value.lands[1].temporary_state = 0x10;
    value.players[0].status.god_info = 5;
    rich4::GameSession game(value, 0);
    first_landing(game);
    require(game.world().players[0].state.cash == 94000 &&
                game.world().players[1].state.bank_balance == 13000 &&
                game.world().players[2].state.bank_balance == 13000,
            "ally toll must be added after temporary doubling and split before god adjustment");
    require(game.world().players[0].status.hostility[1] == 40 &&
                game.world().players[0].status.hostility[2] == 20,
            "ally hostility uses god-adjusted total minus original ally toll, not payment shares");
    require(game.world().lands[1].removed_building == 6000,
            "allied land must preserve total requested rent after both payments");
    value.players[0].status.god_info = 0;
    value.lands[1].temporary_state = 0;
    value.players[0].state.cards.fill(0);
    value.players[0].state.cards[0] = 20;
    rich4::GameSession free(value, 0);
    first_landing(free);
    require(free.pending_decision() && free.respond(0), "allied free toll choice absent");
    require(free.world().players[0].state.cash == 100000 &&
                free.world().players[0].status.hostility[1] == 10 &&
                free.world().players[0].status.hostility[2] == 20,
            "free card must not erase original pre-defense hostility");
}
void bank_and_failure() {
    auto value = world();
    for (auto &node : value.nodes)
        node.flags = (node.flags & ~0xffU) | 14;
    rich4::GameSession bank(std::move(value), 0);
    first_landing(bank);
    require(bank.pending_decision()->kind == rich4::DecisionKind::bank, "bank decision missing");
    require(!bank.respond(0, -10) && !bank.respond(0, 100001), "invalid bank amount accepted");
    require(bank.respond(0, 1234), "integer deposit rejected");
    require(bank.world().players[0].state.cash == 98766 &&
                bank.world().players[0].state.bank_balance == 11234,
            "deposit changed total funds");
    require(bank.respond(1, 234) && bank.world().players[0].state.cash == 99000, "withdrawal failed");
    require(bank.respond(bank.pending_decision()->options.size() - 1), "bank exit failed");
    value = world();
    property_nodes(value);
    value.lands[1].economy.owner_code = 2;
    value.players[0].state.cash = 40;
    value.players[0].state.bank_balance = 20;
    rich4::GameSession bankrupt(std::move(value), 0);
    first_landing(bankrupt);
    require(bankrupt.phase() == rich4::GamePhase::finished &&
                !bankrupt.world().players[0].state.controller_flags,
            "sole human bankruptcy must end game");
    require(bankrupt.world().players[1].state.bank_balance == 10060 &&
                bankrupt.world().players[0].status.cumulative_spending == 60,
            "bankruptcy must transfer actual liquid funds");
}
void thirty_days_and_timing() {
    rich4::GameSession small(world(), 7), large(world(), 7);
    auto run = [](rich4::GameSession &game, unsigned delta) {
        unsigned bound = 0;
        while (game.completed_days() < 30 && ++bound < 100000) {
            if (game.phase() == rich4::GamePhase::human_ready)
                require(game.roll_dice(), "autoplay roll failed");
            require(!game.pending_decision(), "plain roads unexpectedly need a decision");
            game.tick(delta);
        }
        require(game.completed_days() == 30 && game.can_save(), "thirty days did not return to human input");
    };
    run(small, 17);
    run(large, 100000);
    require(small.random_state() == large.random_state(), "animation timing changed RNG sequence");
    for (std::size_t i = 0; i < 4; ++i) {
        require(small.world().players[i].state.node_id == large.world().players[i].state.node_id,
                "animation timing changed final nodes");
        require(small.world().players[i].state.bank_balance == 11000, "month boundary bank interest differs");
    }
    require(small.world().header.date.day == 4 && small.world().header.date.month == 11,
            "date must advance once per complete round");
}
// Plays whole rounds until the date advances `days` times; returns every presentation effect.
std::vector<rich4::EffectEvent> play_days(rich4::GameSession &game, std::uint64_t days) {
    std::vector<rich4::EffectEvent> effects;
    for (unsigned bound = 0; game.completed_days() < days && bound < 200000; ++bound) {
        if (game.phase() == rich4::GamePhase::human_ready)
            require(game.roll_dice(), "autoplay roll failed");
        if (game.pending_decision())
            require(game.respond(game.pending_decision()->options.empty()
                                     ? 0
                                     : game.pending_decision()->options.size() - 1),
                    "decision answer rejected");
        game.tick(50);
        for (auto &effect : game.drain_effects())
            effects.push_back(std::move(effect));
        (void)game.drain_events();
    }
    require(game.completed_days() == days, "days did not advance");
    return effects;
}
const rich4::EffectEvent *find_effect(const std::vector<rich4::EffectEvent> &effects, rich4::EffectKind kind) {
    for (const auto &effect : effects)
        if (effect.kind == kind)
            return &effect;
    return nullptr;
}
// Original fcn_0042ba97 on the 15th: listed companies pay their whole balance to the active
// shareholders' deposits (binary32 share ratio, truncated product), then clear it; losses come out
// of the deposit, then cash. Presentation effects carry the table, the draw and the month report.
void monthly_settlement() {
    auto value = world();
    value.header.date = {14, 10, 2026};
    value.stocks[0].flags_4 = 1;
    value.commercials.at(1).funds = 1000;
    value.players[0].stocks[0].amount = 1;
    value.players[1].stocks[0].amount = 2;
    value.stocks[1].flags_4 = 1; // second listing of the same company: already paid out (funds 0)
    value.lottery_tickets[6] = 2; // player 1 holds number 7
    value.public_fund = 5000;
    rich4::GameSession game(std::move(value), 3);
    const auto effects = play_days(game, 1);
    const auto &players = game.world().players;
    require(players[0].state.bank_balance == 10333 && players[1].state.bank_balance == 10666 &&
                players[2].state.bank_balance == 10000,
            "dividend shares differ from trunc(funds * float(shares / total))");
    require(game.world().commercials[1].funds == 0, "paid-out company balance not cleared");
    const auto *table = find_effect(effects, rich4::EffectKind::dividend);
    require(table && table->monthly && table->monthly->dividends.size() == 2 &&
                table->monthly->dividends[0].earnings == 1000 && table->monthly->dividends[0].payout[0] == 333 &&
                table->monthly->dividends[0].payout[1] == 666 && table->monthly->dividends[1].earnings == 0 &&
                table->monthly->active == 0xf,
            "dividend table effect differs");
    const auto *draw = find_effect(effects, rich4::EffectKind::lottery_draw);
    require(draw && draw->monthly && draw->monthly->tickets[6] == 2 && draw->monthly->pool == 5000 &&
                draw->monthly->number >= 1 && draw->monthly->number <= 36,
            "lottery draw effect differs");
    require((draw->monthly->winner == 1) == (draw->monthly->number == 7), "lottery winner differs from the draw");
    require(table < draw, "the dividend table must show before the lottery draw");

    value = world();
    value.header.date = {14, 10, 2026};
    value.stocks[0].flags_4 = 1;
    value.commercials.at(1).funds = -30000;
    value.players[2].stocks[0].amount = 5;
    rich4::GameSession loss(std::move(value), 3);
    (void)play_days(loss, 1);
    require(loss.world().players[2].state.bank_balance == 0 && loss.world().players[2].state.cash <= 80000,
            "a dividend loss must empty the deposit before cash");

    value = world();
    value.header.date = {31, 10, 2026};
    value.players[1].state.bank_balance = 50000;
    value.players[2].state.loan = 1000;
    value.players[3].status.total_winter_sleep_days = 3;
    value.players[3].status.cumulative_spending = 9000;
    rich4::GameSession month(std::move(value), 3);
    const auto report_effects = play_days(month, 1);
    const auto *report = find_effect(report_effects, rich4::EffectKind::month_report);
    require(report && report->monthly, "month report effect missing");
    const auto &figures = *report->monthly;
    require(figures.deposit[1] == 50000 && figures.interest[1] == 5000 && figures.balance[1] == 55000 &&
                (figures.loans & 4) && !(figures.loans & 2) && figures.champion == 1,
            "month report figures differ");
    require(figures.unlucky_days[3] == 3 && figures.spending[3] == 9000 && figures.unlucky == -1,
            "a lone unlucky score has no runner-up, so no 悲情人物");
}
// Unlike deterministic test replays, interactive loads do not restore a .rng
// sidecar. Original save_load.asm reseeds from GetTickCount after loading DAT.
void same_save_interactive_reload_rng() {
    auto value = world();
    const auto saved = rich4::SaveFile::decode(value.export_save());
    rich4::LoadSeedSequence live_seeds;
    const auto seed_a = live_seeds.next(123456U);
    const auto seed_b = live_seeds.next(123456U); // second load inside same clock millisecond
    rich4::GameSession first(rich4::World::from_save(saved), seed_a);
    rich4::GameSession again(rich4::World::from_save(saved), seed_b);
    require(first.export_save().encode() == again.export_save().encode() &&
                first.random_state() != again.random_state(),
            "same DAT should preserve map and inventory but get fresh live RNG seeds");
    require(first.roll_dice() && again.roll_dice(), "both interactive reloads must permit dice");
    rich4::Random expected_a(seed_a), expected_b(seed_b);
    require(first.dice() == std::vector<std::uint8_t>{static_cast<std::uint8_t>(expected_a.next() % 6U + 1U)} &&
                again.dice() == std::vector<std::uint8_t>{static_cast<std::uint8_t>(expected_b.next() % 6U + 1U)} &&
                first.random_state() != again.random_state(),
            "interactive reloads must use original Watcom rand() from freshly seeded streams");
}
void save_reload_and_deferred_spawn() {
    auto value = world();
    for (auto &entry : value.players) {
        entry.state.x = entry.state.y = entry.state.node_id = entry.state.last_node_id = 0;
    }
    for (auto &node : value.nodes)
        node.flags &= ~0x0f00U;
    rich4::GameSession spawn(std::move(value), 11);
    rich4::Random expected(11);
    (void)expected.next();
    (void)expected.next();
    require(spawn.world().players[0].state.node_id != 0 && spawn.world().players[1].state.node_id == 0,
            "only the player's first turn should spawn it");
    require(spawn.random_state() == expected.state(), "spawn must consume node and neighbor random calls");
    auto saved = spawn.export_save();
    auto reloaded = rich4::World::from_save(saved);
    require(reloaded.export_save() == saved.encode(), "native settled save lost original fields");
    rich4::GameSession resume(std::move(reloaded), spawn.random_state());
    require(resume.world().players[0].state.node_id == spawn.world().players[0].state.node_id &&
                resume.random_state() == spawn.random_state(),
            "reload must not respawn the current player");
    require(spawn.roll_dice() && resume.roll_dice(), "reloaded human cannot roll");
    until(spawn, [&] { return spawn.completed_days() == 1; });
    until(resume, [&] { return resume.completed_days() == 1; });
    require(spawn.export_save().encode() == resume.export_save().encode(),
            "explicit RNG seed plus settled save must continue identically");
}
void facilities_events_and_rescue() {
    auto value = world();
    for (auto &node : value.nodes) {
        node.flags &= ~0xffU;
        node.type_and_index = 4001;
    }
    value.facilities[1].land_price = 4000;
    value.facilities[1].toll_by_level = {400, 800, 1600, 3200, 6400, 12800};
    rich4::GameSession purchase(value, 0);
    first_landing(purchase);
    require(purchase.pending_decision()->kind == rich4::DecisionKind::buy_facility &&
                purchase.pending_decision()->amount == 4000,
            "unowned facility price/decision differs");
    require(purchase.respond(0) && purchase.world().facilities[1].owner_code == 1 &&
                purchase.world().players[0].state.cash == 96000,
            "facility purchase failed");
    value.facilities[1].owner_code = 1;
    rich4::GameSession construct(value, 0);
    first_landing(construct);
    require(construct.pending_decision()->kind == rich4::DecisionKind::construct_facility,
            "facility construction choice missing");
    require(construct.pending_decision()->options.size() == 5 && construct.pending_decision()->options[0].value == 0,
            "the build choice must offer 公園 first, like fcn_00440aac");
    require(construct.respond(2) && construct.world().facilities[1].facility_type == 2 &&
                construct.world().facilities[1].level == 1,
            "selected department-store construction failed");
    rich4::GameSession park(value, 0);
    first_landing(park);
    require(park.respond(0) && park.world().facilities[1].facility_type == 0 && park.world().facilities[1].level == 1 &&
                park.world().facilities[1].owner_code == 1,
            "a human 公園 build (slot 0) must construct a level-1 park, not give up");
    // Regression: building a level-1 research institute MUST start the research
    // selection, not silently end the turn. The result is persisted in save bytes.
    rich4::GameSession laboratory(value, 0);
    first_landing(laboratory);
    require(laboratory.respond(4) && laboratory.pending_decision() &&
                laboratory.pending_decision()->kind == rich4::DecisionKind::research_tool,
            "research institute construction did not open the research picker");
    require(laboratory.pending_decision()->options.size() == 2 &&
                laboratory.pending_decision()->options[0].value == 9,
            "level-1 research must only offer the machine worker and postpone");
    require(laboratory.respond(0) && laboratory.world().facilities[1].flags_30 == 5 &&
                laboratory.world().facilities[1].research_product == 0,
            "selected research must start a five-day production cycle");
    const auto saved_lab = rich4::World::from_save(rich4::SaveFile::decode(laboratory.world().export_save()));
    require(saved_lab.facilities[1].flags_30 == 5 &&
                saved_lab.facilities[1].research_product == 0,
            "research progress lost during DAT save/reload");

    // Regression: level 5 can choose every level's product even without upgrade funds.
    auto max_lab = value;
    max_lab.facilities[1].facility_type = 4;
    max_lab.facilities[1].level = 5;
    max_lab.players[0].state.cash = 0;
    rich4::GameSession all_products(max_lab, 0);
    first_landing(all_products);
    require(all_products.pending_decision() &&
                all_products.pending_decision()->kind == rich4::DecisionKind::research_tool &&
                all_products.pending_decision()->options.size() == 6 &&
                all_products.pending_decision()->options[4].value == 13,
            "max-level lab did not offer five research items");
    require(all_products.respond(4) && all_products.world().facilities[1].flags_30 == 5 &&
                all_products.world().facilities[1].research_product == 4,
            "level-5 nuclear missile research selection failed");

    // Research proceeds on calendar days, not taps or UI frame count; the completed
    // tool is deposited in the owner's inventory after the fifth game day.
    const auto before_tools = laboratory.world().players[0].state.tool_amounts[8];
    for (unsigned ticks = 0; ticks < 150000 && laboratory.world().facilities[1].flags_30; ++ticks) {
        if (laboratory.phase() == rich4::GamePhase::human_ready)
            require(laboratory.roll_dice(), "research test could not advance the human turn");
        if (laboratory.pending_decision()) {
            const auto &pending = *laboratory.pending_decision();
            // Decline incidental upgrades and wait before starting another project.
            const auto choice = pending.options.empty() ? 0U : static_cast<unsigned>(pending.options.size() - 1);
            if (!pending.options.empty())
                require(laboratory.respond(choice), "research test failed to answer a decision");
        }
        laboratory.tick(160);
    }
    require(laboratory.world().facilities[1].flags_30 == 0 &&
                laboratory.world().players[0].state.tool_amounts[8] == before_tools + 1,
            "completed 5-day research failed to award machine worker");

    value.facilities[1].owner_code = 2;
    value.facilities[1].facility_type = 2;
    value.facilities[1].level = 1;
    rich4::GameSession shopping(value, 0);
    first_landing(shopping);
    require(shopping.pending_decision()->kind == rich4::DecisionKind::roulette,
            "department roulette missing");
    const auto prior = shopping.random_state();
    require(shopping.pending_decision()->spinner && shopping.pending_decision()->spinner->type == 2 &&
                shopping.pending_decision()->spinner->subject == 1,
            "mall wheel state missing");
    shopping.tick(350);
    const auto stopped_at = shopping.pending_decision()->spinner->cursor;
    require(shopping.respond(0), "roulette stop rejected");
    const auto wheel_effects = shopping.drain_effects();
    require(std::any_of(wheel_effects.begin(), wheel_effects.end(),
                        [&](const auto &effect) {
                            return effect.kind == rich4::EffectKind::roulette && effect.spinner &&
                                   effect.spinner->phase == 2 && effect.spinner->cursor == stopped_at &&
                                   effect.spinner->result > 0;
                        }),
            "wheel slow-down effect must follow STOP");
    require(shopping.world().players[0].state.cash < 100000 &&
                shopping.world().players[1].state.bank_balance > 10000,
            "roulette did not charge the selected original multiplier");
    require(prior == shopping.random_state(), "low-fee roulette animation must not consume random calls");

    value = world();
    for (auto &node : value.nodes)
        node.flags = (node.flags & ~0xffU) | 3;
    value.fortune_deck.fill(14);
    value.fortune_cursor = 0;
    rich4::GameSession expense(value, 0);
    first_landing(expense);
    require(expense.world().players[0].state.cash == 97000 && expense.world().fortune_cursor == 1,
            "fortune deck expense was not applied exactly once");
    value.fortune_deck.fill(5);
    value.players[1].state.cards[0] = 3;
    rich4::GameSession card(value, 0);
    first_landing(card);
    require(card.pending_decision()->kind == rich4::DecisionKind::choose_card &&
                card.world().fortune_cursor == 0,
            "nested card choice should suspend without committing the event");
    require(card.respond(0) && card.world().players[0].state.cards[0] == 3 &&
                card.world().players[1].state.cards[0] == 0 && card.world().fortune_cursor == 1,
            "choice replay duplicated or lost card transfer");

    value = world();
    for (auto &node : value.nodes)
        node.flags = (node.flags & ~0xffU) | 4;
    value.prison_roster.fill(0);
    value.prison_roster[1] = 1;
    value.players[1].status.days_in_prison = 5;
    value.players[0].state.points = 50;
    rich4::GameSession rescue(value, 0);
    first_landing(rescue);
    require(rescue.pending_decision()->kind == rich4::DecisionKind::rescue && rescue.respond(0),
            "prison rescue input failed");
    require(rescue.world().players[0].state.points == 20 &&
                rescue.world().players[1].status.days_in_prison == 0x80 &&
                rescue.world().prison_roster[1] == 0,
            "prison rescue must spend points and defer original release");
}
// Teleporting the user must settle the destination as the end of the die move, not
// leave GO active. Other targets must not consume the user's dice/landing.
void teleport_landing_rules() {
    auto value = world();
    value.players[0].state.tool_amounts[10] = 1;
    value.lands[1].economy.owner_code = 1;
    value.lands[1].economy.level = 1;
    value.nodes[5].flags &= ~0xffU;
    value.nodes[5].type_and_index = 2001;
    rich4::GameSession upgrade(value, 19);
    const auto cash = upgrade.world().players[0].state.cash;
    require(upgrade.use_tool(11, 0x8001, 5) &&
                upgrade.pending_decision() &&
                upgrade.pending_decision()->kind == rich4::DecisionKind::upgrade_land &&
                !upgrade.roll_dice(), "self teleport to own house must offer an upgrade, not GO");
    require(upgrade.respond(0) &&
                upgrade.world().lands[1].economy.level == 2 &&
                upgrade.world().players[0].state.cash == cash - 400 &&
                upgrade.phase() == rich4::GamePhase::advancing_turn,
            "teleported house upgrade must charge cash and finish the turn");

    value = world();
    value.players[0].state.tool_amounts[10] = 1;
    value.players[0].state.points = 1000;
    value.nodes[5].flags = (value.nodes[5].flags & ~0xffU) | 15U;
    rich4::GameSession shop(value, 17);
    require(shop.use_tool(11, 0x8001, 5) && shop.pending_decision() &&
                shop.pending_decision()->kind == rich4::DecisionKind::shop && !shop.roll_dice(),
            "self teleport to shop must open normal card/tool trading");
    require(shop.respond(0) && shop.world().players[0].state.points < 1000 &&
                shop.pending_decision() && shop.pending_decision()->kind == rich4::DecisionKind::shop,
            "teleported shop should allow an item purchase");
    const auto exit = shop.pending_decision()->options.size() - 1;
    require(shop.respond(exit) && shop.phase() == rich4::GamePhase::advancing_turn,
            "leaving a teleported shop must finish the turn");

    value = world();
    value.players[0].state.tool_amounts[10] = 1;
    rich4::GameSession other(value, 20);
    require(other.use_tool(11, 0x8002, 5) &&
                other.world().players[1].state.node_id == 5 &&
                other.phase() == rich4::GamePhase::human_ready && other.roll_dice(),
            "teleporting another player must leave the user's roll available");

    value = world();
    value.players[0].state.tool_amounts[10] = 1;
    value.players[0].state.cards[0] = 1;
    value.players[0].state.tool_amounts[9] = 1;
    value.objects[0].kind = 1;
    value.objects[0].node_id = 1;
    value.objects[0].attached_player = 1;
    rich4::GameSession carrier(value, 21);
    require(carrier.use_tool(11, 0x8100, 5) &&
                carrier.world().players[0].state.node_id == 5 &&
                carrier.phase() == rich4::GamePhase::advancing_turn,
            "teleporting an attached object must also settle the actual carrier");
}

void service_and_auction_choices() {
    auto value = world();
    for (auto &node : value.nodes)
        node.flags = (node.flags & ~0xffU) | 15;
    value.players[0].state.points = 1000;
    rich4::GameSession shop(value, 7);
    first_landing(shop);
    require(shop.pending_decision()->kind == rich4::DecisionKind::shop, "shop did not expose a finite shelf");
    require(shop.respond(0) && shop.world().players[0].state.points < 1000 &&
                shop.pending_decision()->kind == rich4::DecisionKind::shop,
            "shop purchase did not preserve its visit");
    const auto leave = shop.pending_decision()->options.size() - 1;
    require(shop.respond(leave) && shop.phase() == rich4::GamePhase::advancing_turn,
            "leaving shop lost turn progression");
    value = world();
    for (auto &node : value.nodes)
        node.flags = (node.flags & ~0xffU) | 16;
    rich4::GameSession magic(value, 7);
    first_landing(magic);
    require(magic.pending_decision()->kind == rich4::DecisionKind::magic_category && magic.respond(0),
            "magic target category missing");
    require(magic.pending_decision()->kind == rich4::DecisionKind::magic_effect && magic.respond(4),
            "magic deposit action missing");
    require(magic.world().players[0].state.cash == 0 && magic.world().players[0].state.bank_balance == 110000,
            "magic action was not applied to the selected category");
    value = world();
    for (auto &node : value.nodes)
        node.flags = (node.flags & ~0xffU) | 2;
    value.news_deck.fill(7);
    value.news_cursor = 0;
    rich4::GameSession auction(value, 7);
    first_landing(auction);
    for (unsigned n = 0; n < 100 && auction.pending_decision(); ++n) {
        require(auction.pending_decision()->kind == rich4::DecisionKind::auction,
                "news auction lost nested decision type");
        const auto &options = auction.pending_decision()->options;
        const auto withdraw = std::find_if(options.begin(), options.end(),
                                           [](const auto &option) { return option.value == 6; });
        require(withdraw != options.end(), "auction permanent withdrawal missing");
        require(auction.respond(static_cast<std::size_t>(withdraw - options.begin())),
                "auction withdrawal rejected");
    }
    require(!auction.pending_decision() && auction.world().news_cursor == 1 &&
                auction.world().players[0].state.cash == 100000,
            "nested auction replay duplicated economic state or failed to complete");
}
// Presentation emitted before a NeedChoice (god entrance movie, bad-god speech, arrival caption) must
// be drained BEFORE the spinner decision is answered, exactly once, and the rule outcome (world, RNG,
// full event/effect stream) must be identical to the pre-fix core. Baselines were recorded from the
// core before the presentation change (commit 3010f94).
std::uint64_t fnv(std::uint64_t hash, std::string_view text) {
    for (const unsigned char c : text)
        hash = (hash ^ c) * 1099511628211ULL;
    return hash;
}
void god_spinner_presentation_order() {
    for (const std::uint8_t kind : {std::uint8_t{1}, std::uint8_t{5}}) {
        auto value = world();
        value.objects[0].kind = kind;
        value.objects[0].node_id = 2;
        value.nodes[2].flags |= 1U << 16;
        rich4::GameSession god(value, 0);
        first_landing(god);
        require(god.pending_decision() && god.pending_decision()->kind == rich4::DecisionKind::money_spinner,
                "god spinner decision missing");
        const auto before_events = god.drain_events();
        const auto before_effects = god.drain_effects();
        god.tick(330);
        require(god.respond(0), "god spinner Stop failed");
        for (unsigned i = 0; i < 1000 && god.pending_decision(); ++i)
            god.tick(30);
        require(!god.pending_decision(), "god spinner did not finish");
        const auto after_events = god.drain_events();
        const auto after_effects = god.drain_effects();
        std::uint64_t hash = 1469598103934665603ULL;
        std::size_t appear = 0, arrival = 0;
        for (const auto *list : {&before_events, &after_events})
            for (const auto &event : *list) {
                hash = fnv(hash, std::to_string(int(event.kind)) + ':' + std::to_string(event.player) + ':' +
                                     std::to_string(event.amount) + ':' + event.message + ':' +
                                     std::to_string(event.voice_category.value_or(255)) + ':' +
                                     std::to_string(int(event.announcement)) + ';');
                arrival += event.announcement == rich4::Announcement::god_arrival;
            }
        for (const auto *list : {&before_effects, &after_effects})
            for (const auto &effect : *list) {
                if (effect.kind == rich4::EffectKind::pause) // later timed holds, not in the baseline
                    continue;
                hash = fnv(hash, std::to_string(int(effect.kind)) + ':' + std::to_string(effect.object_kind) + ';');
                appear += effect.kind == rich4::EffectKind::god_appear;
            }
        hash = fnv(hash, std::to_string(god.random_state()));
        for (const auto &player : god.world().players)
            hash = fnv(hash, std::to_string(player.state.cash) + ',' + std::to_string(player.state.bank_balance));
        require(hash == (kind == 1 ? 11138929515763228672ULL : 5082774162140978962ULL),
                "god spinner outcome or presentation stream differs from the pre-fix core");
        require(appear == 1 && arrival == 1, "god entrance movie/caption must be delivered exactly once");
        const auto movie = std::find_if(before_effects.begin(), before_effects.end(), [](const auto &effect) {
            return effect.kind == rich4::EffectKind::god_appear;
        });
        require(movie != before_effects.end() && movie->object_kind == kind,
                "god entrance movie must be drained before the spinner decision is answered");
        const auto caption = std::find_if(before_events.begin(), before_events.end(), [](const auto &event) {
            return event.announcement == rich4::Announcement::god_arrival;
        });
        require(caption != before_events.end() &&
                    movie->after_events <= std::size_t(caption - before_events.begin()),
                "arrival caption must follow the entrance movie, before the spinner");
        if (kind == 5) { // rich4_gods.asm fcn_0040ef1b: player_say(22) BEFORE the FLC and caption
            const auto speech = std::find_if(before_events.begin(), before_events.end(), [](const auto &event) {
                return event.voice_category == 22;
            });
            require(speech != before_events.end() &&
                        std::size_t(speech - before_events.begin()) < movie->after_events,
                    "bad-god speech must precede the entrance movie");
        }
    }
    // The decision mirrors the slot machine's reels; the AI spin is a presentation effect.
    auto value = world();
    value.objects[0].kind = 2;
    value.objects[0].node_id = 2;
    value.nodes[2].flags |= 1U << 16;
    rich4::GameSession human(value, 0);
    first_landing(human);
    require(human.pending_decision()->spinner && human.pending_decision()->spinner->type == 2 &&
                human.pending_decision()->spinner->phase == 0,
            "money spinner decision must carry the slot-machine state");
    human.tick(300);
    require(human.pending_decision()->spinner->phase == 1 && human.pending_decision()->spinner->digits[3] < 20,
            "money spinner reels not mirrored");
    // A sleep-walking human (like a computer) gets the self-stopping machine as an effect whose
    // replay from `seed` reproduces the amount the rule applied.
    value.players[0].status.days_sleep_walking = 3;
    rich4::GameSession walker(value, 0);
    (void)walker.roll_dice();
    for (unsigned i = 0; i < 20000 && walker.world().players[0].status.god_info == 0; ++i)
        walker.tick(1);
    const auto effects = walker.drain_effects();
    const auto spin = std::find_if(effects.begin(), effects.end(), [](const auto &effect) {
        return effect.kind == rich4::EffectKind::money_spinner;
    });
    require(spin != effects.end() && spin->spinner && spin->spinner->type == 2, "auto money spinner effect missing");
    rich4::Random replay(0);
    replay.seed(spin->spinner->seed);
    require(rich4::gods::auto_money(2, replay) == spin->spinner->result,
            "money spinner effect must replay to the applied amount");
}
void native_actions_and_money_spinner() {
    auto value = world();
    value.players[0].state.tool_amounts[0] = 1;
    value.objects[16].kind = 16;
    value.objects[16].node_id = 2;
    value.nodes[2].flags |= 17U << 16;
    rich4::GameSession robot(value, 13);
    rich4::Random expected(13);
    for (unsigned i = 0; i < 9; ++i)
        (void)expected.next();
    require(robot.use_tool(1), "robot tool rejected");
    require(robot.phase() == rich4::GamePhase::human_ready && robot.world().players[0].state.node_id == 1,
            "robot must return control without moving its owner");
    require(robot.random_state() == expected.state() && robot.world().special_actors[4].node_id == 2 &&
                robot.world().special_actors[4].inactive_state == 3 && robot.world().objects[16].node_id == 0,
            "robot must walk nine original routes and sweep encountered objects");
    value = world();
    value.players[0].state.tool_amounts[7] = 1;
    rich4::GameSession remote(value, 99);
    require(remote.use_tool(8), "remote dice selection failed");
    require(remote.pending_decision() && remote.respond(3), "remote dice choice failed");
    until(remote, [&] { return remote.phase() == rich4::GamePhase::advancing_turn; });
    expected.seed(99);
    for (unsigned i = 0; i < 4; ++i)
        (void)expected.next();
    require(remote.world().players[0].state.node_id == 5 && remote.random_state() == expected.state(),
            "forced die must move requested distance without drawing a face");
    value = world();
    value.players[0].state.tool_amounts[9] = 1;
    rich4::GameSession machine(value, 17);
    require(!machine.use_tool(10), "empty time history accepted");
    require(machine.roll_dice(), "history roll failed");
    until(machine,
          [&] { return machine.completed_days() == 1 && machine.phase() == rich4::GamePhase::human_ready; });
    const auto before_random = machine.random_state();
    machine.set_camera_rotation(3);
    require(machine.use_tool(10), "recorded time history unavailable");
    require(machine.world().players[0].state.node_id == 1 && machine.world().header.date.day == 5 &&
                machine.world().players[0].state.tool_amounts[9] == 0 &&
                machine.random_state() == before_random && machine.world().camera_rotation == 3,
            "time machine restored excluded RNG/camera or failed to consume tool after restore");
    value = world();
    value.players[0].state.tool_amounts[9] = 1;
    value.players[0].state.tool_amounts[10] = 1;
    rich4::GameSession teleport(value, 5);
    require(teleport.use_tool(11, 0x8001, 5) && teleport.world().players[0].state.node_id == 5 &&
                teleport.phase() == rich4::GamePhase::advancing_turn && !teleport.roll_dice(),
            "self teleport must end the movement opportunity");
    until(teleport, [&] {
        return teleport.completed_days() >= 1 && teleport.phase() == rich4::GamePhase::human_ready;
    });
    require(teleport.use_tool(10) && teleport.world().players[0].state.node_id == 1 &&
                teleport.world().players[0].state.tool_amounts[10] == 1 &&
                teleport.world().players[0].state.tool_amounts[9] == 0,
            "self teleport must record history before movement and consumption even after loading a save");
    value = world();
    value.objects[0].kind = 1;
    value.objects[0].node_id = 2;
    value.nodes[2].flags |= 1U << 16;
    rich4::GameSession god(value, 0);
    first_landing(god);
    require(god.pending_decision()->kind == rich4::DecisionKind::money_spinner,
            "god must present original decimal spinner");
    god.tick(330);
    require(god.respond(0), "god spinner Stop failed");
    for (unsigned i = 0; i < 1000 && god.pending_decision(); ++i)
        god.tick(30);
    require(!god.pending_decision() && god.world().players[0].status.god_info == 1 &&
                god.world().objects[0].attached_player == 1 && !(god.world().nodes[2].flags & 0xff0000U),
            "god choice rollback lost attachment or did not finish");
    for (const auto &event : god.drain_events())
        require(event.kind != rich4::GameEventKind::unsupported_event, "god emitted unsupported rule");
}
void loan_and_free_toll() {
    auto value = world();
    for (auto &node : value.nodes)
        node.flags = (node.flags & ~255U) | 14;
    rich4::GameSession bank(value, 0);
    first_landing(bank);
    auto option = [&](int action) {
        const auto &list = bank.pending_decision()->options;
        const auto it =
            std::find_if(list.begin(), list.end(), [&](const auto &item) { return item.value == action; });
        require(it != list.end(), "bank action absent");
        return static_cast<std::size_t>(it - list.begin());
    };
    require(bank.respond(option(3), 1234), "loan integer amount rejected");
    const auto due = rich4::calendar::pack(rich4::holidays::loan_due_date({2026, 10, 5}, 0, 0));
    require(bank.world().players[0].state.loan == 1234 && bank.world().players[0].status.loan_due_date == due,
            "loan did not preserve original holiday-adjusted due date");
    require(bank.respond(option(4), 1234) && !bank.world().players[0].status.loan_due_date &&
                bank.world().players[0].state.bank_balance == 10000,
            "repay must use bank first and clear due when fully repaid");
    value = world();
    property_nodes(value);
    value.lands[1].economy.owner_code = 2;
    value.lands[1].economy.toll_by_level.fill(3000);
    value.players[0].state.cards.fill(0);
    value.players[0].state.cards[0] = 20;
    rich4::GameSession free(value, 0);
    first_landing(free);
    require(free.pending_decision() && free.respond(0), "free toll choice absent");
    require(free.world().players[0].state.cash == 100000 && free.world().players[0].state.cards[0] == 0 &&
                free.world().lands[1].removed_building == 0,
            "free card replay charged money or failed to consume exactly once");
    const auto free_events = free.drain_events();
    require(std::count_if(free_events.begin(), free_events.end(),
                          [](const auto &event) { return event.card == 20; }) == 1,
            "free card use must report its card id once for the card-art popup");
    value = world();
    value.players[0].state.cards[0] = 1;
    rich4::GameSession equal_wealth(value, 0);
    require(equal_wealth.use_card({1, {}, {}}), "equal-wealth card rejected");
    const auto used_events = equal_wealth.drain_events();
    require(std::any_of(used_events.begin(), used_events.end(),
                        [](const auto &event) { return event.card == 1 && event.player == 0; }),
            "human card use must report its card id");
}
void special_financing() {
    auto value = world();
    for (auto &node : value.nodes) {
        node.flags = (node.flags & ~255U) | 14;
        node.type_and_index = 6001;
    }
    value.commercials[1].owner_code = 1;
    value.commercials[1].commerce_type = 7;
    rich4::GameSession bank(value, 0);
    first_landing(bank);
    auto select = [&](int action) {
        const auto &choices = bank.pending_decision()->options;
        const auto it = std::find_if(choices.begin(), choices.end(),
                                     [&](const auto &item) { return item.value == action; });
        require(it != choices.end(), "finance bank action absent");
        return static_cast<std::size_t>(it - choices.begin());
    };
    require(!bank.respond(select(5), 30001) && bank.respond(select(5), 5000),
            "finance borrowing must respect other bank balances");
    require(bank.world().players[0].state.special_finance == 5000 &&
                bank.world().players[0].state.bank_balance == 15000 &&
                bank.world().players[0].state.loan == 0,
            "special financing must not create an ordinary dated loan");
    require(bank.respond(select(6), 1000) && bank.world().players[0].state.special_finance == 4000 &&
                bank.world().players[0].state.bank_balance == 14000,
            "finance repayment differs");
    value.commercials[1].owner_code = 2;
    value.players[1].state.special_finance = 25000;
    rich4::GameSession withdrawal(value, 0);
    first_landing(withdrawal);
    require(withdrawal.respond(1, 10000), "ATM withdrawal rejected");
    require(withdrawal.world().players[1].state.special_finance == 20000 &&
                withdrawal.world().players[1].state.bank_balance == 5000,
            "withdrawal must collect bank owner's uncovered financing");
    value = world();
    value.players[0].state.special_finance = 1000;
    rich4::GameSession market(value, 0);
    require(market.close_stock_market(), "market close rejected");
    require(!market.world().players[0].state.special_finance &&
                market.world().players[0].state.bank_balance == 9000 &&
                market.world().players[0].status.cumulative_spending == 0,
            "lost financing ownership must collect debt without ordinary expense bookkeeping");
}
void computer_atm_financing() {
    auto value = world();
    value.players[0].state.controller_flags = 2;
    value.players[1].state.controller_flags = 1;
    value.players[0].status.loan_ratio = 0;
    value.players[0].status.init_cash_ratio = 50;
    for (auto &node : value.nodes)
        node.flags = (node.flags & ~255U) | 14;
    value.commercials[1].commerce_type = 7;
    value.commercials[1].owner_code = 2;
    value.players[1].state.special_finance = 60000;
    rich4::GameSession unchanged(value, 7);
    until(unchanged, [&] { return unchanged.phase() == rich4::GamePhase::advancing_turn; });
    require(unchanged.world().players[0].state.cash == 100000 &&
                unchanged.world().players[1].state.special_finance == 60000,
            "computer ATM must not collect financing when its ratio requires no transfer");
    value.players[0].state.cash = 0;
    value.players[0].state.bank_balance = 100000;
    rich4::GameSession rebalanced(value, 7);
    until(rebalanced, [&] { return rebalanced.phase() == rich4::GamePhase::advancing_turn; });
    require(rebalanced.world().players[0].state.cash == 75000 &&
                rebalanced.world().players[0].state.bank_balance == 25000 &&
                rebalanced.world().players[1].state.special_finance == 45000,
            "computer withdrawal must use the date-adjusted ratio then recover uncovered bank financing");
}
void flight_and_confinement_objects() {
    auto value = world();
    property_nodes(value);
    value.players[0].state.traffic_method = 0x1f;
    value.lands[1].economy.owner_code = 2;
    value.lands[1].economy.level = 3;
    value.lands[1].expired_date = 0x7eb0101;
    rich4::GameSession flight(value, 0);
    first_landing(flight);
    require(flight.world().lands[1].economy.level == 0 && flight.world().lands[1].economy.owner_code == 2 &&
                flight.world().lands[1].expired_date == 0x7eb0101,
            "flight bombardment must demolish buildings but preserve ownership and lease");
    require(flight.world().players[0].state.cash == 98800 &&
                flight.world().players[1].status.hostility[0] == 30,
            "flight must pay the original toll before bombing and anger the owner by 30 times index");
    value = world();
    property_nodes(value);
    value.players[0].state.traffic_method = 0x1f;
    value.players[0].status.days_sleep_walking = 2;
    value.lands[1].economy.owner_code = 2;
    value.lands[1].economy.level = 3;
    rich4::GameSession sleeping(value, 0);
    until(sleeping, [&] { return sleeping.phase() == rich4::GamePhase::advancing_turn; });
    require(sleeping.world().lands[1].economy.level == 3, "sleepwalking must suppress plane bombing");
    value = world();
    value.header.current_player = 3;
    value.nodes[1].flags &= ~0x100U;
    value.players[0].state.node_id = 2;
    value.players[0].state.last_node_id = 0;
    value.players[0].state.x = value.players[0].state.y = 500;
    value.players[0].status.days_in_hospital = 0x80;
    value.players[0].status.saved_direction = 15;
    value.players[0].status.god_info = 9;
    value.objects[8].kind = 9;
    value.objects[8].node_id = 2;
    value.objects[8].attached_player = 1;
    value.objects[8].remaining_days = 10;
    value.nodes[2].type_and_index = 2001;
    value.lands[1].economy.owner_code = 2;
    value.lands[1].economy.level = 1;
    rich4::GameSession release(value, 9);
    until(release,
          [&] { return release.completed_days() == 1 && release.phase() == rich4::GamePhase::human_ready; });
    require(!release.world().players[0].status.days_in_hospital &&
                release.world().players[0].state.node_id == 2 && release.world().lands[1].economy.level == 2,
            "returning from a building must reapply the attached god before ready input");
    value = world();
    for (auto &node : value.nodes)
        node.flags = (node.flags & ~255U) | 3;
    value.nodes[7].flags = (value.nodes[7].flags & ~255U) | 5;
    value.fortune_deck.fill(12);
    value.fortune_cursor = 0;
    value.players[0].status.god_info = 1;
    value.objects[0].kind = 1;
    value.objects[0].node_id = 1;
    value.objects[0].attached_player = 1;
    value.objects[0].remaining_days = 4;
    value.players[0].status.attached_bomb = 37;
    value.objects[36].kind = 18;
    value.objects[36].node_id = 1;
    value.objects[36].attached_player = 1;
    value.objects[36].remaining_days = 20;
    // No other player on the first step: retain the bomb until hospitalization.
    value.nodes[2].flags &= ~0x200U;
    value.players[1].state.node_id = 6;
    value.players[1].state.x = static_cast<std::uint16_t>(value.nodes[6].x);
    value.nodes[6].flags |= 0x200U;
    rich4::GameSession hospital(value, 0);
    first_landing(hospital);
    require(hospital.world().players[0].status.days_in_hospital == 3 &&
                hospital.world().players[0].state.node_id == 7 && hospital.world().objects[0].node_id == 7 &&
                hospital.world().objects[36].node_id == 7,
            "confinement must synchronize attached god and bomb node references");
}
void direct_trading() {
    auto value = world();
    value.players[0].state.cards[0] = 1;
    value.players[1].state.cards[0] = 2;
    require(rich4::trading::post(value, 1, {rich4::OfferKind::card, 2, 100, 0}).applied,
            "test seller could not post an owned card");
    rich4::GameSession game(value, 77);
    const auto random = game.random_state();
    require(game.open_trading() && game.post_offer({rich4::OfferKind::card, 1, 100, 0}),
            "human listing input rejected at ready phase");
    require(game.world().players[0].state.cards[0] == 1, "posting must not consume inventory");
    require(game.buy_offer(1, 0) && game.world().players[0].state.cash == 99900 &&
                game.world().players[1].state.bank_balance == 10100 &&
                game.world().players[1].state.cards[0] == 0,
            "direct sale must debit buyer cash and credit seller bank");
    require(game.cancel_offer(0) && game.close_trading() && game.phase() == rich4::GamePhase::human_ready &&
                game.random_state() == random,
            "direct trading should preserve the player's pending dice action and RNG");
    require(game.post_offer({rich4::OfferKind::card, 1, 100, 0}), "relisting failed");
    auto saved = game.export_save();
    require(rich4::World::from_save(saved).offers == game.world().offers, "native save lost a posted offer");
    require(game.roll_dice() && !game.cancel_offer(0) && !game.open_trading(),
            "in-flight turn accepted market mutations");
}
void lifecycle_and_finished_saves() {
    auto value = world();
    for (std::size_t i = 1; i < value.players.size(); ++i)
        value.players[i].state.controller_flags = 0;
    rich4::GameSession winner(value, 55);
    require(winner.phase() == rich4::GamePhase::finished && winner.winning_player() == 0 &&
                winner.random_state() == 55,
            "terminal save must restore winner without starting a turn");
    auto encoded = winner.export_save();
    rich4::GameSession restored(rich4::World::from_save(encoded), 55);
    require(restored.phase() == rich4::GamePhase::finished && restored.winning_player() == 0 &&
                restored.export_save().encode() == encoded.encode(),
            "finished save reload changed result or bytes");
    value = world();
    value.players[0].state.traffic_method = 2;
    rich4::GameSession dice(value, 1);
    require(dice.set_dice_count(3) && !dice.set_dice_count(4) && !dice.set_dice_count(0),
            "vehicle dice limits differ");
    require(dice.roll_dice() && !dice.set_dice_count(1) && dice.dice().size() == 3,
            "dice count may only change at settled human input");
    value = world();
    value.header.current_player = 3;
    value.players[0].state.loan = 1100;
    value.players[0].state.cash = 100;
    value.players[0].state.bank_balance = 1000;
    value.players[0].status.loan_due_date = rich4::calendar::pack({2026, 10, 6});
    rich4::GameSession due(value, 10);
    until(due, [&] { return due.completed_days() == 1 && due.phase() == rich4::GamePhase::human_ready; });
    require(due.world().players[0].state.cash == 0 && due.world().players[0].state.bank_balance == 0 &&
                due.world().players[0].state.loan == 0 && due.world().players[0].status.loan_due_date == 0 &&
                due.world().players[0].status.cumulative_spending == 0,
            "due loan must collect bank then cash without ordinary expense bookkeeping");
    value = world();
    value.nodes[7].flags = (value.nodes[7].flags & ~255U) | 5;
    value.landscapes[1].x = 500;
    value.landscapes[1].y = 500;
    value.players[0].status.attached_bomb = 37;
    value.objects[36].kind = 18;
    value.objects[36].node_id = 1;
    value.objects[36].attached_player = 1;
    value.objects[36].remaining_days = 1;
    rich4::GameSession bomb(value, 0);
    first_landing(bomb);
    require(bomb.world().players[0].status.days_in_hospital == 5 &&
                !bomb.world().players[0].status.attached_bomb && !bomb.world().objects[36].node_id &&
                bomb.steps_remaining() == 0,
            "bomb fuse must explode before normal landing and hospitalize five days");
    value = world();
    value.players[0].status.attached_bomb = 37;
    value.objects[36].kind = 18;
    value.objects[36].node_id = 1;
    value.objects[36].attached_player = 1;
    value.objects[36].remaining_days = 20;
    rich4::GameSession transfer(value, 0);
    first_landing(transfer);
    require(!transfer.world().players[0].status.attached_bomb &&
                transfer.world().players[1].status.attached_bomb == 37 &&
                transfer.world().objects[36].remaining_days == 19 &&
                transfer.world().objects[36].attached_player == 2,
            "bomb collision must decrement then transfer to first live unbombed player");
}
void explicit_unsupported_and_save_guard() {
    auto value = world();
    for (auto &node : value.nodes)
        node.flags = (node.flags & ~0xffU) | 255;
    rich4::GameSession game(std::move(value), 0);
    require(game.roll_dice(), "roll failed");
    bool rejected = false;
    try {
        (void)game.export_save();
    } catch (const std::logic_error &) {
        rejected = true;
    }
    require(rejected, "saving an in-flight move must not lose queued state");
    // Original core_actions:361 (`cmp ebx,0x10; ja loc_0041b3d0`) ends the turn silently on
    // node kinds above 16, so no dialog and no unsupported event.
    until(game, [&] {
        return game.phase() == rich4::GamePhase::advancing_turn || game.pending_decision().has_value();
    });
    require(!game.pending_decision(), "unknown node kind must end the turn without a dialog");
    for (const auto &event : game.drain_events())
        require(event.kind != rich4::GameEventKind::unsupported_event, "unknown node kind is not a rule gap");
}
void hospital_and_prison_buildings() {
    // Map nodes 8001/8002 (hospital/prison building tiles) fall into the original's
    // empty-property tail (core_actions:369 -> loc_0041b077) and end the turn: no dialog.
    for (const std::uint16_t code : {std::uint16_t(8001), std::uint16_t(8002)}) {
        auto value = world();
        for (auto &node : value.nodes) {
            node.flags &= ~0xffU;
            node.type_and_index = code;
        }
        rich4::GameSession game(std::move(value), 0);
        first_landing(game);
        require(!game.pending_decision() && game.phase() == rich4::GamePhase::advancing_turn,
                "hospital/prison building tile must not open a dialog");
        for (const auto &event : game.drain_events())
            require(event.kind != rich4::GameEventKind::unsupported_event,
                    "hospital/prison building tile reported as an unported rule");
        require(game.world().players[0].state.cash == 100000, "building tile must not charge the player");
    }
}
} // namespace
void interactive_mini_games() {
    auto value = world();
    for (auto &node : value.nodes)
        node.flags = (node.flags & ~0xffU) | 6; // every tile is 企鵝挖寶
    rich4::GameSession closed(value, 4);
    require(!closed.interactive_mini_games(), "closed mode is the default");
    first_landing(closed);
    const auto closed_points = closed.world().players[0].state.points;
    require(!closed.pending_decision() && closed_points >= 50 && closed_points < 70,
            "closed mode awards rand()%20+50 points");
    { // SG:4385-4425: the "得點券%d點" box (2000 ms) comes before the speech line.
        const auto events = closed.drain_events();
        const auto box = std::find_if(events.begin(), events.end(), [](const auto &event) {
            return event.announcement == rich4::Announcement::mini_game_points;
        });
        require(box != events.end() && box->amount == closed_points && box->player == 0,
                "closed mode shows the 得點券 box with the reward");
        require(std::any_of(box, events.end(), [](const auto &event) { return event.voice_category.has_value(); }),
                "closed mode speech follows the box");
    }

    rich4::GameSession game(value, 4);
    game.set_interactive_mini_games(true);
    first_landing(game);
    const auto &decision = game.pending_decision();
    require(decision && decision->kind == rich4::DecisionKind::mini_game && decision->mini_game,
            "a human gets the interactive mini-game");
    require(decision->mini_game->game == 0 && decision->mini_game->seed == game.random_state() &&
                decision->mini_game->character == game.world().players[0].state.character,
            "mini-game state carries the game, character and current random state");
    // The closed-mode session drew exactly two more numbers (reward + speech) than the open one.
    rich4::Random expected(game.random_state());
    (void)expected.next();
    (void)expected.next();
    require(expected.state() == closed.random_state(), "interactive landing must not draw the closed-mode numbers");
    require(!game.respond(0) && !game.can_save(), "a mini-game cannot be answered with respond or saved");
    require(!game.submit_mini_game(-1, 0), "negative scores are rejected");
    const auto seed = decision->mini_game->seed;
    const auto before = game.world().players[0].state.points;
    require(game.submit_mini_game(123, 28), "submit rejected");
    rich4::Random advanced(seed);
    for (int i = 0; i < 28; ++i)
        (void)advanced.next();
    require(game.random_state() == advanced.state(), "submit must advance the stream by the game's draws");
    require(game.world().players[0].state.points == before + 123, "score added to points");
    require(!game.pending_decision() && !game.submit_mini_game(1, 0), "decision ends once");
    const auto events = game.drain_events();
    require(std::any_of(events.begin(), events.end(),
                        [](const auto &event) {
                            return event.kind == rich4::GameEventKind::points_received && event.amount == 123 &&
                                   !event.voice_category;
                        }),
            "points event without speech");
    require(std::none_of(events.begin(), events.end(),
                         [](const auto &event) {
                             return event.announcement == rich4::Announcement::mini_game_points ||
                                    event.voice_category.has_value();
                         }),
            "the interactive path shows no 得點券 box and no speech (SG:4339-4355)");
    // 16-bit word add, as `add word [player+48], ax`.
    rich4::GameSession wrap(value, 4);
    wrap.set_interactive_mini_games(true);
    first_landing(wrap);
    const auto start = wrap.world().players[0].state.points;
    require(wrap.submit_mini_game(70000, 0), "large score accepted");
    require(wrap.world().players[0].state.points == static_cast<std::uint16_t>(start + 70000U), "word wrap");
    // Computers always use the closed mode.
    value.players[0].state.controller_flags = 2;
    value.players[1].state.controller_flags = 1; // keep one human so the game goes on
    rich4::GameSession computer(value, 4);
    computer.set_interactive_mini_games(true);
    until(computer, [&] { return computer.world().players[0].state.points != 0 || computer.pending_decision(); });
    require(!computer.pending_decision(), "computers never get the interactive game");
}

void ai_settings_autopilot() {
    // 託管AI: the core switches a human player to the computer and back via controller bit 0x04.
    rich4::GameSession game(world(), 3), untouched(world(), 3);
    const auto settings = game.ai_settings(0);
    require(settings && !settings->autopilot, "human AI settings missing");
    require(!game.ai_settings(1), "computer players have no AI settings");
    require(!game.set_ai_settings(1, {}), "AI settings accepted for a computer slot");
    auto invalid = *settings;
    invalid.personality = 3;
    require(!game.set_ai_settings(0, invalid), "out-of-range personality accepted");
    // Unchanged settings while idle: no rule, no randomness.
    require(game.set_ai_settings(0, *settings), "idle AI settings rejected");
    require(game.random_state() == untouched.random_state() &&
                game.phase() == rich4::GamePhase::human_ready &&
                game.world().export_save() == untouched.world().export_save(),
            "rewriting unchanged AI settings changed the game");
    // Idle hand-over: the waiting turn is played by the computer at once.
    auto autopilot = *settings;
    autopilot.autopilot = true;
    autopilot.personality = 2;
    autopilot.card_strategy = 3;
    require(game.set_ai_settings(0, autopilot), "autopilot rejected");
    require(game.world().players[0].state.controller_flags == 5, "託管 bit not set");
    require(game.world().players[0].status.rescue_strategy == 2 &&
                game.world().players[0].status.card_strategy == 3,
            "AI profile bytes not written");
    require(game.phase() != rich4::GamePhase::human_ready, "autopilot did not start a computer turn");
    for (unsigned i = 0;
         i < 200000 && game.completed_days() < 6 && game.phase() != rich4::GamePhase::finished; ++i) {
        while (game.pending_decision() && game.phase() == rich4::GamePhase::awaiting_decision)
            require(game.respond(0), "computer-time decision rejected");
        require(game.phase() != rich4::GamePhase::human_ready, "autopilot player waited for human input");
        game.tick(50);
    }
    require(game.completed_days() >= 6, "autopilot game did not advance");
    auto saved = game.world().export_save();
    require(rich4::World::from_save(rich4::SaveFile::decode(saved)).players[0].state.controller_flags == 5,
            "託管 bit lost in the save");
    // Mid-turn release: pending until the turn boundary, then the human gets the turn back.
    autopilot.autopilot = false;
    require(game.set_ai_settings(0, autopilot), "autopilot release rejected");
    require(game.ai_settings(0) && !game.ai_settings(0)->autopilot, "pending release not reported");
    for (unsigned i = 0; i < 200000 && game.phase() != rich4::GamePhase::human_ready &&
                         game.phase() != rich4::GamePhase::finished;
         ++i) {
        while (game.pending_decision() && game.phase() == rich4::GamePhase::awaiting_decision)
            require(game.respond(0), "computer-time decision rejected");
        game.tick(50);
    }
    require(game.phase() == rich4::GamePhase::human_ready && game.world().header.current_player == 0 &&
                game.world().players[0].state.controller_flags == 1,
            "human did not get the turn back");
    // Mid-turn hand-over waits for the boundary too.
    rich4::GameSession rolling(world(), 5);
    require(rolling.roll_dice(), "human roll rejected");
    auto on = *rolling.ai_settings(0);
    on.autopilot = true;
    require(rolling.set_ai_settings(0, on), "mid-turn autopilot rejected");
    require(rolling.world().players[0].state.controller_flags == 1, "mid-turn autopilot applied too early");
    until(rolling, [&] {
        if (rolling.pending_decision())
            (void)rolling.respond(0);
        return rolling.world().header.current_player != 0;
    });
    require(rolling.world().players[0].state.controller_flags == 5,
            "autopilot not applied at the turn boundary");
}
// The original movement-state countdowns (EffectKind::pause): 5 ticks between the last step and the
// landing handler, 8 more after a property landing before the next player. Presentation only.
void landing_pauses() {
    auto value = world();
    property_nodes(value);
    rich4::GameSession game(std::move(value), 1);
    first_landing(game);
    require(game.pending_decision() && game.pending_decision()->kind == rich4::DecisionKind::buy_land,
            "landing on unowned land must offer the purchase");
    const auto before = game.drain_effects();
    const auto paused = [](const std::vector<rich4::EffectEvent> &effects, std::uint8_t ticks) {
        return std::count_if(effects.begin(), effects.end(), [&](const auto &effect) {
            return effect.kind == rich4::EffectKind::pause && effect.object_kind == ticks;
        });
    };
    require(paused(before, 5) == 1 && paused(before, 8) == 0, "walk end must hold 5 ticks before landing");
    require(game.respond(0), "purchase rejected");
    const auto events = game.drain_events();
    const auto after = game.drain_effects();
    require(paused(after, 8) == 1 && std::any_of(events.begin(), events.end(), [](const auto &event) {
                return event.kind == rich4::GameEventKind::land_purchased;
            }) && after.back().kind == rich4::EffectKind::pause && after.back().after_events == events.size(),
            "property landing must hold 8 ticks after the purchase, before the next player");
}
int main() {
    try {
        landing_pauses();
        dice_and_paths();
        dice_effect_is_presentation_only();
        parachute_marks_a_spawned_player();
        tool_and_card_effects();
        missile_presentation_order();
        land_god_own_upgrade();
        property_and_rent();
        alliance_rent();
        bank_and_failure();
        thirty_days_and_timing();
        monthly_settlement();
        same_save_interactive_reload_rng();
        save_reload_and_deferred_spawn();
        facilities_events_and_rescue();
        teleport_landing_rules();
        service_and_auction_choices();
        god_spinner_presentation_order();
        native_actions_and_money_spinner();
        loan_and_free_toll();
        special_financing();
        computer_atm_financing();
        flight_and_confinement_objects();
        direct_trading();
        lifecycle_and_finished_saves();
        ai_settings_autopilot();
        explicit_unsupported_and_save_guard();
        interactive_mini_games();
        hospital_and_prison_buildings();
        std::cout << "native game rule tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "native game tests: " << error.what() << '\n';
        return 1;
    }
}
