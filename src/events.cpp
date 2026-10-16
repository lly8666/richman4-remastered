// SPDX-License-Identifier: GPL-3.0-or-later
// News/fortune dispatch translated from original handlers; see migration 014.
#include "rich4/events.hpp"
#include "rich4/mod_rules.hpp"
#include "rich4/inventory.hpp"
#include "rich4/legacy_float.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <stdexcept>
#include <vector>

namespace rich4 {
namespace {
bool alive(const WorldPlayer &player) {
    return player.state.controller_flags != 0;
}
bool on_board(const WorldPlayer &player) {
    const auto &status = player.status;
    return alive(player) && status.days_in_hotel == 0 && status.days_disappearing == 0 &&
           status.days_in_prison == 0 && status.days_in_hospital == 0;
}
std::int32_t add(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(a) + std::bit_cast<std::uint32_t>(b));
}
std::int32_t multiply(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(a) * std::bit_cast<std::uint32_t>(b));
}
// Exact integer times binary32 with truncation, avoiding host double rounding
// near an integer boundary (e.g. the original bank-hack 0.1f multiplier).
std::int32_t fraction32(std::int32_t amount, std::uint32_t fraction_bits) {
    const auto exponent = (fraction_bits >> 23) & 255U;
    if (exponent == 255)
        return std::numeric_limits<std::int32_t>::min();
    const bool negative = (amount < 0) != ((fraction_bits >> 31) != 0);
    const auto magnitude = amount < 0 ? std::uint64_t(-std::int64_t(amount)) : std::uint64_t(amount);
    const auto significand = (fraction_bits & 0x7fffffU) | (exponent == 0 ? 0U : 0x800000U);
    auto product = magnitude * significand;
    const auto shift = static_cast<int>(exponent == 0 ? 1 : exponent) - 150;
    if (shift >= 0) {
        if (shift >= 63 || product > (std::uint64_t(0x80000000) >> shift))
            return std::numeric_limits<std::int32_t>::min();
        product <<= shift;
    } else
        product = shift <= -64 ? 0 : product >> -shift;
    if (product > (negative ? 0x80000000ULL : 0x7fffffffULL))
        return std::numeric_limits<std::int32_t>::min();
    return static_cast<std::int32_t>(negative ? -static_cast<std::int64_t>(product)
                                              : static_cast<std::int64_t>(product));
}
void income(World &world, std::size_t slot, std::int64_t amount, bool cash = true) {
    auto &player = world.players.at(slot);
    // Event 23 awards 10% of a bank balance. Once deposits exceed 2.1b,
    // legacy add(int32,int32) truncated the ENTIRE balance on payout.
    auto &account = cash ? player.state.cash : player.state.bank_balance;
    account = mod::money_add(account, amount);
    // Original status statistics remain an unsigned 32-bit running counter.
    player.status.cumulative_income += static_cast<std::uint32_t>(amount);
}
void voice(const EventCallbacks &callbacks, std::size_t actor, std::uint8_t category) {
    if (callbacks.voice)
        callbacks.voice(actor, category);
}
void voice_money(const World &world, Random &random, std::int32_t amount, std::size_t actor,
                 std::uint8_t base, const EventCallbacks &callbacks) {
    if (amount >= multiply(9000, world.header.price_index))
        voice(callbacks, actor, base);
    else if (amount >= multiply(5000, world.header.price_index))
        voice(callbacks, actor, static_cast<std::uint8_t>(base + (random.next() & 1)));
    else if (amount >= (base == 6 ? multiply(2000, world.header.price_index) : 1))
        voice(callbacks, actor, static_cast<std::uint8_t>(base + 2));
}
int fortune_modifier(const WorldPlayer &player, Random &random, bool physical, bool adverse) {
    const auto resistance =
        physical ? player.status.other_fortune_modifier : player.status.money_fortune_modifier;
    if (physical || adverse) {
        if (resistance > 100)
            return 1;
        if (resistance > 50)
            return random.next() & 1;
        return resistance < 0 ? 2 : 0;
    }
    if (resistance > 100)
        return 2;
    if (resistance > 50)
        return (random.next() & 1) * 2;
    return resistance < 0 ? 1 : 0;
}
void insurance(World &world, std::size_t actor, std::int32_t amount, const EventCallbacks &callbacks) {
    if (world.players.at(actor).status.days_assurance == 0)
        return;
    for (std::size_t company = 1; company < world.commercials.size(); ++company) {
        if (world.commercials[company].commerce_type == 4) {
            callbacks.pay(static_cast<std::int32_t>(company + 100), static_cast<std::int32_t>(actor), amount,
                          1);
            return;
        }
    }
    throw SaveError("Insured event has no original insurance company on this map");
}
EventResult missing(std::uint8_t id, const char *feature) {
    return {false, id, std::string("Event requires ") + feature};
}
EventResult done(std::uint8_t id, const char *description) {
    return {true, id, description};
}
EventResult done(std::uint8_t id, const char *description, std::int32_t value) {
    auto result = done(id, description);
    result.value = value;
    return result;
}

