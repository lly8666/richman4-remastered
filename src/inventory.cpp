// SPDX-License-Identifier: GPL-3.0-or-later
// Reconstructed from original Rich4 card/tool routines; see migration 005.
#include "rich4/inventory.hpp"
#include "rich4/mod_rules.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace rich4 {
namespace {

std::size_t card_index(std::uint8_t id) {
    if (id < 1 || id > 30)
        throw std::invalid_argument("Card ID must be in 1..30");
    return static_cast<std::size_t>(id) - 1;
}

std::size_t tool_index(std::uint8_t id) {
    if (id < 1 || id > 13)
        throw std::invalid_argument("Tool ID must be in 1..13");
    return static_cast<std::size_t>(id) - 1;
}

void validate_cards(const CardSlots &cards, bool require_packed = false) {
    bool saw_empty = false;
    for (const auto card : cards) {
        if (card == 0) {
            saw_empty = true;
        } else {
            (void)card_index(card);
            if (require_packed && saw_empty) {
                throw std::invalid_argument("Dropping half requires a packed card hand");
            }
        }
    }
}

std::uint8_t increment_byte(std::uint8_t value) noexcept {
    return static_cast<std::uint8_t>(static_cast<unsigned>(value) + 1);
}

} // namespace

std::size_t card_count(const CardSlots &cards) noexcept {
    return static_cast<std::size_t>(
        std::count_if(cards.begin(), cards.end(), [](auto card) { return card != 0; }));
}

bool has_card(const CardSlots &cards, std::uint8_t card_id) noexcept {
    return std::find(cards.begin(), cards.end(), card_id) != cards.end();
}

std::uint8_t cheapest_card(const CardSlots &cards, const CardPrices &prices) {
    validate_cards(cards);
    unsigned lowest_price = 10000; // Original sentinel, larger than any byte price.
    std::uint8_t cheapest = 0;
    for (const auto card : cards) {
        if (card != 0 && prices[card_index(card)] < lowest_price) {
            lowest_price = prices[card_index(card)];
            cheapest = card;
        }
    }
    return cheapest;
}

bool consume_card(CardSlots &cards, CardSupply &supply, std::uint8_t card_id) {
    const auto index = card_index(card_id);
    const auto found = std::find(cards.begin(), cards.end(), card_id);
    if (found == cards.end())
        return false;
    const auto slot = static_cast<std::size_t>(found - cards.begin());
    // Original forward memcpy moved overlapping bytes left. memmove expresses
    // the observed behavior without relying on undefined C/C++ memcpy semantics.
    std::memmove(cards.data() + slot, cards.data() + slot + 1, cards.size() - slot - 1);
    cards.back() = 0;
    supply[index] = increment_byte(supply[index]);
    return true;
}

void receive_card(CardSlots &cards, CardSupply &supply, const CardPrices &prices, std::uint8_t card_id) {
    const auto index = card_index(card_id);
    validate_cards(cards);
    if (card_count(cards) == cards.size()) {
        consume_card(cards, supply, cheapest_card(cards, prices));
    }
    *std::find(cards.begin(), cards.end(), 0) = card_id;
    supply[index] = static_cast<std::uint8_t>(static_cast<unsigned>(supply[index]) - 1);
}

bool drop_half_cards(CardSlots &cards, CardSupply &supply) {
    validate_cards(cards, true);
    const auto count = card_count(cards);
    if (count <= 1)
        return false;
    // Read the current slot each iteration: [A,B,C,D] drops A, then C. Repeated
    // IDs still consume the first matching ID, which may precede this slot.
    for (std::size_t slot = 0; slot < count / 2; ++slot) {
        consume_card(cards, supply, cards[slot]);
    }
    return true;
}

std::uint32_t sell_all_cards(CardSlots &cards, CardSupply &supply, const CardPrices &prices) {
    validate_cards(cards);
    std::uint32_t points = 0;
    for (auto &card : cards) {
        if (card == 0)
            continue;
        const auto index = card_index(card);
        supply[index] = increment_byte(supply[index]);
        points += prices[index];
        card = 0;
    }
    return points;
}

bool receive_tool(ToolAmounts &amounts, ToolSupply &supply, std::uint8_t tool_id) {
    const auto index = tool_index(tool_id);
    if (amounts[index] >= mod::max_tool_amount)
        return false;
    if (index < supply.size()) {
        if (supply[index] == 0)
            return false;
        --supply[index];
    }
    ++amounts[index];
    return true;
}

bool consume_tool(ToolAmounts &amounts, ToolSupply &supply, std::uint8_t tool_id) {
    const auto index = tool_index(tool_id);
    if (amounts[index] == 0)
        return false;
    --amounts[index];
    if (index < supply.size())
        supply[index] = increment_byte(supply[index]);
    return true;
}

std::uint32_t sell_stored_tools(ToolAmounts &amounts, ToolSupply &supply, const ToolPrices &prices) noexcept {
    std::uint32_t points = 0;
    for (std::size_t index = 0; index < prices.size(); ++index) {
        if (index < supply.size()) {
            supply[index] = static_cast<std::uint8_t>(static_cast<unsigned>(supply[index]) + amounts[index]);
        }
        points += static_cast<std::uint32_t>(prices[index]) * amounts[index];
        amounts[index] = 0;
    }
    return points;
}

} // namespace rich4
