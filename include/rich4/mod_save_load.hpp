// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Shared, strict DAT + MOD companion validation for load dialogs and gameplay.
// No fallback to a saturated DAT preview is allowed after a MOD sidecar is lost.
#include "rich4/mod_save.hpp"

#include <optional>
#include <string_view>

namespace rich4::mod {

inline World decode_checked_mod_save(std::span<const std::uint8_t> dat,
                                     std::optional<std::string_view> extension = std::nullopt) {
    World loaded = World::from_save(SaveFile::decode(dat));
    if (extension) {
        apply_tool_extension(loaded, dat, *extension);
    } else {
        // Before companion-required footers existed, large MOD supply could be
        // detected only through its impossible-for-vanilla saturated preview.
        // Keep genuine, low-value original DAT files importable.
        const bool missing_wide_ledger =
            std::any_of(loaded.stocks.begin(), loaded.stocks.end(), [](const auto &stock) {
                return stock.shares_available == UINT16_MAX || stock.flags_10 == UINT16_MAX;
            }) ||
            std::any_of(loaded.players.begin(), loaded.players.end(), [](const auto &player) {
                return std::any_of(player.state.tool_amounts.begin(), player.state.tool_amounts.end(),
                                   [](auto count) { return count > 9; });
            });
        if (missing_wide_ledger || requires_mod_sidecar(dat))
            throw SaveError("MOD DAT requires its matching .DAT.mod financial companion");
    }
    return loaded;
}
} // namespace rich4::mod
