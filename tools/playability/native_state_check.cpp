// SPDX-License-Identifier: GPL-3.0-or-later
// Read-only bridge from original-game save captures into the migrated core.
// A successful readback does not mean a native game loop exists or was tested.
#include "rich4/economy.hpp"
#include "rich4/inventory.hpp"
#include "rich4/save.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using Bytes = std::vector<std::uint8_t>;

std::string quoted(std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    std::string result = "\"";
    for (const char raw : value) {
        const auto byte = static_cast<unsigned char>(raw);
        if (byte == '\"' || byte == '\\') {
            result.push_back('\\');
            result.push_back(static_cast<char>(byte));
        } else if (byte < 0x20) {
            result += "\\u00";
            result.push_back(hex[byte >> 4]);
            result.push_back(hex[byte & 15]);
        } else {
            result.push_back(static_cast<char>(byte));
        }
    }
    result.push_back('\"');
    return result;
}

std::string hex_bytes(std::string_view bytes) {
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (const char raw : bytes) {
        const auto byte = static_cast<unsigned char>(raw);
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 15]);
    }
    return result;
}

template <typename T, std::size_t Size>
void write_array(std::ostream &output, const std::array<T, Size> &values) {
    output << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index)
            output << ',';
        output << static_cast<unsigned>(values[index]);
    }
    output << ']';
}

struct Snapshot {
    std::filesystem::path path;
    Bytes bytes;
    rich4::SaveFile save;
    std::vector<rich4::LandEconomyRecord> lands;
};

Snapshot load_snapshot(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot open capture: " + path.string());
    Bytes bytes{std::istreambuf_iterator<char>(input), {}};
    if (input.bad())
        throw std::runtime_error("Cannot read capture: " + path.string());
    auto save = rich4::SaveFile::decode(bytes);
    if (save.encode() != bytes)
        throw std::runtime_error("Native save round trip differs: " + path.string());
    const auto regions = save.regions();
    const auto map_region = std::find_if(regions.begin(), regions.end(),
                                         [](const auto &region) { return region.name == "map_header"; });
    if (map_region == regions.end())
        throw std::runtime_error("Decoded save has no map header region");
    const auto table = save.map_summary().lands;
    if (table.record_bytes != 52)
        throw std::runtime_error("Unsupported land record layout");
    std::vector<rich4::LandEconomyRecord> lands;
    lands.reserve(table.count);
    for (std::uint32_t index = 1; index <= table.count; ++index) {
        const auto offset =
            map_region->offset + table.byte_offset + static_cast<std::size_t>(index) * table.record_bytes;
        lands.push_back(
            rich4::decode_land_economy_record(std::span(bytes).subspan(offset, table.record_bytes)));
    }
    return {std::filesystem::absolute(path), std::move(bytes), std::move(save), std::move(lands)};
}

void write_player(std::ostream &output, const rich4::PlayerState &player) {
    output << "{\"slot\":" << player.slot << ",\"character\":" << static_cast<unsigned>(player.character)
           << ",\"controller\":" << static_cast<unsigned>(player.controller_flags)
           << ",\"cash\":" << player.cash << ",\"bank\":" << player.bank_balance
           << ",\"loan\":" << player.loan << ",\"special_finance\":" << player.special_finance
           << ",\"points\":" << player.points << ",\"node\":" << player.node_id
           << ",\"last_node\":" << player.last_node_id << ",\"x\":" << player.x << ",\"y\":" << player.y
           << ",\"direction\":" << static_cast<unsigned>(player.direction)
           << ",\"traffic_method\":" << static_cast<unsigned>(player.traffic_method)
           << ",\"dice_count\":" << static_cast<unsigned>(player.dice_count)
           << ",\"card_count\":" << rich4::card_count(player.cards) << ",\"cards\":";
    write_array(output, player.cards);
    output << ",\"tool_amounts\":";
    write_array(output, player.tool_amounts);
    output << '}';
}

void write_land(std::ostream &output, const rich4::LandEconomyRecord &land, std::size_t id) {
    output << "{\"id\":" << id << ",\"owner_code\":" << static_cast<unsigned>(land.owner_code)
           << ",\"level\":" << static_cast<unsigned>(land.level)
           << ",\"chain_store\":" << static_cast<unsigned>(land.chain_store)
           << ",\"street_big5_hex\":" << quoted(hex_bytes(land.street_name)) << '}';
}