struct Property {
    std::uint16_t code;
    std::uint16_t x, y;
    std::uint8_t owner, level;
};
std::vector<Property> properties(const World &world) {
    std::vector<Property> result;
    for (std::size_t id = 1; id < world.lands.size(); ++id) {
        const auto &land = world.lands[id];
        result.push_back({static_cast<std::uint16_t>(2000 + id), land.x, land.y, land.economy.owner_code,
                          land.economy.level});
    }
    for (std::size_t id = 1; id < world.facilities.size(); ++id) {
        const auto &facility = world.facilities[id];
        result.push_back({static_cast<std::uint16_t>(4000 + id), facility.x, facility.y, facility.owner_code,
                          facility.level});
    }
    return result;
}
template <class Predicate>
std::vector<Property> matching_properties(const World &world, Predicate predicate) {
    auto result = properties(world);
    std::erase_if(result, [&](const auto &property) { return !predicate(property); });
    return result;
}
const Property &choose(const std::vector<Property> &candidates, Random &random) {
    if (candidates.empty())
        throw SaveError("Original event has an empty candidate list");
    return candidates[static_cast<std::size_t>(random.next()) % candidates.size()];
}
void release_hotel_guests(World &world) {
    for (auto &player : world.players)
        if (alive(player) && player.status.days_in_hotel != 0)
            player.status.days_in_hotel = 0x80;
}
void destroy_property(World &world, std::uint16_t code, bool confiscate) {
    if (code > 2000 && code < 4000) {
        auto &land = world.lands.at(code - 2000);
        if (confiscate) {
            land.economy.owner_code = 0;
            land.economy.level = 0;
            land.economy.chain_store = 0;
            land.expired_date = 0;
        } else if (land.economy.level != 0) {
            --land.economy.level;
            if (land.economy.chain_store != 0) {
                land.economy.level = 0;
                land.economy.chain_store = 0;
            }
        }
    } else if (code > 4000 && code < 6000) {
        auto &facility = world.facilities.at(code - 4000);
        if (confiscate) {
            facility.owner_code = 0;
            facility.level = 0;
            facility.facility_type = 0;
            facility.expired_date = 0;
            release_hotel_guests(world);
        } else if (facility.level != 0) {
            --facility.level;
            if (facility.level == 0) {
                facility.facility_type = 0;
                release_hotel_guests(world);
            }
        }
    } else
        throw SaveError("Invalid property code in event");
}
void update_price(World &world, const Property &property, bool increase) {
    const auto new_price = [increase](std::uint16_t price) {
        // x87 multiplies the exact integer by a binary64 constant, then truncates
        // before storing. 0.7 is BELOW 7/10: divisible-by-ten positive prices
        // truncate one lower; host binary64 multiplication can round that away.
        if (increase)
            return static_cast<std::uint16_t>(std::uint32_t(price) * 13 / 10);
        const auto result = std::uint32_t(price) * 7 / 10;
        return static_cast<std::uint16_t>(result - (price != 0 && price % 10 == 0 ? 1U : 0U));
    };
    if (property.code < 4000) {
        const auto name = world.lands.at(property.code - 2000).economy.street_name;
        for (std::size_t id = 1; id < world.lands.size(); ++id) {
            auto &land = world.lands[id];
            if (land.economy.street_name == name)
                land.land_price = new_price(land.land_price);
        }
    } else {
        auto &facility = world.facilities.at(property.code - 4000);
        facility.land_price = new_price(facility.land_price);
    }
}
constexpr CardPrices card_prices{200, 200, 35, 25, 20, 20, 15, 20, 160, 180, 60, 15, 25, 20, 100,
                                 25,  20,  20, 40, 25, 25, 10, 20, 50,  30,  35, 35, 35, 40, 70};
constexpr ToolPrices tool_prices{15, 30, 25, 25, 80, 150, 100, 30, 30, 40, 95, 150, 250};
} // namespace

