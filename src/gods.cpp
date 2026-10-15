// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/gods.hpp"
#include "rich4/calendar.hpp"
#include "rich4/legacy_float.hpp"
#include "rich4/services.hpp"
#include <bit>
#include <stdexcept>

namespace rich4::gods {
namespace {
constexpr std::array<std::int16_t, 18> luck{0,    -100, -200, -100, -200, 100, 200,  100, 200,
                                            -100, 100,  0,    -500, 0,    0,   1000, 0,   0};
constexpr std::array<std::int16_t, 18> money{0,  100, 150, 0, 0, -60, -100, 0, 0,
                                             60, -60, 0,   0, 0, 0,   -200, 0, 0};
constexpr std::array<std::int16_t, 18> physical{0,  0,   0, 100, 150, 0, 0,    -60, -100,
                                                60, -60, 0, 0,   0,   0, -200, 0,   0};
std::int16_t add16(std::int16_t a, std::int16_t b) {
    return std::bit_cast<std::int16_t>(
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(a) + static_cast<std::uint16_t>(b)));
}
std::int32_t add32(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(a) + std::bit_cast<std::uint32_t>(b));
}
std::int32_t mul32(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(a) * std::bit_cast<std::uint32_t>(b));
}
Result missing(const char *text) {
    return {false, false, false, text};
}
void voice(const Callbacks &callback, std::size_t actor, std::uint8_t category) {
    if (callback.voice)
        callback.voice(actor, category);
}
void prize_voice(Random &random, const Callbacks &callbacks, std::size_t actor, unsigned amount) {
    if (amount > 100)
        voice(callbacks, actor, 0);
    else if (amount > 50)
        voice(callbacks, actor, static_cast<std::uint8_t>(random.next() & 1U));
    else if (amount)
        voice(callbacks, actor, 2);
}
void income_voice(const World &world, Random &random, const Callbacks &callbacks, std::size_t actor,
                  std::int32_t amount) {
    if (amount >= mul32(9000, world.header.price_index))
        voice(callbacks, actor, 6);
    else if (amount >= mul32(5000, world.header.price_index))
        voice(callbacks, actor, static_cast<std::uint8_t>(6 + (random.next() & 1U)));
    else if (amount >= mul32(2000, world.header.price_index))
        voice(callbacks, actor, 8);
}
bool ended(const Callbacks &callbacks) {
    return callbacks.game_over && callbacks.game_over();
}
} // namespace

MoneySpinner::MoneySpinner(std::uint8_t kind) {
    if (kind != 1 && kind != 2 && kind != 5 && kind != 6)
        throw std::invalid_argument("God has no money spinner");
    omitted_digit_ = kind & 1U;
}
void MoneySpinner::stop() noexcept {
    if (phase_ == 1)
        phase_ = 2;
}
std::int32_t MoneySpinner::amount() const noexcept {
    int result = digits_[1] / 2 * 100 + digits_[2] / 2 * 10 + digits_[3] / 2;
    if (omitted_digit_ == 0)
        result += digits_[0] / 2 * 1000;
    return result;
}
bool MoneySpinner::advance_digits(unsigned mask, unsigned slowing) {
    if (slowing) {
        if (last_slowing_digit_ != slowing && (digits_[4 - slowing] & 1U)) {
            slowing_period_ = 1;
            slowing_elapsed_ = 0;
            last_slowing_digit_ = slowing;
        }
        if (slowing_period_) {
            if (slowing_period_ <= slowing_elapsed_) {
                slowing_elapsed_ = 0;
                ++slowing_period_;
            }
            ++slowing_elapsed_;
        }
    }
    for (unsigned reverse = 0; reverse < 4 - omitted_digit_; ++reverse) {
        const auto digit = 3 - reverse;
        if ((mask & (1U << reverse)) == 0)
            continue;
        if (4 - digit == slowing && slowing_period_ > slowing_elapsed_)
            continue;
        digits_[digit] = static_cast<std::uint8_t>((digits_[digit] + 1) % 20);
    }
    if (slowing_period_ == 8) {
        slowing_period_ = slowing_elapsed_ = 0;
        return true;
    }
    return false;
}
void MoneySpinner::tick(Random &random, bool computer) {
    if (finished())
        return;
    if (phase_ < 3 && frames_ % 10 == 0)
        for (auto &digit : digits_)
            digit = static_cast<std::uint8_t>((random.next() % 10) * 2 + 1);
    ++frames_;
    if (computer && frames_ >= 40 && phase_ == 1)
        phase_ = 2;
    switch (phase_) {
    case 1:
        (void)advance_digits(15, 0);
        break;
    case 2:
        (void)advance_digits(15, 0);
        phase_ = 3;
        frames_ = 0;
        break;
    case 3:
        (void)advance_digits(15, 0);
        if (frames_ == 4)
            phase_ = 4;
        break;
    case 4:
        if (advance_digits(15, 1))
            phase_ = 5;
        break;
    case 5:
        if (advance_digits(14, 2))
            phase_ = 6;
        break;
    case 6:
        if (advance_digits(12, 3))
            phase_ = omitted_digit_ + 7;
        break;
    case 7:
        if (advance_digits(8, 4))
            phase_ = 8;
        break;
    case 8:
        phase_ = 9;
        frames_ = 0;
        break;
    case 9:
        if (frames_ == 40)
            phase_ = 10;
        break;
    default:
        throw std::logic_error("Invalid money-spinner phase");
    }
}
std::int32_t auto_money(std::uint8_t kind, Random &random) {
    MoneySpinner spinner(kind);
    for (unsigned frames = 0; !spinner.finished(); ++frames) {
        if (frames > 1000)
            throw std::logic_error("Money spinner did not stop");
        spinner.tick(random, true);
    }
    return spinner.amount();
}

