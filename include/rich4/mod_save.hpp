// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// MOD-only, checksum-bound companion file for quantities wider than the
// original Windows DAT layout. Never reinterpret or extend the DAT structure.
// Losing this file prevents full recovery of high balances, share counts and
// tool counts: backups must include BOTH files.
#include "rich4/mod_rules.hpp"
#include "rich4/world.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rich4::mod {

inline std::uint64_t dat_hash(std::span<const std::uint8_t> bytes) noexcept {
    std::uint64_t hash = UINT64_C(14695981039346656037);
    for (const auto byte : bytes) {
        hash ^= byte;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}
inline std::int32_t legacy_money(std::int64_t amount) noexcept {
    return static_cast<std::int32_t>(std::clamp(amount, std::int64_t(INT32_MIN), std::int64_t(INT32_MAX)));
}
inline std::uint16_t legacy_shares(std::uint64_t amount) noexcept {
    return static_cast<std::uint16_t>(std::min<std::uint64_t>(amount, UINT16_MAX));
}

inline std::string encode_tool_extension(const World &world, std::span<const std::uint8_t> dat) {
    std::ostringstream out;
    out << "RICH4-MOD-ECON-3 " << dat.size() << ' ' << dat_hash(dat) << ' '
        << world.players.size() << ' ' << world.stocks.size() << ' '
        << world.commercials.size() << '\n';
    for (const auto &player : world.players) {
        for (auto count : player.state.tool_amounts) {
            if (count > max_tool_amount)
                throw SaveError("MOD tool count exceeds 999");
            out << count << ' ';
        }
        const auto &state = player.state;
        out << state.cash << ' ' << state.bank_balance << ' ' << state.loan << ' '
            << state.special_finance << ' ';
        for (const auto &holding : player.stocks) {
            if (holding.amount < 0 || holding.amount > max_holding_shares)
                throw SaveError("MOD stock holding count exceeds maximum");
            out << holding.amount << ' ';
        }
        // DAT stores card IDs only; true quantities per distinct card type live
        // in ECON-3. ECON-2 remains readable for pre-stack MOD saves.
        for (unsigned id = 1; id <= 30; ++id) {
            const auto count = card_quantity(state.cards, static_cast<std::uint8_t>(id));
            if (count > max_tool_amount)
                throw SaveError("MOD card stack exceeds 999");
            out << count << ' ';
        }
        out << '\n';
    }
    for (const auto &stock : world.stocks) {
        if (stock.shares_available > max_market_shares || stock.flags_10 > max_market_shares)
            throw SaveError("MOD stock supply exceeds maximum");
        out << stock.shares_available << ' ' << stock.flags_10 << '\n';
    }
    for (const auto &company : world.commercials) {
        if (company.shares_remaining < 0 || company.shares_remaining > max_market_shares)
            throw SaveError("MOD company shares exceed maximum");
        out << company.shares_remaining << ' ';
    }
    out << '\n' << world.public_fund << '\n';
    return out.str();
}

// Load old MOD-only tool sidecars without changing any of the normal financial
// fields. This is useful when upgrading an earlier mod save to the 64-bit ledger.
inline void apply_old_tools(World &world, std::span<const std::uint8_t> dat, std::string_view extension) {
    std::istringstream in{std::string(extension)};
    std::string magic;
    std::uint64_t size = 0, hash = 0;
    std::size_t players = 0;
    if (!(in >> magic >> size >> hash >> players) || magic != "RICH4-MOD-TOOLS-1" ||
        size != dat.size() || hash != dat_hash(dat) || players != world.players.size())
        throw SaveError("Old MOD tool extension is missing or has mismatched checksum");
    std::vector<ToolAmounts> parsed(players);
    for (std::size_t p = 0; p < players; ++p)
        for (std::size_t i = 0; i < parsed[p].size(); ++i) {
            unsigned count = 0;
            if (!(in >> count) || count > max_tool_amount ||
                world.players[p].state.tool_amounts[i] != std::min<unsigned>(count, UINT8_MAX))
                throw SaveError("Old MOD tool extension is invalid");
            parsed[p][i] = static_cast<std::uint16_t>(count);
        }
    std::string extra;
    if (in >> extra)
        throw SaveError("Old MOD tool extension contains unexpected data");
    for (std::size_t p = 0; p < players; ++p)
        world.players[p].state.tool_amounts = parsed[p];
}

inline void apply_tool_extension(World &world, std::span<const std::uint8_t> dat,
                                 std::string_view extension) {
    std::istringstream in{std::string(extension)};
    std::string magic;
    if (!(in >> magic))
        throw SaveError("MOD extension is empty");
    if (magic == "RICH4-MOD-TOOLS-1") {
        apply_old_tools(world, dat, extension);
        return;
    }
    std::uint64_t bytes = 0, hash = 0;
    std::size_t players = 0, stocks = 0, companies = 0;
    const bool card_stacks = magic == "RICH4-MOD-ECON-3";
    if ((magic != "RICH4-MOD-ECON-2" && !card_stacks) ||
        !(in >> bytes >> hash >> players >> stocks >> companies) ||
        bytes != dat.size() || hash != dat_hash(dat) ||
        players != world.players.size() || stocks != world.stocks.size() ||
        companies != world.commercials.size())
        throw SaveError("MOD financial extension is invalid or does not match this DAT");
    // Transactional validation: none of the loaded world is modified unless
    // EVERY extended field AND its saturated DAT preview is valid.
    World candidate = world;
    for (std::size_t p = 0; p < players; ++p) {
        auto &state = candidate.players[p].state;
        const auto &legacy = world.players[p].state;
        for (std::size_t i = 0; i < state.tool_amounts.size(); ++i) {
            unsigned count = 0;
            if (!(in >> count) || count > max_tool_amount ||
                legacy.tool_amounts[i] != std::min<unsigned>(count, UINT8_MAX))
                throw SaveError("MOD sidecar tools disagree with DAT");
            state.tool_amounts[i] = static_cast<std::uint16_t>(count);
        }
        std::int64_t cash = 0, bank = 0, loan = 0, special = 0;
        if (!(in >> cash >> bank >> loan >> special) ||
            legacy.cash != legacy_money(cash) || legacy.bank_balance != legacy_money(bank) ||
            legacy.loan != legacy_money(loan) || legacy.special_finance != legacy_money(special))
            throw SaveError("MOD sidecar bank balances disagree with DAT");
        state.cash = cash;
        state.bank_balance = bank;
        state.loan = loan;
        state.special_finance = special;
        for (std::size_t i = 0; i < candidate.players[p].stocks.size(); ++i) {
            std::int64_t amount = 0;
            if (!(in >> amount) || amount < 0 || amount > max_holding_shares ||
                world.players[p].stocks[i].amount != legacy_money(amount))
                throw SaveError("MOD sidecar share holdings disagree with DAT");
            candidate.players[p].stocks[i].amount = amount;
        }
        if (card_stacks) {
            std::array<unsigned, 30> preview{};
            for (const auto id : legacy.cards) {
                if (id > 30)
                    throw SaveError("MOD DAT preview has invalid card ID");
                if (id)
                    ++preview[id - 1];
            }
            std::array<std::uint16_t, 30> amounts{};
            for (std::size_t i = 0; i < amounts.size(); ++i) {
                unsigned count = 0;
                if (!(in >> count) || count > max_tool_amount ||
                    (preview[i] == 0 && count != 0) ||
                    (preview[i] != 0 && count < preview[i]))
                    throw SaveError("MOD sidecar card counts disagree with DAT");
                amounts[i] = static_cast<std::uint16_t>(count);
            }
            enable_card_stacks(state.cards);
            state.cards.stacked_amounts = amounts;
        }
    }
    for (std::size_t i = 0; i < stocks; ++i) {
        std::uint64_t available = 0, quota = 0;
        if (!(in >> available >> quota) || available > max_market_shares ||
            quota > max_market_shares ||
            world.stocks[i].shares_available != legacy_shares(available) ||
            world.stocks[i].flags_10 != legacy_shares(quota))
            throw SaveError("MOD sidecar market shares disagree with DAT");
        candidate.stocks[i].shares_available = static_cast<std::uint32_t>(available);
        candidate.stocks[i].flags_10 = static_cast<std::uint32_t>(quota);
    }
    for (std::size_t i = 0; i < companies; ++i) {
        std::int64_t remaining = 0;
        if (!(in >> remaining) || remaining < 0 || remaining > max_market_shares ||
            world.commercials[i].shares_remaining != legacy_money(remaining))
            throw SaveError("MOD sidecar company share supply disagrees with DAT");
        candidate.commercials[i].shares_remaining = remaining;
    }
    std::int64_t fund = 0;
    if (!(in >> fund) || world.public_fund != legacy_money(fund))
        throw SaveError("MOD sidecar public fund disagrees with DAT");
    candidate.public_fund = fund;
    std::string extra;
    if (in >> extra)
        throw SaveError("MOD sidecar contains unexpected trailing tokens");
    world = std::move(candidate);
}

} // namespace rich4::mod
