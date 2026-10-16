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

void enable_card_stacks(CardSlots &cards) {
    if (cards.stacked)
        return;
    validate_cards(cards);
    std::array<std::uint8_t, 15> unique{};
    std::array<std::uint16_t, 30> amounts{};
    std::size_t length = 0;
    for (const auto id : cards) {
        if (!id)
            continue;
        if (!amounts[id - 1])
            unique[length++] = id;
        ++amounts[id - 1];
    }
    std::copy(unique.begin(), unique.end(), cards.begin());
    cards.stacked_amounts = amounts;
    cards.stacked = true;
}

std::uint16_t card_quantity(const CardSlots &cards, std::uint8_t card_id) noexcept {
    if (!card_id || card_id > 30)
        return 0;
    if (cards.stacked)
        return cards.stacked_amounts[card_id - 1];
    return static_cast<std::uint16_t>(std::count(cards.begin(), cards.end(), card_id));
}

std::size_t card_total_quantity(const CardSlots &cards) noexcept {
    if (!cards.stacked)
        return card_count(cards);
    std::size_t total = 0;
    for (const auto count : cards.stacked_amounts)
        total += count;
    return total;
}

bool card_can_receive(const CardSlots &cards, std::uint8_t card_id) noexcept {
    if (!card_id || card_id > 30)
        return false;
    if (!cards.stacked)
        return card_count(cards) < cards.size();
    const auto count = card_quantity(cards, card_id);
    return count < mod::max_tool_amount && (count || card_count(cards) < cards.size());
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
    if (cards.stacked) {
        auto &amount = cards.stacked_amounts[index];
        if (!amount)
            throw std::invalid_argument("MOD card stack is inconsistent");
        --amount;
        if (amount) {
            supply[index] = increment_byte(supply[index]);
            return true;
        }
    }
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
    if (cards.stacked) {
        auto &amount = cards.stacked_amounts[index];
        if (amount >= mod::max_tool_amount)
            return; // Grant at 999 changes neither world supply nor the hand.
        if (amount) {
            ++amount;
            --supply[index]; // Original global card supply is an 8-bit counter.
            return;
        }
        if (card_count(cards) == cards.size()) {
            const auto discarded = cheapest_card(cards, prices);
            const auto old = cards.stacked_amounts[discarded - 1];
            const auto found = std::find(cards.begin(), cards.end(), discarded);
            std::move(found + 1, cards.end(), found);
            cards.back() = 0;
            cards.stacked_amounts[discarded - 1] = 0;
            supply[discarded - 1] = static_cast<std::uint8_t>(supply[discarded - 1] + old);
        }
        *std::find(cards.begin(), cards.end(), 0) = card_id;
        amount = 1;
        --supply[index];
        return;
    }
    if (card_count(cards) == cards.size()) {
        consume_card(cards, supply, cheapest_card(cards, prices));
    }
    *std::find(cards.begin(), cards.end(), 0) = card_id;
    supply[index] = static_cast<std::uint8_t>(static_cast<unsigned>(supply[index]) - 1);
}

bool drop_half_cards(CardSlots &cards, CardSupply &supply) {
    validate_cards(cards, true);
    if (cards.stacked) {
        const auto total = card_total_quantity(cards);
        if (total <= 1)
            return false;
        // Drop half of the individual cards, rotating across occupied types
        // rather than destroying an entire 999-card stack in one draw.
        for (std::size_t n = 0; n < total / 2; ++n) {
            const auto present = card_count(cards);
            const auto id = cards[n % present];
            (void)consume_card(cards, supply, id);
        }
        return true;
    }
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
    if (cards.stacked) {
        for (auto &card : cards) {
            if (!card)
                continue;
            const auto index = card_index(card);
            const auto amount = cards.stacked_amounts[index];
            points += std::uint32_t(prices[index]) * amount;
            supply[index] = static_cast<std::uint8_t>(supply[index] + amount);
            cards.stacked_amounts[index] = 0;
            card = 0;
        }
        return points;
    }
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