bool attachable(const World &world, std::uint16_t object) {
    if (!object)
        return false;
    const auto kind = world.objects.at(object - 1).kind;
    return (kind >= 1 && kind <= 10) || kind == 12 || kind == 15;
}
Result detach(World &world, std::size_t actor, const Callbacks &callbacks) {
    const auto &status = world.players.at(actor).status;
    if (!status.god_info)
        return {};
    if (!callbacks.remove_object)
        return missing("神明移除规则尚未接入");
    const auto kind = world.objects.at(status.god_info - 1).kind;
    const bool absent =
        status.days_in_hotel || status.days_disappearing || status.days_in_prison || status.days_in_hospital;
    callbacks.remove_object(status.god_info);
    if (callbacks.sound)
        callbacks.sound(actor, 54); // rich4_gods.asm:238 board preset [19]
    if (callbacks.leaving)
        callbacks.leaving(actor, kind); // rich4_gods.asm:239-419: the god rises off the board
    if (!absent && ((kind >= 5 && kind <= 8) || kind == 15))
        voice(callbacks, actor, 23);
    return {true, true, false, "神明离开"};
}
Result attach(World &world, Random &random, std::size_t actor, std::uint16_t id, std::uint16_t source_node,
              const Callbacks &callbacks) {
    auto &player = world.players.at(actor);
    if (!attachable(world, id))
        return {};
    if (source_node >= world.nodes.size())
        throw std::out_of_range("God source node is outside map");
    auto &object = world.objects.at(id - 1);
    const auto kind = object.kind;
    if (player.status.god_info && !callbacks.remove_object)
        return missing("旧神明移除规则尚未接入");
    if ((kind == 1 || kind == 2 || kind == 5 || kind == 6) && !callbacks.choose_money)
        return missing("神明数字转盘尚未接入");
    if ((kind == 1 || kind == 5 || kind == 6) && !callbacks.pay)
        return missing("神明资金结算尚未接入");
    object.node_id = 0; // Original order avoids the incoming god obstructing old-god respawn.
    if (player.status.god_info) {
        const auto result = detach(world, actor, callbacks);
        if (!result.supported)
            return result;
    }
    player.status.god_info = static_cast<std::uint8_t>(id);
    object.node_id = player.state.node_id;
    object.attached_player = static_cast<std::uint8_t>(actor + 1);
    object.remaining_days = kind == 15 ? 13 : 7;
    if (source_node)
        world.nodes.at(source_node).flags &= ~0x00ff0000U;
    player.status.god_luck_modifier = add16(player.status.god_luck_modifier, luck[kind]);
    player.status.money_fortune_modifier = add16(player.status.money_fortune_modifier, money[kind]);
    player.status.other_fortune_modifier = add16(player.status.other_fortune_modifier, physical[kind]);
    // rich4_gods.asm jump table: bad gods (5..8, 15) first say line 22 (_rich4_player_say), THEN
    // play the appearance FLC and the arrival caption (fcn_0040ef1b, 0040efe4, 0040f083, 0040f155,
    // 0040f2eb); every other god plays FLC then caption. Presentation only: no randomness drawn.
    const bool bad = kind == 5 || kind == 6 || kind == 7 || kind == 8 || kind == 15;
    if (bad)
        voice(callbacks, actor, 22);
    if (callbacks.appeared)
        callbacks.appeared(actor, kind);
    if (callbacks.arrival)
        callbacks.arrival(actor, kind);
    if (kind == 1 || kind == 5) {
        const auto amount = callbacks.choose_money(kind);
        for (std::size_t other = 0; other < world.players.size() && !ended(callbacks); ++other) {
            if (other == actor || !world.players[other].state.controller_flags)
                continue;
            if (kind == 1)
                callbacks.pay(static_cast<std::int32_t>(other), static_cast<std::int32_t>(actor), amount, 1);
            else
                callbacks.pay(static_cast<std::int32_t>(actor), static_cast<std::int32_t>(other), amount, 0);
        }
        if (kind == 1 && amount > 700 && !ended(callbacks))
            voice(callbacks, actor, 8);
    } else if (kind == 2) {
        const auto amount = callbacks.choose_money(kind);
        player.state.cash = add32(player.state.cash, amount);
        player.status.cumulative_income += std::bit_cast<std::uint32_t>(amount);
        if (amount >= mul32(5000, world.header.price_index))
            income_voice(world, random, callbacks, actor, amount);
    } else if (kind == 3 || kind == 4) {
        unsigned worth = 0;
        for (unsigned i = 0; i < (kind == 3 ? 1U : 2U); ++i) {
            const auto card = services::receive_random_card(world, random, actor);
            if (card)
                worth += services::card_prices[card - 1];
        }
        // Original speaks for current_player even when attach was called on another slot.
        prize_voice(random, callbacks, world.header.current_player, worth);
    } else if (kind == 6) {
        callbacks.pay(static_cast<std::int32_t>(actor), -1, callbacks.choose_money(kind), 0);
    } else if (kind == 7) {
        const auto count = card_count(player.state.cards);
        if (count) {
            const auto card = player.state.cards[random.next() % count];
            (void)consume_card(player.state.cards, world.card_supply, card);
        }
    } else if (kind == 8) {
        (void)drop_half_cards(player.state.cards, world.card_supply);
    } else if (kind == 15) {
        const auto traffic = player.state.traffic_method & 3U;
        if (traffic)
            ++player.state.tool_amounts[traffic == 3 ? 11 : traffic + 3];
        player.state.traffic_method = 0;
        player.state.dice_count = 1;
        // Original ignores both sell-all return values here: no point credit.
        (void)sell_stored_tools(player.state.tool_amounts, world.tool_supply, services::tool_prices);
        (void)sell_all_cards(player.state.cards, world.card_supply, services::card_prices);
    }
    return {true, true, false, "神明附身"};
}

