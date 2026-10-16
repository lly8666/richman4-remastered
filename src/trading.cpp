// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/trading.hpp"

#include "rich4/inventory.hpp"
#include "rich4/mod_rules.hpp"
#include "rich4/legacy_float.hpp"
#include "rich4/random.hpp"
#include "rich4/services.hpp"
#include "rich4/stocks.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace rich4::trading {
namespace {
using E = numeric::Extended;
constexpr std::array<int, 13> tool_aggression{0, 1, 1, 1, 0, 0, 2, 0, 1, 2, 1, 2, 2};

std::int32_t add32(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(a) + std::bit_cast<std::uint32_t>(b));
}
std::int32_t mul32(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(a) * std::bit_cast<std::uint32_t>(b));
}
bool active(const World &w, std::size_t slot) {
    return slot < w.players.size() && slot < w.offers.size() && w.players[slot].state.controller_flags != 0;
}
bool is_land(const World &w, std::uint16_t id) {
    return id > 2000 && id < 4000 && static_cast<std::size_t>(id - 2000) < w.lands.size();
}
bool is_facility(const World &w, std::uint16_t id) {
    return id > 4000 && id < 6000 && static_cast<std::size_t>(id - 4000) < w.facilities.size();
}
struct Property {
    std::uint8_t owner, type, level;
    std::int32_t value;
};
std::optional<Property> property(const World &w, std::uint16_t id) {
    if (is_land(w, id)) {
        const auto &p = w.lands[id - 2000];
        return Property{
            p.economy.owner_code, p.economy.chain_store, p.economy.level,
            mul32(add32(p.land_price, mul32(p.price_per_level, p.economy.level)), w.header.price_index)};
    }
    if (is_facility(w, id)) {
        const auto &p = w.facilities[id - 4000];
        return Property{
            p.owner_code, p.facility_type, p.level,
            mul32(add32(p.land_price, mul32(p.price_per_level(), p.level)), w.header.price_index)};
    }
    return std::nullopt;
}
std::optional<std::int32_t> inventory_price(const World &w, OfferKind kind, std::uint16_t id) {
    if (kind == OfferKind::tool && id >= 1 && id <= services::tool_prices.size())
        return mul32(mul32(services::tool_prices[id - 1], 100), w.header.price_index);
    if (kind == OfferKind::card && id >= 1 && id <= services::card_prices.size())
        return mul32(mul32(services::card_prices[id - 1], 100), w.header.price_index);
    return std::nullopt;
}
bool current_offer(const World &w, std::size_t seller, const TradeOffer &offer) {
    if (seller >= w.players.size())
        return false;
    const auto &p = w.players[seller];
    switch (offer.kind) {
    case OfferKind::stock:
        return offer.item < p.stocks.size() && offer.quantity <= p.stocks[offer.item].amount;
    case OfferKind::property: {
        const auto land = property(w, offer.item);
        return land && land->owner == seller + 1 && land->type == offer.property_type &&
               land->level == offer.property_level;
    }
    case OfferKind::tool:
        return offer.item >= 1 && offer.item <= 13 && p.state.tool_amounts[offer.item - 1] != 0;
    case OfferKind::card:
        return offer.item >= 1 && offer.item <= 30 &&
               has_card(p.state.cards, static_cast<std::uint8_t>(offer.item));
    default:
        return true; // Original cleanup retains empty and unknown records.
    }
}
Result failure(const char *message) {
    return {false, message, false};
}
} // namespace

std::vector<Item> listable_items(const World &w, std::size_t seller) {
    std::vector<Item> result;
    if (!active(w, seller))
        return result;
    const auto &p = w.players[seller];
    for (std::size_t i = 0; i < p.stocks.size(); ++i)
        if (p.stocks[i].amount > 0)
            result.push_back({OfferKind::stock, static_cast<std::uint16_t>(i),
                              static_cast<std::uint16_t>(std::min<std::int32_t>(p.stocks[i].amount, 65535))});
    for (std::size_t i = 1; i < w.lands.size() && i < 2000; ++i)
        if (w.lands[i].economy.owner_code == seller + 1)
            result.push_back({OfferKind::property, static_cast<std::uint16_t>(2000 + i), 1});
    for (std::size_t i = 1; i < w.facilities.size() && i < 2000; ++i)
        if (w.facilities[i].owner_code == seller + 1)
            result.push_back({OfferKind::property, static_cast<std::uint16_t>(4000 + i), 1});
    for (std::uint16_t i = 1; i <= 13; ++i)
        if (p.state.tool_amounts[i - 1])
            result.push_back({OfferKind::tool, i, 1});
    for (std::uint16_t i = 1; i <= 30; ++i)
        if (has_card(p.state.cards, static_cast<std::uint8_t>(i)))
            result.push_back({OfferKind::card, i, 1});
    return result;
}

