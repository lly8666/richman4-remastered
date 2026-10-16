// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/random.hpp"
#include "rich4/trading.hpp"

#include <algorithm>
#include <bit>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
using namespace rich4;
using namespace rich4::trading;
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
World fixture() {
    World w;
    w.header.price_index = 2;
    w.players.resize(4);
    w.lands.resize(8);
    w.facilities.resize(4);
    for (std::size_t i = 0; i < 4; ++i) {
        auto &p = w.players[i];
        p.state.controller_flags = 2;
        p.state.cash = 100000;
        p.state.bank_balance = 10000;
        p.state.cards = {1, 3, 5, 7, 9};
        p.state.tool_amounts = {1, 2, 3, 1, 2, 3, 1, 2, 3, 1, 2, 3, 1};
        for (std::size_t s = 0; s < 12; ++s)
            p.stocks[s] = {static_cast<std::int32_t>(100 + i), std::bit_cast<std::uint32_t>(12.375F)};
    }
    for (std::size_t i = 1; i < 8; ++i) {
        auto &p = w.lands[i];
        p.economy.owner_code = 1;
        p.economy.level = 2;
        p.land_price = 1000;
        p.price_per_level = 500;
        p.expired_date = 0x07eb0101;
    }
    for (std::size_t i = 1; i < 4; ++i) {
        auto &p = w.facilities[i];
        p.owner_code = 1;
        p.facility_type = 2;
        p.level = 3;
        p.land_price = 2000;
        p.price_per_level() = 700;
        p.expired_date = 0x07eb0101;
    }
    for (auto &p : w.stocks) {
        p.float_bits[2] = std::bit_cast<std::uint32_t>(20.25F);
        p.shares_available = 777;
    }
    w.card_supply.fill(3);
    w.tool_supply.fill(10);
    return w;
}
void listing_and_cleanup() {
    auto w = fixture();
    const auto limits = listing_limits(w, 0, OfferKind::stock, 2, 99);
    require(limits && limits->reference_price == 2004 && limits->maximum_price == 20040,
            "Stock reference truncates after extended multiply");
    require(listing_limits(w, 0, OfferKind::property, 2001)->reference_price == 4000,
            "Land listing includes every building level and price index");
    require(listing_limits(w, 0, OfferKind::property, 4001)->reference_price == 8200,
            "Facility listing uses its own construction cost");
    require(!listing_limits(w, 1, OfferKind::property, 4001) &&
                !listing_limits(w, 0, OfferKind::stock, 0, 101),
            "Reject unavailable assets");
    const auto hand = w.players[0].state.cards;
    w.offers[0][0].quantity = 4321;
    w.offers[0][0].property_type = 77;
    w.offers[0][0].property_level = 88;
    require(post(w, 0, {OfferKind::card, 3, 111, 0}).applied, "Post owned card");
    require(w.players[0].state.cards == hand && w.offers[0][0].quantity == 4321 &&
                w.offers[0][0].property_type == 77,
            "Posting reserves nothing and preserves unused record bytes");
    require(post(w, 0, {OfferKind::card, 3, 222, 0}).applied && quotes(w).size() == 1,
            "Replacing same item does not consume another slot");
    for (std::uint16_t i = 1; i <= 6; ++i)
        require(post(w, 0, {OfferKind::property, static_cast<std::uint16_t>(2000 + i), 100, 0}).applied,
                "Fill seven slots");
    require(!post(w, 0, {OfferKind::property, 2007, 100, 0}).applied,
            "Human cannot replace oldest full-list offer implicitly");
    require(cancel(w, 0, 3) && w.offers[0][3].item == 2004 && w.offers[0].back() == TradeOffer{},
            "Cancel compacts complete records and clears final slot");
    w = fixture();
    require(post(w, 0, {OfferKind::tool, 1, 100, 0}).applied &&
                post(w, 0, {OfferKind::tool, 2, 100, 0}).applied &&
                post(w, 0, {OfferKind::tool, 3, 100, 0}).applied,
            "Prepare cleanup sequence");
    w.players[0].state.tool_amounts[0] = 0;
    w.players[0].state.tool_amounts[2] = 0;
    require(prune(w) == 1 && w.offers[0][0].item == 2 && w.offers[0][1].item == 3,
            "Original deleted flag freezes scan on shifted valid entry");
    require(quotes(w).size() == 1 && quotes(w)[0].index == 0,
            "Stale offers are not exposed as purchasable quotes");
    require(prune(w) == 1 && w.offers[0][1].kind == OfferKind::empty,
            "Next entry removes the later stale offer");
    require(post(w, 0, {OfferKind::property, 2001, 100, 0}).applied, "Prepare building snapshot");
    ++w.lands[1].economy.level;
    require(!buy(w, 1, 0, 1).applied && prune(w) == 1,
            "Changing building level invalidates its old price offer");
}
void mod_wide_stock_offer() {
    auto w = fixture();
    w.players[0].state.bank_balance = 3'000'000'000LL;
    w.players[1].state.cash = 900'000'000LL;
    w.players[1].stocks[2] = {500'000'000, std::bit_cast<std::uint32_t>(20.25F)};
    w.players[0].stocks[2] = {1'000'000, std::bit_cast<std::uint32_t>(20.25F)};
    require(post(w, 0, {OfferKind::stock, 2, 10000, 1000}).applied,
            "MOD high-balance peer stock offer accepted");
    require(buy(w, 1, 0, 0).applied &&
                w.players[0].state.bank_balance == 3'000'010'000LL &&
                w.players[1].state.cash == 899'990'000LL &&
                w.players[1].stocks[2].amount == 500'001'000LL &&
                std::bit_cast<float>(w.players[1].stocks[2].raw_cost_basis) > 0,
            "MOD peer stock sale preserves int64 bank and high-quantity basis");
    w = fixture();
    w.players[0].state.bank_balance = INT64_MAX - 5;
    w.players[0].stocks[2].amount = 2000; // Valid 1000-share listing before testing recipient overflow.
    require(post(w, 0, {OfferKind::stock, 2, 10000, 1000}).applied,
            "overflow regression offer posted");
    const auto offer = w.offers[0][0];
    const auto buyer_cash = w.players[1].state.cash;
    const auto stock_before = w.players[0].stocks[2].amount;
    require(!buy(w, 1, 0, 0).applied &&
                w.players[0].state.bank_balance == INT64_MAX - 5 &&
                w.players[1].state.cash == buyer_cash &&
                w.players[0].stocks[2].amount == stock_before &&
                w.offers[0][0] == offer,
            "seller bank overflow rejects peer stock trade atomically");
}
void transfer_effects() {
    auto w = fixture();
    require(post(w, 0, {OfferKind::card, 3, 1234, 0}).applied && buy(w, 1, 0, 0).applied,
            "Card transaction succeeds");
    require(w.players[1].state.cash == 98766 && w.players[0].state.bank_balance == 11234 &&
                w.players[0].state.cash == 100000,
            "Buyer cash funds seller bank");
    require(w.players[1].status.cumulative_spending == 1234 && w.players[0].status.cumulative_income == 1234,
            "Transfer records both transaction totals");
    require(w.players[0].state.cards == CardSlots{1, 5, 7, 9} && w.players[1].state.cards[5] == 3 &&
                w.card_supply[2] == 3,
            "Card removal order, append and neutral supply");
    w = fixture();
    require(post(w, 0, {OfferKind::tool, 1, 1234, 0}).applied, "Tool quote");
    w.players[1].state.cash = 100;
    w.players[1].state.bank_balance = 100000;
    require(!buy(w, 1, 0, 0).applied && w.players[0].state.tool_amounts[0] == 1,
            "Bank balance cannot fund market purchase");
    w.players[1].state.cash = 100000;
    w.players[1].state.tool_amounts[0] = 999;
    require(!buy(w, 1, 0, 0).applied && w.players[0].state.tool_amounts[0] == 1,
            "MOD 999-tool cap refuses without consuming seller item");
    w.players[1].state.tool_amounts[0] = 998;
    require(buy(w, 1, 0, 0).applied && w.players[1].state.tool_amounts[0] == 999 && w.tool_supply[0] == 10,
            "MOD player tool transfer reaches 999 and leaves supply unchanged");
    w = fixture();
    require(post(w, 0, {OfferKind::property, 2001, 1234, 0}).applied && buy(w, 1, 0, 0).applied,
            "Property transaction succeeds");
    require(w.lands[1].economy.owner_code == 2 && w.lands[1].expired_date == 0x07eb0101 &&
                w.lands[1].economy.level == 2,
            "Private transfer preserves lease and building");
    w = fixture();
    require(post(w, 0, {OfferKind::stock, 2, 1001, 100}).applied && buy(w, 1, 0, 0).applied,
            "Stock transaction succeeds");
    require(w.players[0].stocks[2].amount == 0 && w.players[0].stocks[2].raw_cost_basis == 0 &&
                w.players[1].stocks[2].amount == 201,
            "Shares transfer, exhausted seller basis resets");
    require(w.players[1].stocks[2].raw_cost_basis == std::bit_cast<std::uint32_t>(2250.F / 201.F),
            "Existing stock cost truncates before weighted average");
    require(w.stocks[2].shares_available == 777 &&
                w.stocks[2].float_bits[2] == std::bit_cast<std::uint32_t>(20.25F),
            "Private purchase does not move exchange price or quota");
    w = fixture();
    w.commercials.resize(2);
    w.stocks[2].flags_4 = 1;
    w.commercials[1].owner_code = 1;
    w.commercials[1].shareholder_order = {1, 2, 3, 4};
    w.players[0].stocks[2].amount = 400;
    w.players[1].stocks[2].amount = 100;
    w.players[2].stocks[2].amount = 0;
    w.players[3].stocks[2].amount = 0;
    require(post(w, 0, {OfferKind::stock, 2, 1000, 200}).applied &&
                buy(w, 1, 0, 0).commercial_owner_changed && w.commercials[1].owner_code == 2,
            "Acquiring shares recomputes commercial control");
}
std::uint32_t seed_for_purchase() {
    for (std::uint32_t seed = 0; seed < 100000; ++seed) {
        Random r(seed);
        if (r.next() % 15 != 0) {
            (void)r.next();
            if (r.next() % 4 == 0)
                return seed;
        }
    }
    throw std::runtime_error("No purchase seed");
}
void ai_rules() {
    auto w = fixture();
    for (auto &p : w.players) {
        p.state.cards = {};
        p.state.tool_amounts = {};
    }
    Random r(7), expected(7);
    for (unsigned i = 0; i < 3; ++i)
        (void)expected.next();
    (void)ai_turn(w, r, 0);
    require(r.state() == expected.state(), "Empty AI market still consumes exactly three gate draws");
    const auto seed = seed_for_purchase();
    w = fixture();
    require(post(w, 0, {OfferKind::property, 2001, 12000, 0}).applied, "Property at three times reference");
    r.seed(seed);
    require(!ai_turn(w, r, 1).purchased, "Three times reference equality is excluded");
    require(post(w, 0, {OfferKind::property, 2001, 11999, 0}).applied, "Price below three times reference");
    w.players[1].state.cash = 23998;
    r.seed(seed);
    require(!ai_turn(w, r, 1).purchased, "Twice asking price cash equality is excluded");
    w.players[1].state.cash = 23999;
    r.seed(seed);
    require(ai_turn(w, r, 1).purchased.has_value(), "Both strict property thresholds permit purchase");
    w = fixture();
    require(post(w, 0, {OfferKind::card, 1, 1, 0}).applied && post(w, 0, {OfferKind::tool, 1, 1, 0}).applied,
            "Cheap inventory quotes");
    r.seed(seed);
    require(!ai_turn(w, r, 1).purchased, "Original AI never purchases other players' cards or tools");
    require(post(w, 0, {OfferKind::stock, 0, 2020, 100}).applied, "Stock price rounds to twenty per share");
    r.seed(seed);
    require(ai_turn(w, r, 1).purchased->index == 2,
            "AI compares truncated per-share asking price against market float");
    bool posted = false;
    for (std::uint32_t seed2 = 0; seed2 < 200 && !posted; ++seed2) {
        w = fixture();
        w.players[0].state.cards = {1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
        r.seed(seed2);
        const auto result = ai_turn(w, r, 0);
        if (result.posted) {
            posted = true;
            require(w.offers[0][0].kind == OfferKind::card && w.offers[0][0].item == 1 &&
                        w.offers[0][0].price == 40000,
                    "Duplicate card is offered before any surplus tool");
        }
    }
    require(posted, "Posting gate exercised");
}

// The optional executable oracle consumes this deterministic, synthetic state.
// Its format includes every field these routines can change, in original byte order.
std::string signature(const World &w, std::uint32_t random) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    auto bytes = [&](std::uint32_t value, unsigned count) {
        for (unsigned i = 0; i < count; ++i)
            out << std::setw(2) << ((value >> (i * 8)) & 255U);
    };
    for (const auto &row : w.offers)
        for (const auto &p : row) {
            bytes(static_cast<unsigned>(p.kind), 1);
            bytes(p.flags, 1);
            bytes(p.item, 2);
            bytes(static_cast<std::uint32_t>(p.price), 4);
            bytes(p.quantity, 2);
            bytes(p.property_type, 1);
            bytes(p.property_level, 1);
        }
    for (const auto &p : w.players) {
        bytes(static_cast<std::uint32_t>(p.state.cash), 4);
        bytes(static_cast<std::uint32_t>(p.state.bank_balance), 4);
        bytes(p.status.cumulative_spending, 4);
        bytes(p.status.cumulative_income, 4);
        for (auto c : p.state.cards)
            bytes(c, 1);
        for (auto t : p.state.tool_amounts)
            bytes(t, 1);
        for (const auto &s : p.stocks) {
            bytes(static_cast<std::uint32_t>(s.amount), 4);
            bytes(s.raw_cost_basis, 4);
        }
    }
    for (auto c : w.card_supply)
        bytes(c, 1);
    for (auto t : w.tool_supply)
        bytes(t, 1);
    for (std::size_t i = 1; i < w.lands.size(); ++i)
        bytes(w.lands[i].economy.owner_code, 1);
    for (std::size_t i = 1; i < w.facilities.size(); ++i)
        bytes(w.facilities[i].owner_code, 1);
    bytes(random, 4);
    return out.str();
}
void oracle_case(unsigned kind, std::uint32_t seed) {
    auto w = fixture();
    Random r(seed);
    switch (kind) {
    case 0:
        w.offers[0][0].quantity = 4321;
        w.offers[0][0].property_type = 77;
        w.offers[0][0].property_level = 88;
        (void)post(w, 0, {OfferKind::card, 3, 111, 0});
        break;
    case 1:
        (void)post(w, 0, {OfferKind::property, 2001, 1000, 0});
        break;
    case 2:
        (void)post(w, 0, {OfferKind::property, 4001, 1000, 0});
        break;
    case 3:
        (void)post(w, 0, {OfferKind::stock, 2, 1000, 99});
        break;
    case 4:
        for (std::uint16_t i = 1; i <= 7; ++i)
            (void)post(w, 0, {OfferKind::property, static_cast<std::uint16_t>(2000 + i), 100, 0});
        (void)post(w, 0, {OfferKind::property, 2004, 200, 0});
        break;
    case 5:
        for (std::uint16_t i = 1; i <= 7; ++i)
            (void)post(w, 0, {OfferKind::property, static_cast<std::uint16_t>(2000 + i), 100, 0});
        (void)cancel(w, 0, 3);
        break;
    case 6:
        for (std::uint16_t i = 1; i <= 3; ++i)
            (void)post(w, 0, {OfferKind::tool, i, 100, 0});
        w.players[0].state.tool_amounts[0] = 0;
        w.players[0].state.tool_amounts[2] = 0;
        (void)prune(w);
        break;
    case 7:
        (void)post(w, 0, {OfferKind::property, 2001, 100, 0});
        (void)post(w, 0, {OfferKind::property, 4001, 100, 0});
        w.lands[1].economy.level = 3;
        w.facilities[1].facility_type = 1;
        (void)prune(w);
        break;
    case 8:
        (void)post(w, 0, {OfferKind::card, 3, 1234, 0});
        (void)buy(w, 1, 0, 0);
        break;
    case 9:
        (void)post(w, 0, {OfferKind::tool, 1, 1234, 0});
        (void)buy(w, 1, 0, 0);
        break;
    case 10:
        (void)post(w, 0, {OfferKind::stock, 2, 1001, 100});
        (void)buy(w, 1, 0, 0);
        break;
    case 11:
        (void)post(w, 0, {OfferKind::property, 2001, 1234, 0});
        (void)buy(w, 1, 0, 0);
        break;
    case 12:
        (void)post(w, 0, {OfferKind::property, 4001, 1234, 0});
        (void)buy(w, 1, 0, 0);
        break;
    case 13:
        (void)post(w, 0, {OfferKind::tool, 1, 1234, 0});
        w.players[1].state.tool_amounts[0] = 9;
        (void)buy(w, 1, 0, 0);
        break;
    case 14:
        for (auto &p : w.players) {
            p.state.cards = {};
            p.state.tool_amounts = {};
        }
        (void)ai_turn(w, r, 0);
        break;
    case 15:
        (void)ai_turn(w, r, 0);
        break;
    case 16:
        w.players[0].state.cards = {1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
        (void)ai_turn(w, r, 0);
        break;
    case 17:
        (void)post(w, 0, {OfferKind::stock, 2, 1001, 100});
        (void)post(w, 0, {OfferKind::property, 2001, 11000, 0});
        (void)ai_turn(w, r, 1);
        break;
    default:
        throw std::runtime_error("Unknown oracle case");
    }
    std::cout << kind << ' ' << seed << ' ' << signature(w, r.state()) << '\n';
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--oracle") {
            for (unsigned i = 0; i < 18; ++i)
                for (std::uint32_t seed = 0; seed < (i < 14 ? 1U : 200U); ++seed)
                    oracle_case(i, seed);
            return 0;
        }
        listing_and_cleanup();
        transfer_effects();
        mod_wide_stock_offer();
        ai_rules();
        std::cout << "trading tests passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