Result touch(World &world, Random &random, std::size_t actor, std::uint16_t id, bool stopped,
             const Callbacks &callbacks) {
    auto &player = world.players.at(actor);
    if (!id || !player.state.controller_flags)
        return {};
    const auto &object = world.objects.at(id - 1);
    const auto kind = object.kind;
    if (kind == 0 || kind > 18)
        throw SaveError("Unknown object type at encounter");
    if (object.attached_player)
        return {};
    if (kind == 15 || (!stopped && kind != 16))
        return {};
    if (attachable(world, id))
        return attach(world, random, actor, id, player.state.node_id, callbacks);
    if (kind == 18) {
        if (player.status.attached_bomb)
            return {};
        auto &bomb = world.objects.at(id - 1);
        player.status.attached_bomb = static_cast<std::uint8_t>(id);
        bomb.attached_player = static_cast<std::uint8_t>(actor + 1);
        bomb.remaining_days = 38;
        world.nodes.at(player.state.node_id).flags &= ~0x00ff0000U;
        return {true, true, false, "定时炸弹附着"};
    }
    if (!callbacks.remove_object)
        return missing("地上对象移除规则尚未接入");
    if ((kind == 17 || (kind == 11 && player.state.traffic_method == 0)) && !callbacks.hospital)
        return missing("对象碰撞受伤规则尚未接入");
    if (kind == 11) {
        callbacks.remove_object(11);
        if (callbacks.dog) // core_actions:2919/2945: vehicle movie, or the dust fight before the hospital
            callbacks.dog(actor, player.state.traffic_method == 0);
        if (player.state.traffic_method == 0) {
            callbacks.hospital(actor, 3);
            return {true, true, true, "碰撞受伤住院"};
        }
        return {true, true, false, "乘车避过碰撞"};
    }
    if (kind == 13) {
        const auto tool = services::receive_random_tool(world, random, actor);
        if (!tool)
            return {}; // Original leaves the gift on the map when supply is empty.
        callbacks.remove_object(13);
        if (callbacks.sound)
            callbacks.sound(actor, 35); // core_actions.asm:2981 board preset [6]
        prize_voice(random, callbacks, actor, services::tool_prices[tool - 1]);
        return {true, true, false, "获得道具"};
    }
    if (kind == 14) {
        callbacks.remove_object(14);
        if (callbacks.sound)
            callbacks.sound(actor, 36); // core_actions.asm:3121 board preset [7]
        player.state.points = static_cast<std::uint16_t>(player.state.points + 500);
        voice(callbacks, actor, 0);
        return {true, true, false, "获得500点"};
    }
    callbacks.remove_object(id);
    if (kind == 17)
        callbacks.hospital(actor, 3);
    return {true, true, true, kind == 16 ? "遇到路障" : "踩中地雷住院"};
}

