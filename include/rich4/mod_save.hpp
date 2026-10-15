// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// MOD-only companion file for wide tool counts. The Windows DAT reserves
// 15 * 1 byte per player, so never expand or mutate its record layout.
#include "rich4/mod_rules.hpp"
#include "rich4/world.hpp"

#include <algorithm>
#include <cstdint>
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

inline std::string encode_tool_extension(const World &world, std::span<const std::uint8_t> dat) {
    std::ostringstream out;
    out << "RICH4-MOD-TOOLS-1 " << dat.size() << ' ' << dat_hash(dat)
        << ' ' << world.players.size() << '\n';
    for (const auto &player : world.players) {
        for (const auto count : player.state.tool_amounts) {
            if (count > max_tool_amount)
                throw SaveError("MOD tool count exceeds 999");
            out << count << ' ';
        }
        out << '\n';
    }
    return out.str();
}

inline void apply_tool_extension(World &world, std::span<const std::uint8_t> dat,
                                 std::string_view extension) {
    std::istringstream in{std::string(extension)};
    std::string magic;
    std::uint64_t size = 0, hash = 0;
    std::size_t players = 0;
    if (!(in >> magic >> size >> hash >> players) ||
        magic != "RICH4-MOD-TOOLS-1" || size != dat.size() ||
        hash != dat_hash(dat) || players != world.players.size())
        throw SaveError("MOD tool extension missing or mismatched DAT checksum");
    std::vector<ToolAmounts> parsed(players);
    for (std::size_t slot = 0; slot < players; ++slot)
        for (std::size_t i = 0; i < parsed[slot].size(); ++i) {
            unsigned count = 0;
            if (!(in >> count) || count > max_tool_amount)
                throw SaveError("MOD tool extension count is corrupt or above 999");
            if (world.players[slot].state.tool_amounts[i] !=
                std::min<unsigned>(count, UINT8_MAX))
                throw SaveError("MOD tool extension conflicts with original DAT inventory");
            parsed[slot][i] = static_cast<std::uint16_t>(count);
        }
    std::string trailing;
    if (in >> trailing)
        throw SaveError("MOD tool extension contains unexpected trailing data");
    for (std::size_t slot = 0; slot < players; ++slot)
        world.players[slot].state.tool_amounts = parsed[slot];
}

} // namespace rich4::mod