void write_snapshot(std::ostream &output, const Snapshot &snapshot) {
    const auto header = snapshot.save.header();
    const auto map = snapshot.save.map_summary();
    output << "{\"path\":" << quoted(snapshot.path.string()) << ",\"file_bytes\":" << snapshot.bytes.size()
           << ",\"byte_roundtrip_identical\":true"
           << ",\"date\":{\"year\":" << header.date.year
           << ",\"month\":" << static_cast<unsigned>(header.date.month)
           << ",\"day\":" << static_cast<unsigned>(header.date.day) << '}' << ",\"map\":" << header.map_id
           << ",\"stage\":" << header.stage_id << ",\"current_player\":" << header.current_player
           << ",\"player_count\":" << header.player_count
           << ",\"human_player_count\":" << header.human_player_count
           << ",\"price_index\":" << header.price_index << ",\"map_nodes\":" << map.nodes.count
           << ",\"land_count\":" << map.lands.count << ",\"players\":[";
    for (std::size_t slot = 0; slot < header.player_count; ++slot) {
        if (slot)
            output << ',';
        write_player(output, snapshot.save.player(slot));
    }
    output << "],\"lands\":[";
    for (std::size_t index = 0; index < snapshot.lands.size(); ++index) {
        if (index)
            output << ',';
        write_land(output, snapshot.lands[index], index + 1);
    }
    output << "]}";
}

struct Changes {
    std::size_t node_changes = 0;
    std::size_t financial_changes = 0;
    std::size_t card_changes = 0;
    std::size_t tool_changes = 0;
    std::size_t land_changes = 0;
    std::string players_json;
    std::string lands_json;
};

void add_change(std::ostream &output, bool &first, std::string_view field, std::int64_t before,
                std::int64_t after) {
    if (before == after)
        return;
    if (!first)
        output << ',';
    first = false;
    output << "{\"field\":" << quoted(field) << ",\"before\":" << before << ",\"after\":" << after
           << ",\"delta\":" << after - before << '}';
}

Changes compare_snapshots(const Snapshot &before, const Snapshot &after, bool same_map) {
    Changes changes;
    std::ostringstream players, lands;
    players << '[';
    bool first_player = true;
    const auto slots = std::min(before.save.header().player_count, after.save.header().player_count);
    for (std::size_t slot = 0; slot < slots; ++slot) {
        const auto old = before.save.player(slot);
        const auto now = after.save.player(slot);
        std::ostringstream fields;
        bool first = true;
        add_change(fields, first, "character", old.character, now.character);
        add_change(fields, first, "controller", old.controller_flags, now.controller_flags);
        add_change(fields, first, "cash", old.cash, now.cash);
        add_change(fields, first, "bank", old.bank_balance, now.bank_balance);
        add_change(fields, first, "loan", old.loan, now.loan);
        add_change(fields, first, "special_finance", old.special_finance, now.special_finance);
        add_change(fields, first, "points", old.points, now.points);
        add_change(fields, first, "node", old.node_id, now.node_id);
        add_change(fields, first, "last_node", old.last_node_id, now.last_node_id);
        add_change(fields, first, "x", old.x, now.x);
        add_change(fields, first, "y", old.y, now.y);
        add_change(fields, first, "direction", old.direction, now.direction);
        add_change(fields, first, "traffic_method", old.traffic_method, now.traffic_method);
        add_change(fields, first, "dice_count", old.dice_count, now.dice_count);
        const bool cards_changed = old.cards != now.cards;
        const bool tools_changed = old.tool_amounts != now.tool_amounts;
        if (old.node_id != now.node_id)
            ++changes.node_changes;
        if (old.cash != now.cash || old.bank_balance != now.bank_balance || old.loan != now.loan ||
            old.special_finance != now.special_finance || old.points != now.points)
            ++changes.financial_changes;
        if (cards_changed)
            ++changes.card_changes;
        if (tools_changed)
            ++changes.tool_changes;
        if (first && !cards_changed && !tools_changed)
            continue;
        if (!first_player)
            players << ',';
        first_player = false;
        players << "{\"slot\":" << slot << ",\"fields\":[" << fields.str() << ']';
        if (cards_changed) {
            players << ",\"cards_before\":";
            write_array(players, old.cards);
            players << ",\"cards_after\":";
            write_array(players, now.cards);
        }
        if (tools_changed) {
            players << ",\"tools_before\":";
            write_array(players, old.tool_amounts);
            players << ",\"tools_after\":";
            write_array(players, now.tool_amounts);
        }
        players << '}';
    }
    players << ']';
    lands << '[';
    if (same_map) {
        for (std::size_t index = 0; index < before.lands.size(); ++index) {
            const auto &old = before.lands[index];
            const auto &now = after.lands[index];
            if (old.owner_code == now.owner_code && old.level == now.level &&
                old.chain_store == now.chain_store && old.street_name == now.street_name)
                continue;
            if (changes.land_changes++)
                lands << ',';
            lands << "{\"id\":" << index + 1 << ",\"before\":";
            write_land(lands, old, index + 1);
            lands << ",\"after\":";
            write_land(lands, now, index + 1);
            lands << '}';
        }
    }
    lands << ']';
    changes.players_json = players.str();
    changes.lands_json = lands.str();
    return changes;
}