Result after_property(World &world, std::size_t actor, std::uint16_t node, const Callbacks &callbacks) {
    auto &player = world.players.at(actor);
    if (player.status.days_in_hotel || !player.state.controller_flags)
        return {};
    const auto god_id = player.status.god_info;
    const auto god = god_id && god_id <= world.objects.size() ? world.objects[god_id - 1].kind : 0;
    if (god != 9 && god != 10 && god != 12)
        return {};
    const auto code = world.nodes.at(node).type_and_index;
    if (god == 9) {
        if (!callbacks.upgrade_property)
            return missing("神明房屋升级规则尚未接入");
        const auto result = callbacks.upgrade_property(code);
        return {true, (result & 1U) != 0, false, (result & 1U) ? "神明帮助房屋升级" : ""};
    }
    std::uint8_t *owner = nullptr;
    std::uint8_t level = 0;
    std::uint16_t base = 0;
    std::uint32_t *expiry = nullptr;
    if (code > 2000 && code < 4000) {
        auto &land = world.lands.at(code - 2000);
        owner = &land.economy.owner_code;
        level = land.economy.level;
        base = land.land_price;
        expiry = &land.expired_date;
    } else if (code > 4000 && code < 6000) {
        auto &facility = world.facilities.at(code - 4000);
        owner = &facility.owner_code;
        level = facility.level;
        base = facility.land_price;
        expiry = &facility.expired_date;
    }
    if (!owner || (god == 10 && level == 0) || (god == 12 && *owner == actor + 1))
        return {};
    if (*owner && !callbacks.hostility)
        return missing("神明地产敌意规则尚未接入");
    if (god == 10) {
        if (!callbacks.downgrade_property)
            return missing("神明房屋降级规则尚未接入");
        if (*owner)
            callbacks.hostility(*owner - 1, actor, mul32(code < 4000 ? 60 : 30, world.header.price_index));
        callbacks.downgrade_property(code);
        return {true, true, false, "神明破坏房屋"};
    }
    if (*owner) {
        // Original stores an x87 result as binary64, then passes it to an
        // integer-argument function without conversion. Preserve the LOW word,
        // not the mathematically intended hostility amount (often zero).
        // Constants at0x46350c/10 are2.0f and5.0f; preserve divide before
        // multiply, and the x87-to-binary64 spill before inspecting its bits.
        using E = numeric::Extended;
        const auto ratio = (E::from(level) + E::from(2.0)) / E::from(5.0);
        const double value = (E::from(mul32(base, world.header.price_index)) * ratio).double_precision();
        const auto low = static_cast<std::uint32_t>(std::bit_cast<std::uint64_t>(value));
        callbacks.hostility(*owner - 1, actor, std::bit_cast<std::int32_t>(low));
    } else if (world.lease_mode) {
        constexpr std::array<std::uint32_t, 6> intervals{0, 0x20000, 0x10000, 0x600, 0x300, 0x100};
        if (world.lease_mode >= intervals.size())
            throw SaveError("Invalid original lease mode");
        *expiry = calendar::add_packed_interval(
            calendar::pack({world.header.date.year, world.header.date.month, world.header.date.day}),
            intervals[world.lease_mode]);
    }
    *owner = static_cast<std::uint8_t>(actor + 1);
    voice(callbacks, world.header.current_player, 0);
    return {true, true, false, "神明取得地产"};
}

Result after_successful_purchase(World &world, Random &random, std::size_t actor, std::uint16_t node,
                                 const Callbacks &callbacks) {
    const auto god_id = world.players.at(actor).status.god_info;
    const auto god = god_id && god_id <= world.objects.size() ? world.objects[god_id - 1].kind : 0;
    if (god != 3 && god != 4)
        return {};
    if (!callbacks.upgrade_property)
        return missing("福神房屋升级规则尚未接入");
    const auto result = callbacks.upgrade_property(world.nodes.at(node).type_and_index);
    if (result & 1U)
        voice(callbacks, actor, static_cast<std::uint8_t>((result & 0x80U) ? 15 : random.next() & 1U));
    return {true, (result & 1U) != 0, false, (result & 1U) ? "福神帮助房屋升级" : ""};
}
} // namespace rich4::gods