bool news_eligible(const World &world, std::uint8_t id) {
    if (id >= 36)
        throw SaveError("News deck contains an invalid event ID");
    switch (id) {
    case 0:
    case 1:
        return std::any_of(world.prison_roster.begin(), world.prison_roster.begin() + 4,
                           [](auto value) { return value != 0; });
    case 2:
    case 3:
        return std::any_of(world.hospital_roster.begin(), world.hospital_roster.begin() + 4,
                           [](auto value) { return value != 0; });
    case 4:
    case 5:
        return !matching_properties(world, [](const auto &p) { return p.level != 0; }).empty();
    case 15:
        // The original's eligibility counts built facilities too, but its handler
        // (rich4_news.asm fcn_0044a453) only picks built ordinary land and divides by that
        // count, crashing when there is none. Matching the handler only changes crash cases.
        return !matching_properties(world, [](const auto &p) { return p.level != 0 && p.code < 4000; })
                    .empty();
    case 7:
        return !matching_properties(world, [](const auto &p) { return p.owner == 0; }).empty();
    case 8:
    case 9:
    case 12:
        return !matching_properties(world, [](const auto &p) { return p.owner != 0; }).empty();
    case 10:
    case 13:
        for (const auto &player : world.players)
            if (alive(player))
                for (const auto &stock : player.stocks)
                    if (stock.amount != 0)
                        return true;
        return false;
    case 16:
    case 17:
        return std::any_of(world.players.begin(), world.players.end(), [id](const auto &player) {
            return alive(player) &&
                   (id == 16 ? player.state.traffic_method == 0 : player.state.traffic_method != 0);
        });
    case 28:
        return std::any_of(world.stocks.begin(), world.stocks.end(),
                           [](const auto &stock) { return stock.suspension_days != 0; });
    case 29:
        for (std::size_t i = 1; i < world.commercials.size(); ++i) {
            const auto owner = world.commercials[i].owner_code;
            if (owner != 0 && owner <= world.players.size() && on_board(world.players[owner - 1]))
                return true;
        }
        return false;
    case 35:
        return std::any_of(world.commercials.begin() + (world.commercials.empty() ? 0 : 1),
                           world.commercials.end(),
                           [](const auto &company) { return company.funds > 10000; });
    default:
        return true;
    }
}

std::optional<std::uint8_t> fortune_eligible(const World &world, std::size_t actor, std::uint8_t id) {
    if (id >= 37)
        throw SaveError("Fortune deck contains an invalid base event ID");
    const auto &player = world.players.at(actor);
    switch (id) {
    case 0:
    case 1:
        for (std::size_t i = 1; i < world.lands.size(); ++i) {
            const auto &land = world.lands[i].economy;
            if (land.owner_code == actor + 1 && (id == 0 ? land.level != 0 : land.level == 0))
                return id;
        }
        return std::nullopt;
    case 5:
        for (std::size_t i = 0; i < world.players.size(); ++i)
            if (i != actor && card_count(world.players[i].state.cards) != 0)
                return id;
        return std::nullopt;
    case 8:
    case 9:
        for (const auto &stock : player.stocks)
            if (stock.amount != 0)
                return id;
        return std::nullopt;
    case 10:
    case 11:
        if (player.state.traffic_method != 1 && player.state.traffic_method != 2)
            return std::nullopt;
        return static_cast<std::uint8_t>(9 + player.state.traffic_method);
    case 12:
    case 13:
        if (player.state.traffic_method > 1)
            return std::nullopt;
        return static_cast<std::uint8_t>(12 + player.state.traffic_method);
    case 14:
    case 15:
    case 16:
        if (player.state.traffic_method > 2)
            return std::nullopt;
        return static_cast<std::uint8_t>(14 + player.state.traffic_method);
    case 33:
    case 34:
    case 35:
    case 36:
        if (world.header.stage_id != 0)
            return std::nullopt;
        if (world.header.map_id > 3)
            throw SaveError("Fortune map-specific event is outside original maps");
        return static_cast<std::uint8_t>(id + world.header.map_id * 4);
    default:
        return id;
    }
}

EventSelection select_news(const World &world) {
    if (world.news_cursor >= world.news_deck.size())
        throw SaveError("News cursor is outside deck");
    auto cursor = world.news_cursor;
    for (std::uint32_t skipped = 0; skipped < world.news_deck.size(); ++skipped) {
        const auto id = world.news_deck[cursor];
        cursor = (cursor + 1) % 36;
        if (news_eligible(world, id))
            return {id, id, cursor, skipped};
    }
    throw SaveError("News deck has no eligible event");
}
EventSelection select_fortune(const World &world, std::size_t actor) {
    if (world.fortune_cursor >= world.fortune_deck.size())
        throw SaveError("Fortune cursor is outside deck");
    auto cursor = world.fortune_cursor;
    for (std::uint32_t skipped = 0; skipped < world.fortune_deck.size(); ++skipped) {
        const auto id = world.fortune_deck[cursor];
        cursor = (cursor + 1) % 37;
        if (const auto effective = fortune_eligible(world, actor, id))
            return {id, *effective, cursor, skipped};
    }
    throw SaveError("Fortune deck has no eligible event");
}

