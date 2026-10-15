// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "rich4/inventory.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace rich4 {

class SaveError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

struct SaveDate {
    std::uint8_t day;
    std::uint8_t month;
    std::uint16_t year;
};

struct SaveHeader {
    std::uint32_t signature;
    SaveDate date;
    std::uint16_t map_id;
    std::uint16_t stage_id;
    std::uint32_t player_count; // Includes bankrupt players; slots are zero based.
    std::uint32_t human_player_count;
    std::uint32_t current_player;
    std::int32_t initial_fund;
    std::int32_t price_index;
};

struct PlayerState {
    std::size_t slot; // The original game identifies players by their array index.
    std::uint32_t color;
    std::uint16_t x;
    std::uint16_t y;
    std::uint16_t node_id;
    std::uint16_t last_node_id;
    std::uint8_t direction;
    std::uint8_t traffic_method;
    std::uint8_t dice_count;
    std::uint8_t character;
    std::uint8_t sex;
    std::uint8_t controller_flags; // Low 2 bits: 0 inactive, 1 human, 2 computer.
    std::int32_t cash;
    std::int32_t bank_balance; // Includes special financing in the original format.
    std::int32_t loan;
    std::int32_t special_finance;
    std::uint16_t points;
    std::array<std::uint8_t, 15> cards;        // Card IDs in inventory slot order.
    ToolAmounts tool_amounts; // MOD: 16-bit counts, legacy DAT serializes only bytes.
};

struct MapTableSummary {
    std::uint32_t count;       // Excludes the record at index zero.
    std::uint32_t byte_offset; // Relative to the map block, never a host pointer.
    std::uint32_t record_bytes;
};

struct MapSummary {
    std::uint32_t byte_size;
    MapTableSummary nodes;
    MapTableSummary lands;
    MapTableSummary facilities;
    MapTableSummary commercials;
    MapTableSummary raw_reserved_table_4; // Original ref_00498e78, semantics unresolved.
};

enum class SaveRegionKind { decoded, partially_decoded, raw_reserved };

struct SaveRegion {
    std::string name;
    std::size_t offset;
    std::size_t byte_size;
    SaveRegionKind kind;
};

// Lossless codec for the original 0x26 SAVE*.DAT layout. This does not yet restore
// a running game's state, interpret every reserved field, or migrate its rules.
// No serialized address is ever interpreted as a host pointer.
class SaveFile {
  public:
    static SaveFile decode(std::span<const std::uint8_t> bytes);

    [[nodiscard]] SaveHeader header() const;
    [[nodiscard]] PlayerState player(std::size_t slot) const;
    [[nodiscard]] MapSummary map_summary() const;
    [[nodiscard]] std::span<const SaveRegion> regions() const noexcept;
    [[nodiscard]] std::vector<std::uint8_t> encode() const;

    // Modify exactly one signed, little-endian 32-bit field. These deliberately
    // do not adjust historical per-player snapshots or implement gameplay rules.
    void set_cash(std::size_t slot, std::int32_t value);
    void set_bank_balance(std::size_t slot, std::int32_t value);

  private:
    SaveFile(std::vector<std::uint8_t> bytes, std::vector<SaveRegion> regions);
    [[nodiscard]] std::size_t player_offset(std::size_t slot) const;

    std::vector<std::uint8_t> bytes_;
    std::vector<SaveRegion> regions_;
};

} // namespace rich4
