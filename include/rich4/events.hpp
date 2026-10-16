// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "rich4/random.hpp"
#include "rich4/world.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace rich4 {

struct EventSelection {
    std::uint8_t source_id = 0; // Card in the saved deck, before transport/map remapping.
    std::uint8_t event_id = 0;  // Effective handler: news0..35, fortune0..48.
    std::uint32_t next_cursor = 0;
    std::uint32_t skipped = 0;
};

struct EventResult {
    bool supported = false;
    std::uint8_t event_id = 0;
    std::string message;
    // Arguments of the original news/fortune body text (presentation only; no rule effect).
    std::uint16_t property = 0; // Original property code (2000+land, 4000+facility) named by %s.
    std::int32_t subject = -1;  // Player slot (awards) or stock index (news 27/28) named by %s.
    std::int32_t company = -1;  // Commercial index (news 29..35).
    std::int64_t value = 0;     // %d: days, amount or percent.
    std::uint8_t players = 0;   // Bit mask of player slots shown as portrait rows.
    std::array<std::int64_t, 4> player_amounts{}; // Per-slot tax/bonus rows (news 11..13, 23).
};

// Side effects already owned by the session, plus explicit UI choices. An event
// requiring an absent callback returns supported=false before applying changes.
// The callbacks operate on the SAME World passed to apply_*.
struct EventCallbacks {
    // Original event_strings category after any required random draw. UI must
    // play this result without drawing from the gameplay random stream again.
    std::function<void(std::size_t, std::uint8_t)> voice;
    // Original 0x41d2c6 IDs: players0..3, companies101+, payee-1 public fund.
    // Flags bit0: credit cash, bit2: debit bank first.
    std::function<void(std::int32_t, std::int32_t, std::int64_t, std::uint32_t)> pay;
    std::function<void(std::size_t, std::uint8_t)> add_prison;
    std::function<void(std::size_t, std::uint8_t)> add_hospital;
    std::function<void(std::size_t, std::uint8_t, std::uint8_t)> disappear;
    std::function<std::int32_t(std::size_t)> try_mianzui_jiahuo; // Target slot, or -1 canceled.
    std::function<void(std::size_t)> set_loan_due_date;
    // Final flag: true credits original bank account, false sends proceeds to public fund.
    std::function<void(std::size_t, std::size_t, std::int32_t, bool)> sell_stock;
    std::function<void(std::uint16_t)> auction; // Original property code, seller-1/mode1.
    std::function<std::uint8_t(std::size_t, std::size_t)>
        choose_card; // Actor,victim -> card ID; zero cancels this victim.
    // Exact original square pick-buffer selection; center is a property anchor.
    // Mode1 confiscates, mode0 downgrades; actors=true also applies hospital/objects.
    std::function<void(std::uint16_t, std::uint16_t, std::uint8_t, bool)> area_damage;
    // Original 0x429040: 0 updates all stocks, otherwise one-based stock ID.
    std::function<void(std::uint32_t)> update_stocks;
    // Presentation only: news 4/5/15/20/21 play their board movie on this property code right
    // after the damage, before the owner's voice (rich4_news.asm). Must not draw randomness.
    std::function<void(std::uint8_t, std::uint16_t)> news_picture;
};

[[nodiscard]] bool news_eligible(const World &world, std::uint8_t id);
[[nodiscard]] std::optional<std::uint8_t> fortune_eligible(const World &world, std::size_t actor,
                                                           std::uint8_t id);
[[nodiscard]] EventSelection select_news(const World &world);
[[nodiscard]] EventSelection select_fortune(const World &world, std::size_t actor);
[[nodiscard]] EventResult apply_news(World &world, Random &random, const EventCallbacks &callbacks = {});
[[nodiscard]] EventResult apply_fortune(World &world, Random &random, std::size_t actor,
                                        const EventCallbacks &callbacks = {});
// Direct effective IDs for original-oracle tests and explicit replay. These do
// not advance the saved deck or silently substitute a different event.
[[nodiscard]] EventResult apply_news_id(World &world, Random &random, std::uint8_t id,
                                        const EventCallbacks &callbacks = {});
[[nodiscard]] EventResult apply_fortune_id(World &world, Random &random, std::size_t actor, std::uint8_t id,
                                           const EventCallbacks &callbacks = {});

} // namespace rich4