// A handler that cannot run (a `missing()` guard: no candidate, or an unwired callback) is
// treated like an ineligible card: the deck advances past it and the next eligible card is
// drawn, so a failed event can never stick at the cursor and repeat. Every guard returns
// before consuming RNG, and on shipped data the guards only cover cases where the original
// divides by zero, so no outcome reachable in the original changes. A NeedChoice exception
// still leaves the cursor in place so the same card re-runs with the answer.
EventResult apply_news(World &world, Random &random, const EventCallbacks &callbacks) {
    EventResult result;
    for (std::size_t attempt = 0; attempt < world.news_deck.size(); ++attempt) {
        const auto selected = select_news(world);
        result = apply_news_id(world, random, selected.event_id, callbacks);
        world.news_cursor = selected.next_cursor;
        if (result.supported)
            break;
    }
    return result;
}
EventResult apply_fortune(World &world, Random &random, std::size_t actor, const EventCallbacks &callbacks) {
    EventResult result;
    for (std::size_t attempt = 0; attempt < world.fortune_deck.size(); ++attempt) {
        const auto selected = select_fortune(world, actor);
        result = apply_fortune_id(world, random, actor, selected.event_id, callbacks);
        world.fortune_cursor = selected.next_cursor;
        if (result.supported)
            break;
    }
    return result;
}