std::optional<ListingLimits> listing_limits(const World &w, std::size_t seller, OfferKind kind,
                                            std::uint16_t item, std::uint16_t quantity) {
    if (!active(w, seller))
        return std::nullopt;
    const auto &p = w.players[seller];
    std::int32_t reference = 0;
    switch (kind) {
    case OfferKind::stock: {
        if (item >= p.stocks.size() || !quantity || quantity > p.stocks[item].amount)
            return std::nullopt;
        const auto quote = std::bit_cast<float>(w.stocks[item].float_bits[2]);
        if (!std::isfinite(quote) || quote <= 0)
            return std::nullopt;
        reference = (E::from(quantity) * E::from(quote)).integer();
        break;
    }
    case OfferKind::property: {
        const auto land = property(w, item);
        if (!land || land->owner != seller + 1)
            return std::nullopt;
        reference = land->value;
        break;
    }
    case OfferKind::tool:
        if (item < 1 || item > 13 || !p.state.tool_amounts[item - 1])
            return std::nullopt;
        reference = *inventory_price(w, kind, item);
        break;
    case OfferKind::card:
        if (item < 1 || item > 30 || !has_card(p.state.cards, static_cast<std::uint8_t>(item)))
            return std::nullopt;
        reference = *inventory_price(w, kind, item);
        break;
    default:
        return std::nullopt;
    }
    const auto maximum = mul32(reference, 10);
    if (reference <= 0 || maximum <= 0)
        return std::nullopt;
    return ListingLimits{reference, maximum};
}

std::vector<Quote> quotes(const World &w) {
    std::vector<Quote> result;
    for (std::size_t seller = 0; seller < w.players.size() && seller < w.offers.size(); ++seller) {
        if (!active(w, seller))
            continue;
        for (std::size_t i = 0; i < w.offers[seller].size(); ++i) {
            const auto &offer = w.offers[seller][i];
            if (offer.kind >= OfferKind::stock && offer.kind <= OfferKind::card &&
                current_offer(w, seller, offer))
                result.push_back({seller, i, offer});
        }
    }
    return result;
}

Result post(World &w, std::size_t seller, const Listing &listing) {
    const auto limits = listing_limits(w, seller, listing.kind, listing.item, listing.quantity);
    if (!limits || listing.price < 1 || listing.price > limits->maximum_price)
        return failure("Invalid listing or price");
    auto &offers = w.offers[seller];
    const auto found = std::find_if(offers.begin(), offers.end(), [&](const auto &offer) {
        return offer.kind == OfferKind::empty || (offer.kind == listing.kind && offer.item == listing.item);
    });
    if (found == offers.end())
        return failure("All seven listing slots are occupied");
    found->kind = listing.kind;
    found->flags = 0;
    found->item = listing.item;
    found->price = listing.price;
    if (listing.kind == OfferKind::stock)
        found->quantity = listing.quantity;
    else if (listing.kind == OfferKind::property) {
        const auto p = *property(w, listing.item);
        found->property_type = p.type;
        found->property_level = p.level;
    }
    return {true, "Listed", false};
}

bool cancel(World &w, std::size_t seller, std::size_t index) {
    if (seller >= w.offers.size() || index >= 7 || w.offers[seller][index].kind == OfferKind::empty)
        return false;
    auto &offers = w.offers[seller];
    std::move(offers.begin() + static_cast<std::ptrdiff_t>(index + 1), offers.end(),
              offers.begin() + static_cast<std::ptrdiff_t>(index));
    offers.back() = {};
    return true;
}

std::size_t prune(World &w) {
    std::size_t removed = 0;
    for (std::size_t seller = 0; seller < w.players.size() && seller < w.offers.size(); ++seller) {
        bool deleted = false;
        std::size_t index = 0;
        // 0x42483e does not reset EDI after deleting: the cursor remains on the
        // shifted entry for the rest of this seller's seven inspection attempts.
        for (unsigned attempt = 0; attempt < 7; ++attempt) {
            if (!current_offer(w, seller, w.offers[seller][index])) {
                if (cancel(w, seller, index))
                    ++removed;
                deleted = true;
            }
            if (!deleted)
                ++index;
        }
    }
    return removed;
}