bool same_path(const std::filesystem::path &one, const std::filesystem::path &two) {
    if (std::filesystem::weakly_canonical(one) == std::filesystem::weakly_canonical(two))
        return true;
    return std::filesystem::exists(one) && std::filesystem::exists(two) &&
           std::filesystem::equivalent(one, two);
}

} // namespace

int main(int argc, char **argv) {
    try {
        if (argc < 3) {
            std::cerr << "Usage: rich4-state-check BEFORE.DAT AFTER.DAT [--output REPORT.json] "
                         "[--require-native-gameplay]\n";
            return 2;
        }
        std::optional<std::filesystem::path> output_path;
        bool require_native = false;
        for (int index = 3; index < argc; ++index) {
            const std::string_view option(argv[index]);
            if (option == "--require-native-gameplay") {
                require_native = true;
            } else if (option == "--output" && index + 1 < argc && !output_path) {
                output_path = argv[++index];
            } else {
                throw std::invalid_argument("Unknown/incomplete argument: " + std::string(option));
            }
        }
        if (output_path && (same_path(*output_path, argv[1]) || same_path(*output_path, argv[2]))) {
            throw std::invalid_argument("Report output must not overwrite a captured save");
        }
        const auto before = load_snapshot(argv[1]);
        const auto after = load_snapshot(argv[2]);
        const auto first = before.save.header();
        const auto last = after.save.header();
        const bool same_map = first.map_id == last.map_id && first.stage_id == last.stage_id &&
                              before.lands.size() == after.lands.size();
        const auto changes = compare_snapshots(before, after, same_map);
        std::size_t changed_bytes = std::max(before.bytes.size(), after.bytes.size()) -
                                    std::min(before.bytes.size(), after.bytes.size());
        for (std::size_t index = 0; index < std::min(before.bytes.size(), after.bytes.size()); ++index) {
            if (before.bytes[index] != after.bytes[index])
                ++changed_bytes;
        }
        const bool date_changed = first.date.year != last.date.year || first.date.month != last.date.month ||
                                  first.date.day != last.date.day;
        std::ostringstream report;
        report << "{\"schema\":1,\"tool\":\"rich4-native-state-check\","
               << "\"acceptance_scope\":\"captured_save_readback_and_difference\","
               << "\"state_readback_passed\":true,\"native_main_loop_available\":false,"
               << "\"native_gameplay_verified\":false,\"full_gameplay_verified\":false,"
               << "\"original_turn_rules_verified\":false,\"migrated_function_replay_performed\":false,"
               << "\"native_gameplay_gate\":" << quoted(require_native ? "unsupported" : "not_requested")
               << ',' << "\"limitations\":[\"No native game loop exists in this build\","
               << "\"Save differences are observations, not inferred turn, purchase, or card rules\","
               << "\"Unmigrated fields are preserved but not interpreted\"],\"before\":";
        write_snapshot(report, before);
        report << ",\"after\":";
        write_snapshot(report, after);
        report << ",\"changes\":{\"raw_file_changed_bytes\":" << changed_bytes
               << ",\"date_changed\":" << (date_changed ? "true" : "false") << ",\"current_player_changed\":"
               << (first.current_player != last.current_player ? "true" : "false")
               << ",\"player_count_changed\":" << (first.player_count != last.player_count ? "true" : "false")
               << ",\"land_records_comparable\":" << (same_map ? "true" : "false")
               << ",\"players_with_node_changes\":" << changes.node_changes
               << ",\"players_with_financial_changes\":" << changes.financial_changes
               << ",\"players_with_card_changes\":" << changes.card_changes
               << ",\"players_with_tool_changes\":" << changes.tool_changes
               << ",\"land_changes_count\":" << changes.land_changes
               << ",\"player_changes\":" << changes.players_json << ",\"land_changes\":" << changes.lands_json
               << "}}\n";
        if (output_path) {
            std::ofstream output(*output_path, std::ios::binary);
            if (!output || !(output << report.str()))
                throw std::runtime_error("Cannot write state report");
            std::cout << "Captured save readback passed; native gameplay remains unavailable. Report: "
                      << output_path->string() << '\n';
        } else {
            std::cout << report.str();
        }
        if (require_native) {
            std::cerr << "Native gameplay gate is unsupported: this build has no native game loop.\n";
            return 3;
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "state check: " << error.what() << '\n';
        return 1;
    }
}