EventResult apply_news_id(World &world, Random &random, std::uint8_t id, const EventCallbacks &callbacks) {
    if (id >= 36)
        throw SaveError("Invalid news handler ID");
    if ((id == 4 || id == 20) && !callbacks.area_damage)
        return missing(id, "original square-area damage");
    if (id == 7 && !callbacks.auction)
        return missing(id, "property auction choice");
    if (id >= 11 && id <= 13 && !callbacks.pay)
        return missing(id, "payment and bankruptcy handling");
    if ((id == 24 || id == 25 || id >= 30) && !callbacks.update_stocks)
        return missing(id, "stock price update");
    if (id == 29 && (!callbacks.try_mianzui_jiahuo || !callbacks.add_prison))
        return missing(id, "prison/card handling");

    if (id < 4) {
        auto &roster = id < 2 ? world.prison_roster : world.hospital_roster;
        std::uint8_t affected = 0;
        for (std::size_t slot = 0; slot < std::min<std::size_t>(world.players.size(), 4); ++slot)
            if (roster[slot] != 0) {
                affected = static_cast<std::uint8_t>(affected | (1U << slot));
                auto &days = id < 2 ? world.players[slot].status.days_in_prison
                                    : world.players[slot].status.days_in_hospital;
                if ((id & 1) == 0) {
                    days = 0x80;
                    roster[slot] = 0;
                } else
                    days = static_cast<std::uint8_t>((days + 3) & 0x7f);
            }
        auto result = done(id, id < 2 ? ((id & 1) ? "新闻：刑期延长三天" : "新闻：大赦释放")
                                      : ((id & 1) ? "新闻：住院延长三天" : "新闻：康复出院"));
        result.players = affected;
        result.value = 3;
        return result;
    }
    if (id == 4 || id == 20) {
        const auto candidates =
            matching_properties(world, [id](const auto &p) { return id == 20 || p.level != 0; });
        if (candidates.empty())
            return missing(id, "a valid disaster target");
        const auto property = choose(candidates, random);
        callbacks.area_damage(property.x, property.y, id == 4 ? 1 : 0, id == 4);
        if (callbacks.news_picture)
            callbacks.news_picture(id, property.code);
        auto result = done(id, id == 4 ? "新闻：灾害袭击，区域受损" : "新闻：地震，区域建筑降级");
        result.property = property.code;
        return result;
    }
    if (id == 5 || id == 15 || id == 19 || id == 21) {
        const auto candidates = matching_properties(world, [id](const auto &p) {
            return (id != 5 && id != 15) || (p.level != 0 && (id != 15 || p.code < 4000));
        });
        if (candidates.empty())
            return missing(id, "a built ordinary land (original event 15 otherwise divides by zero)");
        const auto property = choose(candidates, random);
        destroy_property(world, property.code, id == 5 || id == 19);
        if (id != 19 && callbacks.news_picture)
            callbacks.news_picture(id, property.code);
        if (property.owner != 0)
            voice(callbacks, property.owner - 1, static_cast<std::uint8_t>(3 + (random.next() & 1)));
        auto result =
            done(id, id == 5 || id == 19 ? "新闻：地产遭到破坏并被清除" : "新闻：建筑损坏，降级一层");
        result.property = property.code;
        return result;
    }
    if (id == 6 || id == 14) {
        const auto candidates = properties(world);
        if (candidates.empty())
            return missing(id, "a property for land-price news");
        const auto &property = choose(candidates, random);
        update_price(world, property, id == 6);
        auto result = done(id, id == 6 ? "新闻：地价上涨三成" : "新闻：地价下跌三成");
        result.property = property.code;
        return result;
    }
    if (id == 7) {
        const auto candidates = matching_properties(world, [](const auto &p) { return p.owner == 0; });
        if (candidates.empty())
            return missing(id, "an unowned auction property");
        const auto code = choose(candidates, random).code;
        callbacks.auction(code);
        auto result = done(id, "新闻：无主地产拍卖");
        result.property = code;
        return result;
    }
    if (id >= 8 && id <= 10) {
        std::array<std::int32_t, 4> totals{};
        if (world.players.size() > totals.size())
            throw SaveError("News supports at most four players");
        if (id == 10) {
            for (std::size_t player = 0; player < world.players.size(); ++player)
                for (const auto &stock : world.players[player].stocks)
                    totals[player] = add(totals[player], stock.amount);
        } else
            for (const auto &property : properties(world)) {
                if (property.owner != 0 && property.owner <= totals.size())
                    ++totals[property.owner - 1];
            }
        std::optional<std::size_t> winner;
        for (std::size_t slot = 0; slot < world.players.size(); ++slot)
            if (alive(world.players[slot])) {
                if (!winner || (id == 9 ? totals[slot] < totals[*winner] : totals[slot] > totals[*winner]))
                    winner = slot;
            }
        if (!winner)
            return missing(id, "a living player to receive the award");
        const auto award = multiply(id == 9 ? 5000 : 10000, world.header.price_index);
        income(world, *winner, award);
        voice(callbacks, *winner, static_cast<std::uint8_t>(6 + (id == 9 ? (random.next() & 1) : 0)));
        auto result = done(id, id == 8   ? "新闻：地产最多者获奖"
                               : id == 9 ? "新闻：地产最少者获补助"
                                         : "新闻：持股最多者获奖");
        result.subject = static_cast<std::int32_t>(*winner);
        result.value = award;
        return result;
    }
    if (id >= 11 && id <= 13) {
        std::vector<std::int32_t> taxes(world.players.size());
        for (std::size_t slot = 0; slot < world.players.size(); ++slot)
            if (alive(world.players[slot])) {
                if (id == 11)
                    taxes[slot] = world.players[slot].state.cash / 20;
                else if (id == 12) {
                    std::int32_t total = 0;
                    for (std::size_t i = 1; i < world.lands.size(); ++i) {
                        const auto &land = world.lands[i];
                        if (land.economy.owner_code == slot + 1)
                            total = add(total, add(land.land_price,
                                                   multiply(land.economy.level, land.price_per_level)));
                    }
                    for (std::size_t i = 1; i < world.facilities.size(); ++i) {
                        const auto &facility = world.facilities[i];
                        if (facility.owner_code == slot + 1)
                            total = add(total, add(facility.land_price,
                                                   multiply(facility.level, facility.price_per_level())));
                    }
                    taxes[slot] = multiply(total / 20, world.header.price_index);
                } else {
                    using E = numeric::Extended;
                    float value = 0;
                    for (std::size_t stock = 0; stock < world.stocks.size(); ++stock) {
                        if (world.players[slot].stocks[stock].amount == 0)
                            continue;
                        value = (E::from(world.players[slot].stocks[stock].amount) *
                                     E::from(std::bit_cast<float>(world.stocks[stock].float_bits[2])) +
                                 E::from(value))
                                    .single();
                    }
                    taxes[slot] =
                        multiply((E::from(value) * E::from(0.05)).integer(), world.header.price_index);
                }
            }
        for (std::size_t slot = 0; slot < taxes.size(); ++slot) {
            if (alive(world.players[slot]) && taxes[slot] != 0)
                callbacks.pay(static_cast<std::int32_t>(slot), -1, taxes[slot], 0);
            if (std::count_if(world.players.begin(), world.players.end(), alive) <= 1)
                break;
        }
        auto result =
            done(id, id == 11 ? "新闻：征收现金税" : id == 12 ? "新闻：征收地产税" : "新闻：征收股票税");
        for (std::size_t slot = 0; slot < std::min<std::size_t>(taxes.size(), 4); ++slot)
            if (taxes[slot] != 0) {
                result.players = static_cast<std::uint8_t>(result.players | (1U << slot));
                result.player_amounts[slot] = taxes[slot];
            }
        return result;
    }
    if (id == 16 || id == 17) {
        for (auto &player : world.players)
            if (alive(player) &&
                (id == 16 ? player.state.traffic_method == 0 : player.state.traffic_method != 0))
                player.status.days_stopping = 1;
        return done(id, id == 16 ? "新闻：行人暂停一天" : "新闻：车辆暂停一天");
    }
    if (id == 18) {
        const auto candidates = properties(world);
        if (candidates.empty())
            return missing(id, "a property for street damage");
        const auto property = choose(candidates, random);
        auto result = done(id, "新闻：整条街建筑受损");
        result.property = property.code;
        if (property.code < 4000) {
            const auto street = world.lands.at(property.code - 2000).economy.street_name;
            for (std::size_t i = 1; i < world.lands.size(); ++i)
                if (world.lands[i].economy.street_name == street)
                    destroy_property(world, static_cast<std::uint16_t>(2000 + i), false);
        } else
            destroy_property(world, property.code, false);
        return result;
    }
    if (id == 22) {
        for (auto &player : world.players)
            if (alive(player))
                player.status.days_bank_no_loans = 15;
        return done(id, "新闻：银行暂停贷款十五天");
    }
    if (id == 23) {
        auto result = done(id, "新闻：无贷款者获得存款一成奖励");
        for (std::size_t slot = 0; slot < world.players.size(); ++slot)
            if (alive(world.players[slot]) && world.players[slot].state.loan == 0) {
                const auto bonus = world.players[slot].state.bank_balance / 10;
                income(world, slot, bonus, false);
                if (slot < 4 && bonus != 0) {
                    result.players = static_cast<std::uint8_t>(result.players | (1U << slot));
                    result.player_amounts[slot] = bonus;
                }
            }
        return result;
    }
    if (id == 24 || id == 25) {
        for (auto &stock : world.stocks)
            stock.news_movement = id == 24 ? 1 : 0x10;
        callbacks.update_stocks(0);
        return done(id, id == 24 ? "新闻：股市下跌" : "新闻：股市上涨");
    }
    if (id == 26) {
        world.news_effect_days = 10;
        return done(id, "新闻：市场措施持续十天");
    }
    if (id == 27 || id == 28) {
        std::vector<std::size_t> choices;
        for (std::size_t i = 0; i < world.stocks.size(); ++i)
            if (id == 27 || world.stocks[i].suspension_days != 0)
                choices.push_back(i);
        if (choices.empty())
            return missing(id, "a suspended stock");
        const auto index = choices[static_cast<std::size_t>(random.next()) % choices.size()];
        auto &stock = world.stocks[index];
        stock.suspension_days = id == 27 ? 15 : 0;
        if (id == 27) {
            stock.float_bits[2] = stock.float_bits[1];
            auto history = add(world.stock_history_cursor, -1) % 144;
            if (history < 0)
                history += 144;
            world.stock_history_bits[index][static_cast<std::size_t>(history)] = stock.float_bits[1];
        }
        auto result = done(id, id == 27 ? "新闻：一家公司股票暂停交易十五天" : "新闻：股票恢复交易");
        result.subject = static_cast<std::int32_t>(index);
        return result;
    }
    if (id == 29) {
        std::vector<std::size_t> choices;
        for (std::size_t i = 1; i < world.commercials.size(); ++i)
            if (world.commercials[i].owner_code != 0)
                choices.push_back(i);
        if (choices.empty())
            return missing(id, "an owned company");
        const auto company = choices[static_cast<std::size_t>(random.next()) % choices.size()];
        const auto owner = world.commercials[company].owner_code;
        if (owner > world.players.size())
            throw SaveError("Company has an invalid owner");
        const auto target = callbacks.try_mianzui_jiahuo(owner - 1);
        if (target >= 0)
            callbacks.add_prison(static_cast<std::size_t>(target), 5);
        auto result = done(id, "新闻：公司负责人面临五天监禁");
        result.company = static_cast<std::int32_t>(company);
        result.subject = owner - 1;
        if (target >= 0 && target < 4)
            result.players = static_cast<std::uint8_t>(1U << target);
        return result;
    }
    if (id >= 30) {
        std::vector<std::size_t> choices;
        for (std::size_t i = 1; i < world.commercials.size(); ++i)
            if (id != 35 || world.commercials[i].funds > 10000)
                choices.push_back(i);
        if (choices.empty())
            return missing(id, "a qualifying company");
        const auto index = choices[static_cast<std::size_t>(random.next()) % choices.size()];
        auto &company = world.commercials[index];
        constexpr std::array<std::int32_t, 5> deltas{-10000, 20000, -20000, -10000, -5000};
        constexpr std::array<std::uint8_t, 5> movements{3, 0x30, 4, 3, 3};
        auto movement = std::uint8_t(0);
        if (id == 35) {
            const auto previous = company.funds;
            company.funds = add(previous, previous);
            company.funds_change = add(company.funds_change, company.funds);
            movement = static_cast<std::uint8_t>(static_cast<std::uint32_t>(previous / 10000) << 4);
        } else {
            company.funds = add(company.funds, deltas[id - 30]);
            company.funds_change = add(company.funds_change, deltas[id - 30]);
            movement = movements[id - 30];
        }
        if (company.stock_index < world.stocks.size()) {
            world.stocks[company.stock_index].news_movement = movement;
            callbacks.update_stocks(static_cast<std::uint32_t>(company.stock_index + 1));
        }
        auto result = done(id, "新闻：公司业绩与股价变化");
        result.company = static_cast<std::int32_t>(index);
        return result;
    }
    throw SaveError("Unreachable news dispatch");
}