Result buy(World &w, std::size_t buyer, std::size_t seller, std::size_t index) {
    if (!active(w, buyer) || !active(w, seller) || buyer == seller || index >= 7)
        return failure("Invalid buyer or seller");
    const auto offer = w.offers[seller][index];
    if (offer.price <= 0 || !current_offer(w, seller, offer))
        return failure("Listing is no longer available");
    auto &recipient = w.players[buyer];
    auto &source = w.players[seller];
    if (recipient.state.cash < offer.price)
        return failure("Not enough cash");
    const auto buyer_cash = mod::money_sub(recipient.state.cash, offer.price);
    const auto seller_bank = mod::money_add(source.state.bank_balance, offer.price);
    bool owner_changed = false;
    switch (offer.kind) {
    case OfferKind::stock: {
        if (!offer.quantity)
            return failure("Invalid stock quantity");
        auto &acquired = recipient.stocks[offer.item];
        auto &sold = source.stocks[offer.item];
        const auto old_basis = std::bit_cast<float>(acquired.raw_cost_basis);
        if (!std::isfinite(old_basis) || acquired.amount < 0 ||
            acquired.amount > mod::max_holding_shares - offer.quantity)
            return failure("Invalid stock holding");
        const auto prior_cost = (E::from(static_cast<double>(acquired.amount)) *
                                 E::from(static_cast<double>(old_basis))).integer64();
        const auto total_cost = mod::money_add(prior_cost, offer.price);
        const auto new_amount = acquired.amount + offer.quantity;
        const auto new_basis = (E::from(static_cast<double>(total_cost)) /
                                E::from(static_cast<double>(new_amount))).single();
        acquired.amount = new_amount;
        sold.amount -= offer.quantity;
        if (!sold.amount)
            sold.raw_cost_basis = 0;
        acquired.raw_cost_basis = std::bit_cast<std::uint32_t>(new_basis);
        owner_changed = stocks::update_commercial_owner(w, buyer, offer.item);
        break;
    }
    case OfferKind::property:
        if (is_land(w, offer.item))
            w.lands[offer.item - 2000].economy.owner_code = static_cast<std::uint8_t>(buyer + 1);
        else
            w.facilities[offer.item - 4000].owner_code = static_cast<std::uint8_t>(buyer + 1);
        break;
    case OfferKind::tool:
        if (recipient.state.tool_amounts[offer.item - 1] >= mod::max_tool_amount)
            return failure("Tool inventory is full");
        (void)consume_tool(source.state.tool_amounts, w.tool_supply, static_cast<std::uint8_t>(offer.item));
        (void)receive_tool(recipient.state.tool_amounts, w.tool_supply,
                           static_cast<std::uint8_t>(offer.item));
        break;
    case OfferKind::card:
        if (!card_can_receive(recipient.state.cards, static_cast<std::uint8_t>(offer.item)))
            return failure("Card stack is full");
        (void)consume_card(source.state.cards, w.card_supply, static_cast<std::uint8_t>(offer.item));
        receive_card(recipient.state.cards, w.card_supply, services::card_prices,
                     static_cast<std::uint8_t>(offer.item));
        break;
    default:
        return failure("Unknown listing type");
    }
    recipient.state.cash = buyer_cash;
    source.state.bank_balance = seller_bank;
    recipient.status.cumulative_spending += static_cast<std::uint32_t>(offer.price);
    source.status.cumulative_income += static_cast<std::uint32_t>(offer.price);
    (void)cancel(w, seller, index);
    return {true, "Purchased", owner_changed};
}

AiResult ai_turn(World &w, Random &random, std::size_t actor) {
    AiResult result;
    if (!active(w, actor))
        return result;
    result.removed = prune(w);
    const auto &player = w.players[actor];
    if (random.next() % 15 == 0) {
        std::vector<std::uint16_t> candidates;
        const auto count = card_count(player.state.cards);
        OfferKind kind = OfferKind::card;
        if (count > 12)
            for (std::size_t i = 0; i < count; ++i)
                for (std::size_t j = 0; j < count; ++j)
                    if (i != j && player.state.cards[i] == player.state.cards[j])
                        candidates.push_back(player.state.cards[i]);
        if (candidates.empty()) {
            kind = OfferKind::tool;
            for (std::uint16_t i = 1; i <= 13; ++i) {
                const auto amount = player.state.tool_amounts[i - 1];
                if (amount >= 3 || (amount && tool_aggression[i - 1] - player.status.rescue_strategy == 2))
                    candidates.push_back(i);
            }
        }
        if (!candidates.empty()) {
            const auto id = candidates[random.next() % candidates.size()];
            if (w.offers[actor].back().kind != OfferKind::empty) {
                (void)cancel(w, actor, 0);
                ++result.removed;
            }
            if (const auto price = inventory_price(w, kind, id);
                price && post(w, actor, {kind, id, *price, 0}).applied)
                ++result.posted;
        }
    }
    if (random.next() % 3 == 0)
        for (auto &offer : w.offers[actor]) {
            if (const auto price = inventory_price(w, offer.kind, offer.item);
                price && *price != offer.price) {
                offer.price = *price;
                ++result.repriced;
            }
        }
    if (random.next() % 4 == 0) {
        for (std::size_t seller = 0; seller < w.players.size() && seller < w.offers.size(); ++seller) {
            if (seller == actor || !active(w, seller))
                continue;
            for (std::size_t index = 0; index < 7; ++index) {
                const auto offer = w.offers[seller][index];
                bool bargain = false;
                if (offer.kind == OfferKind::stock && offer.item < 12 && offer.quantity) {
                    const auto per_share = (E::from(offer.price) / E::from(offer.quantity)).integer();
                    bargain = static_cast<double>(per_share) <
                              std::bit_cast<float>(w.stocks[offer.item].float_bits[2]);
                } else if (offer.kind == OfferKind::property) {
                    if (const auto p = property(w, offer.item))
                        bargain =
                            mul32(3, p->value) > offer.price && mul32(2, offer.price) < player.state.cash;
                }
                if (bargain && buy(w, actor, seller, index).applied) {
                    result.purchased = Quote{seller, index, offer};
                    return result;
                }
            }
        }
    }
    return result;
}
} // namespace rich4::trading