EventResult apply_fortune_id(World &world, Random &random, std::size_t actor, std::uint8_t id,
                             const EventCallbacks &callbacks) {
    if (id >= 49)
        throw SaveError("Invalid fortune handler ID");
    auto &player = world.players.at(actor);
    if ((id == 2 || id == 4 ||
         (id >= 14 && id <= 31 && (id < 20 || id == 23 || id == 24 || id == 26 || id == 30))) &&
        !callbacks.pay)
        return missing(id, "payment and insurance handling");
    if (id == 2 && !callbacks.set_loan_due_date)
        return missing(id, "original loan due-date update");
    if ((id == 8 || id == 9) && !callbacks.sell_stock)
        return missing(id, "original stock sale");
    if ((id == 6 || id == 7) && (!callbacks.disappear || !callbacks.try_mianzui_jiahuo))
        return missing(id, "absence/card handling");
    if ((id == 12 || id == 13 || ((id == 15 || id == 16) && player.state.traffic_method == 0)) &&
        (!callbacks.add_hospital || !callbacks.try_mianzui_jiahuo))
        return missing(id, "hospital/card handling");
    if (id >= 33 && (!callbacks.add_prison || !callbacks.try_mianzui_jiahuo))
        return missing(id, "prison/card handling");
    if (id == 5 && player.state.controller_flags == 1 && !callbacks.choose_card)
        return missing(id, "human card selection");

    if (id == 0 || id == 1) {
        const auto candidates = matching_properties(world, [&](const auto &p) {
            return p.code < 4000 && p.owner == actor + 1 && (id == 0 ? p.level != 0 : p.level == 0);
        });
        if (candidates.empty())
            return missing(id, "an eligible owned land");
        const auto property = choose(candidates, random);
        auto &land = world.lands.at(property.code - 2000);
        if (id == 0) {
            income(world, actor, multiply(land.economy.level, land.price_per_level));
            land.economy.level = 0;
            land.economy.chain_store = 0;
        } else {
            income(world, actor, land.land_price);
            land.economy.owner_code = 0;
            land.expired_date = 0;
        }
        voice(callbacks, actor, static_cast<std::uint8_t>(3 + (random.next() & 1)));
        return done(id, id == 0 ? "命运：房屋被拆除，获得建筑补偿" : "命运：空地被征收，获得地价补偿");
    }
    if (id == 2 || id == 3) {
        auto amount = multiply(10000, world.header.price_index);
        const auto modifier = fortune_modifier(player, random, false, true);
        if (modifier == 1)
            return done(id, "命运：神明阻止了损失", amount);
        if (id == 2) {
            if (modifier == 2)
                amount = add(amount, amount);
            // Even a small identity-theft loan must not narrow the existing
            // 64-bit MOD debt back to a signed 32-bit integer.
            player.state.loan = mod::money_add(player.state.loan, amount);
            callbacks.set_loan_due_date(actor);
            insurance(world, actor, amount, callbacks);
        } else
            player.status.days_rejected_by_bank =
                static_cast<std::uint8_t>(player.status.days_rejected_by_bank + 30);
        auto result = done(id, id == 2 ? "命运：身份遭盗用，新增贷款" : "命运：银行拒绝往来三十天");
        result.value = amount;
        return result;
    }
    if (id == 4) {
        for (std::size_t slot = 0; slot < world.players.size(); ++slot) {
            if (slot == actor || !alive(world.players[slot]) || world.players[slot].state.bank_balance == 0)
                continue;
            const auto amount =
                fraction32(world.players[slot].state.bank_balance, std::bit_cast<std::uint32_t>(0.1F));
            callbacks.pay(static_cast<std::int32_t>(slot), static_cast<std::int32_t>(actor), amount, 4);
        }
        auto result = done(id, "命运：取得其他玩家存款的一成");
        result.value = 10;
        return result;
    }
    if (id == 5) {
        bool received = false;
        for (std::size_t victim = 0; victim < world.players.size(); ++victim) {
            if (victim == actor || !alive(world.players[victim]))
                continue;
            auto &cards = world.players[victim].state.cards;
            const auto count = card_count(cards);
            if (count == 0)
                continue;
            std::uint8_t card = 0;
            if (player.state.controller_flags > 1)
                card = cards[static_cast<std::size_t>(random.next()) % count];
            else if (player.state.controller_flags == 1)
                card = callbacks.choose_card(actor, victim);
            else
                continue;
            received = true;
            if (card == 0 && player.state.controller_flags == 1)
                continue; // Original right-click cancellation.
            if (card == 0 || card > 30 || !has_card(cards, card))
                throw SaveError("Card choice is not in the victim's inventory");
            consume_card(cards, world.card_supply, card);
            receive_card(player.state.cards, world.card_supply, card_prices, card);
        }
        if (received)
            voice(callbacks, actor, static_cast<std::uint8_t>(random.next() & 1));
        return done(id, "命运：从其他玩家各取得一张卡片");
    }
    if (id == 6 || id == 7 || id == 12 || id == 13 || id >= 33 ||
        ((id == 15 || id == 16) && player.state.traffic_method == 0)) {
        auto days = static_cast<std::uint8_t>(id >= 33 ? 3 + ((id - 33) % 4) * 2 : 3);
        const auto modifier = fortune_modifier(player, random, true, true);
        if (modifier == 1) {
            if (id >= 33)
                voice(callbacks, actor, 0);
            return done(id, "命运：神明阻止了灾祸", days);
        }
        if (modifier == 2)
            days = static_cast<std::uint8_t>(days * 2);
        const auto target = callbacks.try_mianzui_jiahuo(actor);
        if (target >= 0) {
            if (id == 6 || id == 7)
                callbacks.disappear(static_cast<std::size_t>(target), days, id == 6 ? 0 : 1);
            else if (id >= 33)
                callbacks.add_prison(static_cast<std::size_t>(target), days);
            else
                callbacks.add_hospital(static_cast<std::size_t>(target), days);
        }
        auto result = done(id, id == 6    ? "命运：出国旅行"
                               : id == 7  ? "命运：被外星人带走"
                               : id >= 33 ? "命运：面临监禁"
                                          : "命运：受伤住院");
        result.value = days;
        return result;
    }
    if (id == 8 || id == 9) {
        if (fortune_modifier(player, random, false, true) == 1)
            return done(id, "命运：神明保护了股票");
        for (std::size_t stock = 0; stock < player.stocks.size(); ++stock) {
            const auto amount =
                id == 8 ? fraction32(player.stocks[stock].amount, std::bit_cast<std::uint32_t>(0.1F))
                        : player.stocks[stock].amount;
            if (id == 8 || amount != 0)
                callbacks.sell_stock(actor, stock, amount, id == 9);
        }
        if (id == 9)
            voice(callbacks, actor, 5);
        auto result = done(id, id == 8 ? "命运：损失一成股票" : "命运：全部股票被变卖");
        result.value = 10;
        return result;
    }
    if (id == 10 || id == 11) {
        if (fortune_modifier(player, random, true, true) == 1)
            return done(id, "命运：神明保护了交通工具");
        player.state.traffic_method = 0;
        player.state.dice_count = 1;
        ++world.tool_supply[id == 10 ? 4 : 5];
        voice(callbacks, actor, static_cast<std::uint8_t>(3 + (id == 10 ? (random.next() & 1) : 0)));
        return done(id, id == 10 ? "命运：机车失窃，改为步行" : "命运：汽车毁损，改为步行");
    }
    if (id >= 14 && id <= 31) {
        constexpr std::array<std::int32_t, 18> amounts{-3000, -3000, -3000, -6000, -600,  -1500,
                                                       1000,  2000,  3000,  -1000, -2000, 10000,
                                                       -8000, 4000,  6000,  8000,  -5000, 5000};
        const auto nominal = amounts[id - 14];
        const bool adverse = nominal < 0;
        auto amount = multiply(adverse ? -nominal : nominal, world.header.price_index);
        const auto modifier = fortune_modifier(player, random, false, adverse);
        if (modifier == 1) {
            if (adverse)
                voice_money(world, random, amount, actor, 12, callbacks);
            return done(id, adverse ? "命运：神明阻止了损失" : "命运：奖励被神明阻止", amount);
        }
        if (modifier == 2)
            amount = add(amount, amount);
        if (!adverse) {
            income(world, actor, amount);
            voice_money(world, random, amount, actor, 6, callbacks);
        } else {
            callbacks.pay(static_cast<std::int32_t>(actor), -1, amount, 0);
            if (alive(player) && std::count_if(world.players.begin(), world.players.end(), alive) > 1) {
                voice_money(world, random, amount, actor, 9, callbacks);
                insurance(world, actor, amount, callbacks);
            }
        }
        auto result = done(id, adverse ? "命运：支付 " : "命运：获得 ");
        result.message += std::to_string(amount);
        result.value = amount;
        return result;
    }
    if (id == 32) {
        if (fortune_modifier(player, random, true, true) == 1)
            return done(id, "命运：神明保护了卡片与道具");
        if (player.state.traffic_method != 0) {
            const auto transport = player.state.traffic_method & 3;
            if (transport == 1)
                mod::grant_equipped_tool(player.state.tool_amounts[4]);
            else if (transport == 2)
                mod::grant_equipped_tool(player.state.tool_amounts[5]);
            else if (transport == 3)
                mod::grant_equipped_tool(player.state.tool_amounts[11]);
            player.state.traffic_method = 0;
            player.state.dice_count = 1;
        }
        auto points = sell_stored_tools(player.state.tool_amounts, world.tool_supply, tool_prices);
        points += sell_all_cards(player.state.cards, world.card_supply, card_prices);
        mod::credit_points(player.state.points, points);
        voice(callbacks, actor, 3);
        return done(id, "命运：卡片与道具全部变卖为点数");
    }
    throw SaveError("Unreachable fortune dispatch");
}

} // namespace rich4
