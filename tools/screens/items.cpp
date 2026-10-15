// SPDX-License-Identifier: GPL-3.0-or-later
#include "items.hpp"
#include "voices.hpp" // AUDIO: clerk voice ids

#include "messages.hpp"
#include "rich4/cards.hpp"
#include "rich4/mod_rules.hpp"
#include "rich4/flc.hpp"
#include "rich4/inventory.hpp"
#include "rich4/services.hpp"
#include "rich4/world.hpp"

#include <algorithm>
#include <array>
#include <iostream>

namespace rich4::native::items {
namespace detail {
struct CastMovie {
    flc::Decoder decoder;
    graphics::Image image;
    bool sounded = false;
};
} // namespace detail
namespace {
using graphics::Rect;

constexpr auto white = ui::color(255, 255, 255), dark = ui::color(16, 16, 16), grey = ui::color(160, 160, 160);

// help.mkf description entries by card / tool id (§1.5, §2.3).
constexpr std::array<unsigned, 30> card_help{63, 62, 85, 77, 78, 86, 64, 66, 57, 76, 65, 67, 81, 73, 58,
                                             82, 74, 75, 80, 60, 61, 72, 84, 70, 79, 69, 83, 68, 59, 71};
constexpr std::array<unsigned, 13> tool_help{99, 95, 88, 90, 97, 89, 91, 96, 98, 92, 94, 87, 93};

void opaque(Frame &frame, const graphics::Image &image, int x, int y) {
    graphics::blit(frame.canvas, image, x, y, graphics::Transparency::opaque);
}
void keyed(Frame &frame, const graphics::Image &image, int x, int y) {
    graphics::blit(frame.canvas, image, x, y);
}
const graphics::Image &art(Frame &frame, char archive, std::size_t entry, std::size_t index) {
    return frame.artwork.frame(archive, entry, index);
}

// Original font flags: bit0 shadow (second colour at +1,+1), bit1 bold.
void styled(Frame &frame, std::string_view text, int x, int y, int size, std::uint16_t fg, bool shadow,
            std::uint16_t second, bool bold) {
    if (shadow) {
        frame.font.draw(frame.canvas, text, x + 1, y + 1, size, second);
        if (bold)
            frame.font.draw(frame.canvas, text, x + 2, y + 1, size, second);
    }
    frame.font.draw(frame.canvas, text, x, y, size, fg);
    if (bold)
        frame.font.draw(frame.canvas, text, x + 1, y, size, fg);
}
int top_for_centre(int centre_y, int size) {
    return centre_y - size * 9 / 16;
}
std::vector<std::string_view> lines(std::string_view text) {
    std::vector<std::string_view> result;
    for (std::size_t start = 0;;) {
        const auto end = text.find('\n', start);
        result.push_back(text.substr(start, end == std::string_view::npos ? end : end - start));
        if (end == std::string_view::npos)
            return result;
        start = end + 1;
    }
}
// _rich4_draw_text mode 2 (centre) and mode 4 (multi-line, lines centred).
void centred(Frame &frame, std::string_view text, int cx, int cy, int size, std::uint16_t fg, bool shadow = true,
             std::uint16_t second = dark, bool bold = true) {
    const auto rows = lines(text);
    const int height = size + 4;
    int y = cy - int(rows.size()) * height / 2 + height / 2;
    for (const auto row : rows) {
        styled(frame, row, cx - frame.font.measure(row, size) / 2, top_for_centre(y, size), size, fg, shadow, second,
               bold);
        y += height;
    }
}
// Mode 1: top-right anchor.
void top_right(Frame &frame, std::string_view text, int right, int top, int size, std::uint16_t fg) {
    styled(frame, text, right - frame.font.measure(text, size) - 1, top, size, fg, true, dark, true);
}
// Mode 6: right edge, vertically centred.
void right_middle(Frame &frame, std::string_view text, int right, int cy, int size, std::uint16_t fg) {
    styled(frame, text, right - frame.font.measure(text, size) - 1, top_for_centre(cy, size), size, fg, true, dark,
           true);
}

// fcn_00451b9e: shift the rect 1 px down-right and darken the new top row and left column.
void sink(graphics::Image &canvas, Rect rect) {
    rect.left = std::clamp(rect.left, 0, canvas.width);
    rect.right = std::clamp(rect.right, 0, canvas.width);
    rect.top = std::clamp(rect.top, 0, canvas.height);
    rect.bottom = std::clamp(rect.bottom, 0, canvas.height);
    if (rect.right - rect.left < 2 || rect.bottom - rect.top < 2)
        return;
    const auto at = [&](int x, int y) -> std::uint16_t & { return canvas.pixels[std::size_t(y * canvas.width + x)]; };
    for (int y = rect.bottom - 1; y > rect.top; --y)
        for (int x = rect.right - 1; x > rect.left; --x)
            at(x, y) = at(x - 1, y - 1);
    const auto darken = [](std::uint16_t value) {
        std::uint16_t result = 0;
        for (unsigned shift : {0U, 5U, 10U})
            result = static_cast<std::uint16_t>(result | (((value >> shift) & 31U) / 2U) << shift);
        return result;
    };
    for (int x = rect.left; x < rect.right; ++x)
        at(x, rect.top) = darken(at(x, rect.top));
    for (int y = rect.top + 1; y < rect.bottom; ++y)
        at(rect.left, y) = darken(at(rect.left, y));
}

// Touch selection highlight: blend the rect toward warm yellow.
void tint(graphics::Image &canvas, Rect rect) {
    rect.left = std::clamp(rect.left, 0, canvas.width);
    rect.right = std::clamp(rect.right, 0, canvas.width);
    rect.top = std::clamp(rect.top, 0, canvas.height);
    rect.bottom = std::clamp(rect.bottom, 0, canvas.height);
    constexpr std::array<unsigned, 3> target{6, 28, 31}; // b, g, r in 5-bit units
    for (int y = rect.top; y < rect.bottom; ++y)
        for (int x = rect.left; x < rect.right; ++x) {
            auto &pixel = canvas.pixels[std::size_t(y * canvas.width + x)];
            std::uint16_t result = 0;
            for (unsigned channel = 0; channel < 3; ++channel) {
                const unsigned shift = channel * 5, value = (pixel >> shift) & 31U;
                result = static_cast<std::uint16_t>(result | ((value + target[channel]) / 2U) << shift);
            }
            pixel = result;
        }
}

Rect grid_tile(int origin_x, int origin_y, int tile) {
    const int c = tile % 5, r = tile / 5;
    return {origin_x + 6 + 80 * c, origin_y + 6 + 56 * r, origin_x + 84 + 80 * c, origin_y + 60 + 56 * r};
}
// Card names (§1.1) on a grid whose top-left is (x, y).
void card_names_on_grid(Frame &frame, const CardSlots &cards, int x, int y, std::vector<std::uint8_t> *order = nullptr) {
    int tile = 0;
    for (const auto id : cards) {
        if (!id || id > 30)
            continue;
        centred(frame, services::card_names[id - 1], x + 45 + 80 * (tile % 5), y + 33 + 56 * (tile / 5), 20, white);
        if (order)
            order->push_back(id);
        ++tile;
    }
}
// Tool icons and "×n" counts (§2.1) on a grid whose top-left is (x, y).
std::vector<unsigned> tools_on_grid(Frame &frame, const PlayerState &player, int x, int y) {
    std::vector<unsigned> order;
    for (unsigned id = 1; id <= 13; ++id) {
        if (!player.tool_amounts[id - 1])
            continue;
        const int tile = int(order.size());
        const int c = tile % 5, r = tile / 5;
        keyed(frame, art(frame, 'p', 11, id + 1), x + 29 + 80 * c, y + 33 + 56 * r);
        const auto amount = player.tool_amounts[id - 1];
        // MOD: "×999" can cover the entire tool sprite at the original 20px
        // inventory-count anchor. Keep classic <=9 counts unchanged; put wide
        // counts on the lower edge in a smaller font, clear of the icon.
        top_right(frame, "×" + std::to_string(amount), x + 79 + 80 * c,
                  y + (amount >= 10 ? 39 : 23) + 56 * r, amount >= 10 ? 14 : 20, white);
        order.push_back(id);
    }
    return order;
}
std::optional<std::size_t> option_of(const GameDecision &decision, std::int32_t value) {
    for (std::size_t i = 0; i < decision.options.size(); ++i)
        if (decision.options[i].value == value)
            return i;
    return std::nullopt;
}
void slide(std::uint64_t elapsed, int &list_x, int &grid_x) {
    list_x = -222;
    grid_x = 640;
    int vx = 40, vy = 80;
    for (std::uint64_t tick = 0; tick < elapsed / 50 && (list_x < 5 || grid_x > 227); ++tick) {
        list_x = std::min(5, list_x + vx);
        grid_x = std::max(227, grid_x - vy);
        vx = std::max(vx - 3, 1);
        vy = std::max(vy - 7, 1);
    }
}
} // namespace

std::uint64_t Screens::now(const Frame &frame) const {
    return clock ? *clock : frame.ticks;
}

void Screens::run_deferred() {
    auto actions = std::move(deferred_);
    deferred_.clear();
    for (auto &action : actions)
        action();
}

void Screens::reset() {
    cast_.reset();
    popups_.clear();
    deferred_.clear();
    press_.reset();
    last_card_ = 0;
    last_tool_ = 0;
    end_visits();
}

void Screens::voice_once(const std::string &key, std::initializer_list<std::size_t> clips) {
    if (key == voiced_)
        return;
    voiced_ = key;
    if (!speech || key.empty())
        return;
    bool first = true;
    for (const auto clip : clips) {
        speech(clip, !first);
        first = false;
    }
}

// AUDIO: bail-out lines. Hospital: 請稍後！ 可以出院了！ (the rescued NPC's thanks) 要保重身體喔！;
// prison: the rescued NPC's thanks (_special_player_strings, ui_prison.asm:802 / ui_hospital.asm:549).
void Screens::rescue_voice(bool hospital, int slot) {
    if (!speech)
        return;
    std::vector<std::size_t> clips;
    if (hospital)
        clips = {128, 129};
    if (slot >= 4)
        clips.push_back(voices::rescued_npc_voice(slot));
    if (hospital)
        clips.push_back(130);
    for (std::size_t i = 0; i < clips.size(); ++i)
        speech(clips[i], i > 0);
}

void Screens::end_visits() {
    shop_ = {};
    magic_ = {};
    rescue_ = {};
    voiced_.clear(); // AUDIO
}

void Screens::press(Frame &frame, std::string owner, Rect rect, std::function<void()> action, int item) {
    (void)frame;
    press_ = Press{std::move(owner), rect, std::move(action), item};
    if (sound)
        sound(1);
}

// Touch press/release: the tile sinks while held and acts when released on it. Sliding off cancels.
void Screens::resolve_press(Frame &frame, std::string_view owner) {
    if (!press_)
        return;
    if (press_->owner != owner) {
        press_.reset();
        return;
    }
    if (frame.pointer_down)
        return;
    auto action = std::move(press_->action);
    const bool inside = ui::contains(press_->rect, frame.pointer_x, frame.pointer_y);
    press_.reset();
    if (inside && action)
        deferred_.push_back(std::move(action));
}

const std::string &Screens::description(Artwork &artwork, bool card, unsigned id) {
    auto &text = descriptions_[{card, id}];
    if (!text.empty())
        return text;
    if (card && id == 15) {
        // help.mkf entry 58 is a byte copy of the 紅卡 text in the shipped data (§1.5).
        text = "讓其他所有角色冬眠五天，期間無法行動。";
        return text;
    }
    try {
        const auto bytes = artwork.archive('h').decode(card ? card_help.at(id - 1) : tool_help.at(id - 1));
        std::string section;
        for (std::size_t start = 0; start < bytes.size();) {
            auto end = start;
            while (end < bytes.size() && bytes[end])
                ++end;
            if (end == start && !section.empty())
                break; // the first blank line ends the 功能 section
            section.append(reinterpret_cast<const char *>(bytes.data() + start), end - start);
            start = end + 1;
        }
        text = ui::big5(section);
        if (text.starts_with("功能："))
            text.erase(0, std::string_view("功能：").size());
    } catch (const std::exception &) {
        text = " ";
    }
    return text;
}

const graphics::Image &Screens::card_art(Artwork &artwork, std::uint8_t card) {
    auto &image = card_art_[card];
    if (!image.pixels.empty())
        return image;
    const auto bytes = artwork.archive('d').decode(570U + card);
    image.width = 165;
    image.height = 256;
    image.pixels.assign(std::size_t(165 * 256), 0);
    for (std::size_t i = 0; i < image.pixels.size() && i * 2 + 1 < bytes.size(); ++i)
        image.pixels[i] = static_cast<std::uint16_t>(bytes[i * 2] | bytes[i * 2 + 1] << 8);
    return image;
}

// ---------------------------------------------------------------------------------------------
// §1 / §2 use-card and use-tool grids.
void Screens::draw_inventory(Frame &frame, GameSession &session, bool cards, const Host &host) {
    const std::string owner = cards ? "cards" : "tools";
    resolve_press(frame, owner);
    shot = owner;
    const auto &player = session.world().active_player().state;
    constexpr int gx = 14, gy = 130;
    opaque(frame, art(frame, 'p', 11, cards ? 0 : 1), gx, gy);
    GameSession *game = &session;
    std::vector<std::pair<unsigned, int>> tiles; // id, tile index
    if (cards) {
        std::vector<std::uint8_t> order;
        card_names_on_grid(frame, player.cards, gx, gy, &order);
        for (std::size_t t = 0; t < order.size(); ++t)
            tiles.emplace_back(order[t], int(t));
    } else {
        const auto order = tools_on_grid(frame, player, gx, gy);
        for (std::size_t t = 0; t < order.size(); ++t)
            tiles.emplace_back(order[t], int(t));
        const auto vehicle = player.traffic_method & 3U;
        if (vehicle == 1 || vehicle == 2) {
            opaque(frame, art(frame, 'p', 11, vehicle == 1 ? 15 : 16), gx + 325, gy + 117);
            const auto rect = grid_tile(gx, gy, 14);
            frame.hit(dismount_id, rect, [this, &frame, rect, game, host] {
                press(frame, "tools", rect, [game, host] {
                    const bool success = game->dismount();
                    if (success)
                        host.close();
                    host.notify(success, "目前無法下車");
                });
            });
        }
    }
    for (const auto &[id, tile] : tiles) {
        const auto rect = grid_tile(gx, gy, tile);
        frame.hit(card_tile_id + int(id), rect, [this, &frame, rect, id = id, cards, game, host] {
            press(
                frame, cards ? "cards" : "tools", rect,
                [this, id, cards, game, host] {
                    if (!cards) {
                        last_tool_ = id;
                        const bool success = game->use_tool(id);
                        if (success)
                            host.close();
                        else if (sound)
                            sound(3);
                        host.notify(success, "目前無法使用");
                        return;
                    }
                    const auto card = static_cast<std::uint8_t>(id);
                    // §1.3: the card art popup comes first, then the card function runs.
                    popups_.push_back({card, 0, [this, card, game, host] {
                                           if (!active_card(card)) {
                                               if (sound)
                                                   sound(3);
                                               host.notify(false, "這張卡會在需要時自動使用");
                                               return;
                                           }
                                           last_card_ = card;
                                           const bool success = game->use_card({card, {}, {}});
                                           if (success)
                                               host.close();
                                           else if (sound)
                                               sound(3);
                                           host.notify(success, "目前無法使用");
                                       }});
                },
                int(id));
        });
    }
    if (press_ && press_->owner == owner) {
        sink(frame.canvas, press_->rect);
        shot = owner + "-pressed";
    }

    // Native-only help strip (the original shows descriptions only in its Help screen).
    const Rect strip{gx, 316, gx + 412, 382};
    ui::fill(frame.canvas, strip, ui::paper);
    ui::border(frame.canvas, strip, ui::color(110, 74, 26));
    if (press_ && press_->owner == owner && press_->item) {
        const auto item = unsigned(press_->item);
        const auto name = std::string(cards ? services::card_names.at(item - 1) : services::tool_names.at(item - 1));
        frame.text(name, strip.left + 8, strip.top + 5, 16, ui::blue);
        frame.text(description(frame.artwork, cards, item), strip.left + 8, strip.top + 26, 14, ui::ink, 396);
    } else if (tiles.empty() && (cards || !(player.traffic_method & 3U))) {
        frame.text(cards ? "目前沒有卡片。" : "目前沒有道具。", strip.left + 10, strip.top + 10, 16);
    } else {
        frame.text(cards ? "按住卡片可看說明，放開就使用。" : "按住道具可看說明，放開就使用。", strip.left + 10,
                   strip.top + 8, 16);
        frame.text("手指滑開再放開即可取消。", strip.left + 10, strip.top + 33, 14, ui::color(90, 80, 60));
    }
    frame.button(back_id, {gx + 146, 392, gx + 266, 432}, "返回", [host] { host.close(); });
    frame.hit(62, {gx, gy, gx + 412, gy + 180}, [] {});
    frame.hit(outside_id, {0, 0, 640, 480}, [host] { host.close(); });
}

// §1.4 card art popup.
void Screens::observe(const GameEvent &event, const World &world) {
    if (!event.card || *event.card < 1 || *event.card > 30 || event.player >= world.players.size())
        return;
    // Human active cards already showed the popup when the tile was released.
    if ((world.players[event.player].state.controller_flags & 3U) == 1 && active_card(*event.card))
        return;
    popups_.push_back({*event.card, 0, {}});
}

void Screens::draw_popup(Frame &frame) {
    if (popups_.empty())
        return;
    auto &popup = popups_.front();
    if (!popup.until) {
        popup.until = now(frame) + 1500;
        if (sound)
            sound(62);
    }
    if (now(frame) >= popup.until) {
        if (popup.then)
            deferred_.push_back(std::move(popup.then));
        popups_.pop_front();
        return;
    }
    const auto card = popup.card;
    shot = "popup-" + std::to_string(card);
    opaque(frame, card_art(frame.artwork, card), 138, 200);
    if (card == 14) {
        // Data.mkf 584 decodes with its lower half as noise (§1.5): show a text tile instead.
        const Rect tile{138, 200, 303, 456};
        ui::fill(frame.canvas, tile, ui::color(40, 64, 112));
        ui::border(frame.canvas, tile, ui::color(16, 16, 16));
        ui::border(frame.canvas, {tile.left + 4, tile.top + 4, tile.right - 4, tile.bottom - 4},
                   ui::color(232, 196, 96));
        ui::border(frame.canvas, {tile.left + 7, tile.top + 7, tile.right - 7, tile.bottom - 7},
                   ui::color(232, 196, 96));
        centred(frame, "停", 220, 270, 56, ui::color(250, 236, 180));
        centred(frame, "留", 220, 330, 56, ui::color(250, 236, 180));
        centred(frame, "卡", 220, 390, 56, ui::color(250, 236, 180));
    }
    keyed(frame, art(frame, 'd', 517, 5), 220, 129);
    centred(frame, "使用" + std::string(services::card_names.at(card - 1)), 220, 129, 16, ui::color(240, 240, 240),
            true, dark, false);
    frame.hit(popup_skip_id, {0, 0, 640, 480}, [this] {
        if (popups_.empty())
            return;
        auto then = std::move(popups_.front().then);
        popups_.pop_front();
        if (then)
            then();
    });
}

// ---------------------------------------------------------------------------------------------
// §3 target picking: the original cursor sprites drawn on every valid target.
void Screens::draw_targets(Frame &frame, const GameDecision &decision, const board::Camera &camera,
                           std::optional<std::size_t> selected, const std::function<void(std::size_t)> &tap) {
    int first = 8, count = 4; // red reticle
    bool card = false;
    unsigned item = last_tool_;
    for (std::size_t i = 0; i < services::card_names.size(); ++i)
        if (decision.prompt.starts_with(services::card_names[i])) {
            card = true;
            item = unsigned(i + 1);
        }
    if (card) {
        first = 12;
        count = 15;
    } else if (decision.kind == DecisionKind::action_target && last_tool_) {
        switch (last_tool_) {
        case 2: first = 0, count = 1; break;
        case 3: first = 1, count = 1; break;
        case 4: first = 2, count = 1; break;
        case 7: first = 3, count = 1; break;
        case 13: first = 4, count = 1; break;
        case 9: first = 9, count = 3; break;
        case 11:
            if (decision.prompt.find("目的") == std::string::npos)
                first = 32, count = 2;
            else
                first = 9, count = 3;
            break;
        default: break;
        }
    }
    struct Marker {
        std::size_t index;
        board::Point point;
    };
    std::vector<Marker> markers;
    for (std::size_t i = 0; i < decision.options.size(); ++i) {
        const auto &location = decision.options[i].location;
        if (!location)
            continue;
        const auto projected = board::project({location->x, location->y}, camera);
        if (projected && projected->x >= 12 && projected->x < 428 && projected->y >= 54 && projected->y < 466)
            markers.push_back({i, *projected});
    }
    const auto animation = std::size_t(now(frame) / 100);
    shot = "target-" + std::string(card ? "card-" : "tool-") + std::to_string(item) + (selected ? "-selected" : "");
    for (const auto &marker : markers) {
        if (selected == marker.index)
            continue;
        if ((animation / 3 + marker.index) % 2 == 0) // a slow pulse so small cursors stay noticeable
            keyed(frame, art(frame, 'd', 0, 42), marker.point.x, marker.point.y + 12);
        keyed(frame, art(frame, 'd', 0, std::size_t(first) + (animation + marker.index) % std::size_t(count)),
              marker.point.x, marker.point.y);
    }
    for (const auto &marker : markers)
        if (selected == marker.index) {
            const auto [x, y] = marker.point;
            keyed(frame, art(frame, 'd', 0, 42), x, y + 12);
            keyed(frame, art(frame, 'd', 0, std::size_t(first) + animation % std::size_t(count)), x, y);
            const int bounce = int((now(frame) / 80) % 8);
            keyed(frame, art(frame, 'd', 0, 38), x, y - 16 - (bounce < 4 ? bounce : 8 - bounce));
        }
    // Finger-sized hit areas (the cursor art is mouse-sized). Where they overlap, the tap goes to
    // the target nearest to the finger, so a target's own centre always picks that target.
    const auto nearest = [&frame, markers, tap] {
        std::optional<std::size_t> best;
        long best_distance = 0;
        for (const auto &marker : markers) {
            const long dx = marker.point.x - frame.pointer_x, dy = marker.point.y - frame.pointer_y;
            if (!best || dx * dx + dy * dy < best_distance) {
                best = marker.index;
                best_distance = dx * dx + dy * dy;
            }
        }
        if (best)
            tap(*best);
    };
    for (const auto &marker : markers) {
        const auto [x, y] = marker.point;
        frame.hit(5000 + int(marker.index), {x - 30, y - 32, x + 30, y + 28}, nearest);
    }
}

// ---------------------------------------------------------------------------------------------
bool Screens::draw_decision(Frame &frame, GameSession &session, const Host &host) {
    const auto &decision = *session.pending_decision();
    switch (decision.kind) {
    case DecisionKind::shop:
        magic_ = {};
        rescue_ = {};
        draw_shop(frame, session, decision, host);
        return true;
    case DecisionKind::magic_category:
    case DecisionKind::magic_effect:
        shop_ = {};
        rescue_ = {};
        draw_magic(frame, session, decision, host);
        return true;
    case DecisionKind::rescue:
        shop_ = {};
        magic_ = {};
        draw_rescue(frame, session, decision, host);
        return true;
    case DecisionKind::action_target: // 遙控骰子: options 1..6 points then 取消 (0), no map targets
        if (decision.victim) { // 搶奪卡 inventory pick (fcn_0044192a)
            end_visits();
            draw_steal(frame, session, decision, host);
            return true;
        }
        steal_pick_.reset();
        if (decision.options.size() != 7 ||
            !std::all_of(decision.options.begin(), decision.options.end(), [&decision](const auto &option) {
                return !option.location && option.value == ((&option - decision.options.data()) + 1) % 7;
            }))
            break;
        end_visits();
        draw_remote_dice(frame, decision, host);
        return true;
    case DecisionKind::card_target: // 嫁禍卡 victim (rich4_card_passive.asm:170-215)
        // The core prompt is 0x46535d "請選擇嫁禍對象..." (several) or "是否嫁禍給%s？" (one, which
        // is also a 2-option yes/no); the old simplified text is kept for the items gallery.
        if (decision.prompt != "使用嫁祸卡？" && decision.prompt != "請選擇嫁禍對象...")
            break;
        end_visits();
        draw_frame_up(frame, session, decision, host);
        return true;
    default:
        break;
    }
    end_visits();
    return false;
}

// 遙控骰子 (_rich4_use_tool_yaokongtouzi): the Panel 72:0 six-face bar at (92,300) over the board;
// the touched face lights with 72:n at (104+40(n-1), 314) (fcn_00446774). Each face's hit area
// covers its whole 40x55 column of the bar; 取消 stands in for the original right-click.
void Screens::draw_remote_dice(Frame &frame, const GameDecision &decision, const Host &host) {
    shot = "remote-dice";
    keyed(frame, art(frame, 'p', 72, 0), 92, 300);
    for (std::size_t i = 0; i < decision.options.size(); ++i) {
        const int value = decision.options[i].value;
        if (value < 1 || value > 6)
            continue;
        const int x = 104 + 40 * (value - 1);
        const Rect cell{value == 1 ? 92 : x - 5, 300, value == 6 ? 348 : x + 35, 355};
        if (frame.pointer_down && frame.pointer_in(cell))
            keyed(frame, art(frame, 'p', 72, std::size_t(value)), x, 314);
        frame.hit(option_id + int(i), cell, [host, i] { host.respond(i); });
    }
    if (const auto cancel = option_of(decision, 0))
        frame.button(option_id + int(*cancel), {356, 306, 436, 350}, "取消",
                     [host, index = *cancel] { host.respond(index); });
}

// 搶奪卡 for a human (fcn_0044192a): the victim's card grid (Panel 11:0) at (14,70) and, since the
// card allows tools, their tool grid (Panel 11:1) at (14,270), both in the victim's hand order; the
// original takes the clicked item at once. Touch: the first tap selects a tile (sunk, yellow frame,
// its name above the grids), a second tap takes it; 取消 replaces the right-click.
void Screens::draw_steal(Frame &frame, GameSession &session, const GameDecision &decision, const Host &host) {
    shot = "steal";
    const auto &world = session.world();
    if (!decision.victim || *decision.victim >= world.players.size())
        return;
    const auto &victim = world.players[*decision.victim].state;
    constexpr int gx = 14, card_y = 70, tool_y = 270;
    opaque(frame, art(frame, 'p', 11, 0), gx, card_y);
    opaque(frame, art(frame, 'p', 11, 1), gx, tool_y);
    std::vector<std::uint8_t> cards;
    card_names_on_grid(frame, victim.cards, gx, card_y, &cards);
    const auto tools = tools_on_grid(frame, victim, gx, tool_y);
    const auto option_for = [&decision](CardTargetKind kind, unsigned id) -> std::optional<std::size_t> {
        return option_of(decision, std::int32_t((unsigned(kind) << 16U) | id));
    };
    if (steal_pick_ && *steal_pick_ >= decision.options.size())
        steal_pick_.reset();
    const auto tile = [&](CardTargetKind kind, unsigned id, Rect rect) {
        const auto index = option_for(kind, id);
        if (!index)
            return;
        if (steal_pick_ == *index || (frame.pointer_down && frame.pointer_in(rect)))
            sink(frame.canvas, rect);
        if (steal_pick_ == *index)
            for (int inset = 0; inset < 3; ++inset)
                ui::border(frame.canvas, {rect.left + inset, rect.top + inset, rect.right - inset, rect.bottom - inset},
                           ui::color(255, 255, 0));
        frame.hit(option_id + int(*index), rect, [this, host, index = *index] {
            if (steal_pick_ == index) {
                steal_pick_.reset();
                if (sound)
                    sound(1);
                host.respond(index);
            } else {
                steal_pick_ = index;
                if (sound)
                    sound(0);
            }
        });
    };
    for (std::size_t t = 0; t < cards.size(); ++t)
        tile(CardTargetKind::card, cards[t], grid_tile(gx, card_y, int(t)));
    for (std::size_t t = 0; t < tools.size(); ++t)
        tile(CardTargetKind::tool, tools[t], grid_tile(gx, tool_y, int(t)));
    // Prompt / selected item's name and help (native: the original has no hover text here).
    const Rect strip{gx, 41, gx + 412, 69};
    ui::fill(frame.canvas, strip, ui::paper);
    ui::border(frame.canvas, strip, ui::color(110, 74, 26));
    std::string line = "請選擇要搶的卡片或道具";
    if (steal_pick_) {
        const auto value = decision.options[*steal_pick_].value;
        const bool tool = (unsigned(value) >> 16U) == unsigned(CardTargetKind::tool);
        const auto id = unsigned(value & 0xffff);
        line = std::string(tool ? services::tool_names.at(id - 1) : services::card_names.at(id - 1)) +
               "　再點一次就搶走";
    }
    frame.text(line, strip.left + 8, strip.top + 5, 16, ui::ink);
    if (const auto cancel = option_of(decision, -1))
        frame.button(option_id + int(*cancel), {gx + 316, 452, gx + 412, 478}, "取消", [this, host, index = *cancel] {
            steal_pick_.reset();
            host.respond(index);
        });
}

// 嫁禍卡 for a human (fcn_00440e1a): with one candidate the original asks "是否嫁禍給%s？" (yes/no);
// with more it shows the Data 517:5 plate with "請選擇嫁禍對象..." at (220,140) and the Data 518
// face strip (2/3/4 slots) at (220,320) holding the candidates' Data 2 portraits at +12+80i.
void Screens::draw_frame_up(Frame &frame, GameSession &session, const GameDecision &decision,
                            const Host &host) {
    const auto &world = session.world();
    std::vector<std::size_t> candidates;
    for (std::size_t i = 0; i < decision.options.size(); ++i)
        if (decision.options[i].value >= 0 && std::size_t(decision.options[i].value) < world.players.size())
            candidates.push_back(i);
    const auto keep = option_of(decision, -1);
    const auto character = [&](std::size_t index) {
        return world.players[std::size_t(decision.options[index].value)].state.character;
    };
    if (candidates.size() == 1 && keep) {
        shot = "frame-up-yesno";
        const auto yes = candidates.front();
        messages::draw_yes_no(frame, "是否嫁禍給" + std::string(names.at(character(yes))) + "？",
                              option_id + int(yes), [host, yes] { host.respond(yes); },
                              option_id + int(*keep), [host, index = *keep] { host.respond(index); });
        return;
    }
    shot = "frame-up";
    keyed(frame, art(frame, 'd', 517, 5), 220, 140);
    centred(frame, "請選擇嫁禍對象...", 220, 140, 16, ui::color(240, 240, 240));
    const auto &strip = art(frame, 'd', 518, std::clamp<std::size_t>(candidates.size(), 2, 4) - 2);
    keyed(frame, strip, 220, 320);
    const int left = 220 - strip.origin_x, top = 320 - strip.origin_y;
    for (std::size_t k = 0; k < candidates.size() && k < 4; ++k) {
        const auto index = candidates[k];
        const Rect face{left + 12 + 80 * int(k), top + 12, left + 84 + 80 * int(k), top + 84};
        frame.picture('d', 2, character(index), face.left, face.top);
        if (frame.pointer_down && frame.pointer_in(face)) { // the god-of-death picker's 2 px yellow hover
            ui::border(frame.canvas, face, ui::color(255, 255, 0));
            ui::border(frame.canvas, {face.left + 1, face.top + 1, face.right - 1, face.bottom - 1},
                       ui::color(255, 255, 0));
        }
        frame.hit(option_id + int(index), {face.left - 4, face.top - 4, face.right + 4, face.bottom + 4},
                  [host, index] { host.respond(index); });
    }
    if (keep) // stands in for the original's right-click (keep the card)
        frame.button(option_id + int(*keep), {160, top + strip.height + 10, 280, top + strip.height + 52},
                     "保留卡片", [host, index = *keep] { host.respond(index); });
}

// §6 point shop.
namespace {
constexpr std::array<std::array<const char *, 5>, 2> shop_lines{
    {{"有什麼我能\n為你服務的嗎？", "請挑選你想要\n兌換的卡片。", "抱歉！\n你的點數不足！", "對不起！\n您的卡片欄已滿！",
      "歡迎下次再來！"},
     {"歡迎光臨\n道具店！", "您要兌換\n什麼道具？", "對不起，\n您的點券不夠！", "很抱歉！您的\n道具欄已滿！",
      "謝謝惠顧！"}}};
}

std::optional<std::int32_t> Screens::shop_pending_value() const {
    if (!shop_.active || shop_.pick_kind < 0)
        return std::nullopt;
    return shop_.pick_value;
}

bool Screens::shop_show_option(std::int32_t value) {
    const int before = shop_.mode;
    if ((value > 100 && value <= 130) || (value > 300 && value <= 330))
        shop_.mode = 0;
    else if ((value > 200 && value <= 213) || (value > 400 && value <= 413))
        shop_.mode = 1;
    return before != shop_.mode;
}

void Screens::draw_shop(Frame &frame, GameSession &session, const GameDecision &decision, const Host &host) {
    auto &shop = shop_;
    const auto now = this->now(frame);
    if (!shop.active) {
        shop = {};
        shop.active = true;
        shop.opened = now;
        for (const auto &option : decision.options) {
            if (option.value > 100 && option.value <= 130)
                shop.cards.push_back(static_cast<std::uint8_t>(option.value - 100));
            if (option.value > 200 && option.value <= 213)
                shop.tools.push_back(static_cast<std::uint8_t>(option.value - 200));
        }
        shop.cards.resize(std::min<std::size_t>(shop.cards.size(), 15));
        shop.cards_bought.assign(shop.cards.size(), false);
        shop.tools_bought.assign(shop.tools.size(), false);
        shop.balloon = shop_lines[0][0];
        shop.balloon_until = now + 2000;
    }
    const int mode = shop.mode;
    const auto say = [&shop, now, mode](int line) {
        shop.balloon = shop_lines[std::size_t(mode)][std::size_t(line)];
        shop.balloon_until = now + 2000;
    };
    if (!shop.balloon.empty() && !shop.balloon_until)
        shop.balloon_until = now + 2000;
    if (shop.leaving && !shop.leave_at)
        shop.leave_at = now + 1500;
    if (!shop.balloon.empty() && now >= shop.balloon_until) {
        if (shop.balloon == shop_lines[0][0] || shop.balloon == shop_lines[1][0])
            say(1);
        else
            shop.balloon.clear();
    }
    const auto leave = option_of(decision, 0);
    if (shop.leaving && shop.leave_at && now >= shop.leave_at && leave) {
        deferred_.push_back([host, index = *leave] { host.respond(index); });
        shop.leave_at = UINT64_MAX;
    }
    const auto &player = session.world().players.at(decision.player).state;
    const std::size_t base = mode ? 16 : 0;
    shot = std::string("shop-") + (mode ? "tool" : "card") + (shop.leaving ? "-leaving" : "") +
           (std::count(shop.cards_bought.begin(), shop.cards_bought.end(), true) +
                    std::count(shop.tools_bought.begin(), shop.tools_bought.end(), true)
                ? "-bought"
                : "");
    opaque(frame, art(frame, 'p', 10, 0 + base), 0, 0);
    keyed(frame, art(frame, 'p', 10, 2 + base), 320, 240);
    // Idle shopkeeper: blink every few seconds, flap the mouth while the balloon is up.
    const auto blink = (now - shop.opened) % 3700;
    if (blink < 400) {
        constexpr std::array<std::size_t, 4> card_blink{7, 8, 7, 5}, tool_blink{21, 22, 21, 23};
        const auto frame_index = (mode ? tool_blink : card_blink)[std::size_t(blink / 100)];
        opaque(frame, art(frame, 'p', 10, frame_index), mode ? 417 : 405, mode ? 50 : 60);
    }
    if (!shop.balloon.empty() && (now / 150) % 2)
        opaque(frame, art(frame, 'p', 10, mode ? 26 : 10), mode ? 417 : 405, mode ? 89 : 91);
    int list_x = 5, grid_x = 227;
    slide(now - shop.opened, list_x, grid_x);
    opaque(frame, art(frame, 'p', 10, 1 + base), list_x, 10);
    const int dx = list_x - 5;
    GameSession *game = &session;
    const auto buy = [this, game, host, &decision](bool card, std::size_t row, std::int32_t value) {
        const auto index = option_of(decision, value);
        return [this, game, host, card, row, value, index] {
            auto &state = game->world().players.at(game->pending_decision()->player).state;
            const auto item = std::size_t(value % 100);
            const auto price = card ? services::card_prices.at(item - 1) : services::tool_prices.at(item - 1);
            const auto mode_lines = shop_lines[card ? 0 : 1];
            const auto complain = [this](const char *line) {
                shop_.balloon = line;
                shop_.balloon_until = 0; // set on the next frame
                if (sound)
                    sound(3);
            };
            const int kind = card ? 0 : 1;
            if (state.points < price)
                complain(mode_lines[2]);
            else if (card ? card_count(state.cards) >= 15 : state.tool_amounts.at(item - 1) >= mod::max_tool_amount)
                complain(mode_lines[3]);
            else if (shop_.pick_kind != kind || shop_.pick_index != row) {
                // Touch: the first tap shows the item in the balloon, a second tap buys it.
                shop_.pick_kind = kind;
                shop_.pick_index = row;
                shop_.pick_value = value;
                shop_.balloon =
                    std::string(card ? services::card_names.at(item - 1) : services::tool_names.at(item - 1)) +
                    "　" + std::to_string(price) + "點\n再點一次就兌換";
                shop_.balloon_until = UINT64_MAX;
                if (sound)
                    sound(1);
            } else if (index && host.respond(*index)) {
                shop_.pick_kind = -1;
                shop_.balloon.clear();
                (card ? shop_.cards_bought : shop_.tools_bought).at(row) = true;
                if (sound)
                    sound(1);
            } else
                host.notify(false, "目前無法兌換");
        };
    };
    if (mode == 0)
        for (std::size_t i = 0; i < shop.cards.size(); ++i) {
            const auto card = shop.cards[i];
            const bool bought = shop.cards_bought[i];
            const int y = int(i) * 24;
            if (shop.pick_kind == 0 && shop.pick_index == i)
                tint(frame.canvas, {14 + dx, 82 + y, 215 + dx, 104 + y});
            centred(frame, services::card_names.at(card - 1), 95 + dx, 93 + y, 20, bought ? grey : white);
            top_right(frame, "$" + std::to_string(services::card_prices.at(card - 1)), 199 + dx, 85 + y, 20,
                      bought ? grey : white);
            if (!bought && option_of(decision, 100 + card))
                frame.hit(option_id + int(*option_of(decision, 100 + card)), {14, 81 + y, 215, 105 + y},
                          buy(true, i, 100 + card));
        }
    else
        for (std::size_t i = 0; i < shop.tools.size(); ++i) {
            const auto tool = shop.tools[i];
            const bool bought = shop.tools_bought[i];
            const int y = int(i) * 48;
            if (shop.pick_kind == 1 && shop.pick_index == i)
                tint(frame.canvas, {12 + dx, 81 + y, 213 + dx, 127 + y});
            centred(frame, services::tool_names.at(tool - 1), 95 + dx, 102 + y, 20, bought ? grey : white);
            top_right(frame, "$" + std::to_string(services::tool_prices.at(tool - 1)), 199 + dx, 94 + y, 20,
                      bought ? grey : white);
            if (!bought && option_of(decision, 200 + tool))
                frame.hit(option_id + int(*option_of(decision, 200 + tool)), {12, 80 + y, 213, 128 + y},
                          buy(false, i, 200 + tool));
        }
    // The player's own grid; a tap sells at once, as in the original.
    keyed(frame, art(frame, 'p', 11, mode ? 1 : 0), grid_x, 293);
    std::vector<std::int32_t> sell_values;
    if (mode == 0) {
        std::vector<std::uint8_t> order;
        card_names_on_grid(frame, player.cards, grid_x, 293, &order);
        for (const auto card : order)
            sell_values.push_back(300 + card);
    } else
        for (const auto tool : tools_on_grid(frame, player, grid_x, 293))
            sell_values.push_back(400 + std::int32_t(tool));
    for (std::size_t tile = 0; tile < sell_values.size(); ++tile) {
        const auto index = option_of(decision, sell_values[tile]);
        if (!index)
            continue;
        const int c = int(tile) % 5, r = int(tile) / 5;
        const Rect rect{233 + 80 * c, 299 + 56 * r, 311 + 80 * c, 353 + 56 * r};
        if (frame.pointer_down && frame.pointer_in(rect))
            sink(frame.canvas, rect);
        const int kind = mode ? 3 : 2;
        if (shop.pick_kind == kind && shop.pick_index == tile) {
            ui::border(frame.canvas, rect, ui::color(255, 255, 0));
            ui::border(frame.canvas, {rect.left + 1, rect.top + 1, rect.right - 1, rect.bottom - 1},
                       ui::color(255, 255, 0));
        }
        const auto owned = mode ? player.tool_amounts.at(std::size_t(sell_values[tile] % 100) - 1) : 1;
        frame.hit(option_id + int(*index), rect,
                  [this, host, kind, tile, owned, index = *index, value = sell_values[tile]] {
                      if (shop_.pick_kind != kind || shop_.pick_index != tile) {
                          // Touch: the first tap names the price, a second tap sells.
                          const auto item = std::size_t(value % 100);
                          const bool card = kind == 2;
                          const auto price = card ? services::card_prices.at(item - 1)
                                                  : services::tool_prices.at(item - 1) * owned;
                          shop_.pick_kind = kind;
                          shop_.pick_index = tile;
                          shop_.pick_value = value;
                          shop_.balloon = "賣出" +
                                          std::string(card ? services::card_names.at(item - 1)
                                                           : services::tool_names.at(item - 1)) +
                                          "\n可得" + std::to_string(price * 9 / 10) + "點，再點一次";
                          shop_.balloon_until = UINT64_MAX;
                          if (sound)
                              sound(1);
                          return;
                      }
                      shop_.pick_kind = -1;
                      shop_.balloon.clear();
                      if (host.respond(index) && sound)
                          sound(1);
                  });
    }
    // Native-only help (the original shows item descriptions only in its Help screen; on a phone there
    // is no hover either): while an item is picked, its help.mkf description covers the side the player
    // is not choosing from -- the hand grid for a shelf pick, the shelf for a hand pick. A tap on the
    // strip cancels the pick; it takes precedence over the controls it covers.
    if (shop.pick_kind >= 0) {
        const bool buying = shop.pick_kind < 2, card = shop.pick_kind % 2 == 0;
        const auto item = unsigned(shop.pick_value % 100);
        const Rect strip = buying ? Rect{grid_x + 2, 295, grid_x + 410, 471}
                                  : Rect{list_x + 4, 236, list_x + 222, 470}; // below the balloon
        ui::fill(frame.canvas, strip, ui::paper);
        ui::border(frame.canvas, strip, ui::color(110, 74, 26));
        frame.text(card ? services::card_names.at(item - 1) : services::tool_names.at(item - 1),
                   strip.left + 10, strip.top + 8, 18, ui::blue);
        frame.text(description(frame.artwork, card, item), strip.left + 10, strip.top + 36, 16, ui::ink,
                   strip.right - strip.left - 20);
        frame.text("點此取消", strip.right - 74, strip.bottom - 24, 14, ui::color(90, 80, 60));
        frame.buttons.insert(frame.buttons.begin(), Button{282, strip, true, [this] {
                                                               shop_.pick_kind = -1;
                                                               shop_.balloon.clear();
                                                           }});
    }
    // Switch shop, EXIT and points box.
    const Rect switch_rect{542, 13, 627, 98}, exit_rect{556, 246, 636, 286};
    const bool switch_down = frame.pointer_down && frame.pointer_in(switch_rect);
    keyed(frame, art(frame, 'p', 10, (mode ? 29 : 13) + (switch_down ? 1 : 0)), 542, 13);
    const bool exit_down = shop.leaving || (frame.pointer_down && frame.pointer_in(exit_rect));
    keyed(frame, art(frame, 'p', 10, exit_down ? 36 : 35), 556, 246);
    keyed(frame, art(frame, 'p', 10, 37), 230, 246);
    top_right(frame, std::to_string(player.points), 310, 257, 20, white);
    if (shop.leaving)
        keyed(frame, art(frame, 'p', 10, mode ? 19 : 3), 320, 240);
    if (const auto voice = voices::clerk_voice(shop.balloon)) // AUDIO: shop clerks #0000..#0010
        voice_once("shop:" + shop.balloon, {*voice});
    else
        voice_once(shop.balloon.empty() ? "" : "shop:" + shop.balloon, {});
    if (!shop.balloon.empty()) {
        keyed(frame, art(frame, 'p', 10, 15), 120, 10);
        centred(frame, shop.balloon, 235, 119, 20, dark, false, dark, true);
        if (!shop.leaving) // a tap on the balloon dismisses it, as a click did in the original
            frame.hit(281, {150, 30, 360, 210}, [this] {
                shop_.balloon.clear();
                shop_.pick_kind = -1;
            });
    }
    if (shop.leaving) {
        if (leave)
            frame.hit(option_id + int(*leave), {0, 0, 640, 480}, [host, index = *leave] { host.respond(index); });
        return;
    }
    frame.hit(shop_switch_id, switch_rect, [this] {
        shop_.mode ^= 1;
        shop_.pick_kind = -1;
        if (!shop_.balloon.empty() && shop_.balloon != shop_lines[0][0] && shop_.balloon != shop_lines[1][0])
            shop_.balloon.clear();
    });
    if (leave)
        frame.hit(option_id + int(*leave), exit_rect, [this, mode] {
            shop_.pick_kind = -1;
            shop_.leaving = true;
            shop_.leave_at = 0;
            shop_.balloon = shop_lines[std::size_t(mode)][4];
            shop_.balloon_until = 0;
        });
}

// §7 magic house.
namespace {
constexpr std::array<const char *, 12> magic_groups{"財產最多的人", "土地最多的人", "房屋最多的人", "現金最多的人",
                                                    "存款最多的人", "點券最多的人", "走路的人",     "騎機車的人",
                                                    "開汽車的人",   "神明附身的人", "所有男生",     "所有女生"};
struct MagicEffect {
    const char *text;
    std::size_t plate;
    int label_x, label_y, icon_x, icon_y;
};
constexpr std::array<MagicEffect, 12> magic_table{{{"變賣所有卡片", 9, 208, 167, 322, 91},
                                                   {"抽取命運三張", 10, 510, 150, 414, 82},
                                                   {"立刻坐牢三天", 7, 545, 88, 453, 157},
                                                   {"原地停留一回合", 7, 568, 147, 509, 242},
                                                   {"存入所有現金", 7, 550, 234, 458, 314},
                                                   {"就地加蓋房屋", 7, 510, 320, 415, 387},
                                                   {"得一張卡片", 7, 422, 320, 322, 394},
                                                   {"向後轉", 6, 134, 318, 239, 393},
                                                   {"變賣所有道具", 6, 92, 250, 188, 316},
                                                   {"就地拆除房屋", 6, 72, 130, 131, 222},
                                                   {"住院檢查三天", 6, 77, 85, 184, 161},
                                                   {"拍賣當格土地", 9, 122, 154, 225, 83}}};
} // namespace

void Screens::draw_magic(Frame &frame, GameSession &session, const GameDecision &decision, const Host &host) {
    (void)session;
    const auto now = this->now(frame);
    if (!magic_.active) {
        magic_ = {};
        magic_.active = true;
        magic_.opened = now;
    }
    if (magic_mask_.empty()) {
        try {
            magic_mask_ = frame.artwork.archive('p').decode(19);
        } catch (const std::exception &) {
        }
    }
    opaque(frame, art(frame, 'p', 18, 0), 0, 0);
    const auto balloon = [&frame](std::string_view text) {
        keyed(frame, art(frame, 'p', 18, 8), 320, 384);
        centred(frame, text, 320, 384, 18, ui::color(250, 240, 210));
    };
    if (decision.kind == DecisionKind::magic_category) {
        // The original witch draws the group at random (§7.3); the native rules let the player
        // choose it, so the twelve group icons (18:11-22) are offered on a plate instead.
        shot = "magic-category";
        voice_once("magic-category", {37}); // AUDIO: 進來魔法屋，就得完全照我的指示！
        keyed(frame, art(frame, 'p', 18, 1), 241, 140);
        const Rect panel{18, 300, 622, 474};
        ui::fill(frame.canvas, panel, ui::color(74, 34, 12));
        ui::border(frame.canvas, panel, ui::color(232, 196, 96));
        ui::border(frame.canvas, {panel.left + 3, panel.top + 3, panel.right - 3, panel.bottom - 3},
                   ui::color(150, 96, 40));
        centred(frame, "進來魔法屋，就得完全照我的指示！請選出施法的對象：", 320, 318, 16, ui::color(250, 240, 210));
        for (int group = 0; group < 12; ++group) {
            const int x = panel.left + 50 + (group % 6) * 100, y = panel.top + 66 + (group / 6) * 70;
            const Rect cell{x - 48, y - 34, x + 48, y + 34};
            const auto index = option_of(decision, group);
            keyed(frame, art(frame, 'p', 18, std::size_t(11 + group)), x, y - 10);
            centred(frame, magic_groups[std::size_t(group)], x, y + 25, 13,
                    index ? ui::color(250, 240, 210) : ui::color(120, 100, 80));
            if (!index) {
                for (int yy = cell.top; yy < cell.bottom; ++yy) // dim groups with nobody in them
                    for (int xx = cell.left; xx < cell.right; ++xx) {
                        auto &pixel = frame.canvas.pixels[std::size_t(yy * frame.canvas.width + xx)];
                        pixel = static_cast<std::uint16_t>((pixel >> 1) & 0x3def);
                    }
                continue;
            }
            if (frame.pointer_down && frame.pointer_in(cell))
                sink(frame.canvas, cell);
            frame.hit(option_id + int(*index), cell, [this, host, group, index = *index] {
                magic_.category = group;
                host.respond(index);
            });
        }
        return;
    }
    // Effect choice: tap a triangle to light it and read its label, tap it again to cast.
    const auto group = magic_.category;
    const auto respond_effect = [this, host, &decision](int effect) {
        if (const auto index = option_of(decision, effect)) {
            magic_ = {};
            host.respond(*index);
        }
    };
    const auto schedule_effect = [this, host, &decision](int effect) {
        if (const auto index = option_of(decision, effect); index && !magic_.scheduled) {
            magic_.scheduled = true;
            deferred_.push_back([this, host, index = *index] {
                magic_ = {};
                host.respond(index);
            });
        }
    };
    shot = magic_.casting ? "magic-casting" : magic_.selected >= 0 ? "magic-selected" : "magic-effect";
    if (magic_.casting) {
        const int effect = magic_.selected;
        // #0041 cast balloon for 1 s, then the Panel 20 hexagram movie with sound 0x3b
        // (magic_house.asm:1175) before the effect applies; a tap skips both.
        if (magic_.casting > 1 && now >= magic_.casting + 1000) {
            shot = "magic-movie";
            if (!draw_cast_movie(frame, magic_.movie, now - magic_.casting - 1000))
                schedule_effect(effect);
        } else {
            keyed(frame, art(frame, 'p', 18, 2), 182, 142);
            if (group >= 0 && group < 12)
                keyed(frame, art(frame, 'p', 18, std::size_t(11 + group)), 326, 296);
            balloon("天靈靈地靈靈～");
            voice_once("magic-cast", {41}); // AUDIO
        }
        if (const auto index = option_of(decision, effect))
            frame.hit(option_id + int(*index), {0, 0, 640, 480}, [respond_effect, effect] { respond_effect(effect); });
        return;
    }
    keyed(frame, art(frame, 'p', 18, 1), 241, 140);
    if (group >= 0 && group < 12) {
        keyed(frame, art(frame, 'p', 18, std::size_t(11 + group)), 326, 296);
        // AUDIO: the witch names the group, then 嘿～輪到你了！ (#0046+group, #0040)
        voice_once("magic-effect" + std::to_string(group), {voices::magic_group_voice(group), 40});
    }
    const auto tap = [this](int effect) {
        if (effect < 0 || effect > 11)
            return;
        if (magic_.selected == effect) {
            magic_.casting = 1; // stamped with the frame time below
            return;
        }
        magic_.selected = effect;
        if (sound)
            sound(0x27);
    };
    // The balloon would cover the lower icons once the choice is open, so the prompt sits on a
    // small plate over the MAGIC lettering instead (touch has no hover to reveal the labels).
    const auto prompt = [&frame](std::string_view text) {
        const Rect plate{150, 444, 490, 476};
        ui::fill(frame.canvas, plate, ui::color(74, 34, 12));
        ui::border(frame.canvas, plate, ui::color(232, 196, 96));
        centred(frame, text, 320, 460, 16, ui::color(250, 240, 210));
    };
    if (magic_.selected >= 0) {
        const auto &effect = magic_table[std::size_t(magic_.selected)];
        keyed(frame, art(frame, 'p', 18, std::size_t(23 + magic_.selected)), effect.icon_x, effect.icon_y);
        keyed(frame, art(frame, 'p', 18, effect.plate), effect.label_x, effect.label_y);
        centred(frame, effect.text, effect.label_x, effect.label_y, 14, ui::color(224, 224, 224), true,
                ui::color(32, 32, 32));
        prompt(std::string(effect.text) + "？再點一次就施法");
    } else
        prompt(std::string(group >= 0 && group < 12 ? magic_groups[std::size_t(group)] : "") +
               "：嘿～輪到你了！請點選魔法");
    for (std::size_t i = 0; i < decision.options.size(); ++i) {
        const auto value = decision.options[i].value;
        if (value < 0 || value > 11)
            continue;
        const auto &effect = magic_table[std::size_t(value)];
        frame.hit(option_id + int(i), {effect.icon_x - 24, effect.icon_y - 24, effect.icon_x + 24, effect.icon_y + 24},
                  [tap, value] { tap(value); });
    }
    if (const auto reselect = option_of(decision, -1))
        frame.button(option_id + int(*reselect), {520, 440, 630, 472}, "重選對象", [host, index = *reselect] {
            host.respond(index);
        });
    frame.hit(magic_mask_id, {0, 0, 640, 480}, [this, &frame, tap] {
        const int x = frame.pointer_x, y = frame.pointer_y;
        if (x < 0 || y < 0 || x >= 640 || y >= 480 || magic_mask_.size() < 640U * 480U)
            return;
        const int value = magic_mask_[std::size_t(y * 640 + x)];
        if (value >= 1 && value <= 12)
            tap(value - 1);
        else if (value == 13)
            magic_.selected = -1;
    });
    if (magic_.casting == 1)
        magic_.casting = now;
}

// Panel 20: 25-frame 640x480 FLC drawn opaque at (0,0) (fcn_0045144f flags 0). Returns false once
// it has ended (its last, white frame stays on screen until the effect is answered).
bool Screens::draw_cast_movie(Frame &frame, std::shared_ptr<detail::CastMovie> &state, std::uint64_t elapsed) {
    try {
        if (!state)
            state = std::make_shared<detail::CastMovie>(
                detail::CastMovie{flc::Decoder(frame.artwork.archive('p').decode(20)), {}, false});
        auto &movie = *state;
        const auto delay = std::max<std::uint64_t>(movie.decoder.delay_ms(), 1);
        const auto count = std::max<std::size_t>(movie.decoder.frame_count(), 1);
        const auto index = std::size_t(std::min<std::uint64_t>(elapsed / delay, count - 1));
        while (movie.decoder.decoded_frames() <= index && movie.decoder.next())
            movie.image = movie.decoder.image();
        if (!movie.sounded) {
            movie.sounded = true;
            if (sound)
                sound(0x3b);
        }
        if (!movie.image.pixels.empty())
            opaque(frame, movie.image, 0, 0);
        return elapsed < delay * count;
    } catch (const std::exception &error) { // a broken asset must never block the effect
        std::cerr << "Magic-house movie skipped: " << error.what() << '\n';
        return false;
    }
}

void Screens::start_magic_cast(int group, int effect) {
    if (group >= 0 && group < 12 && effect >= 0 && effect < 12)
        cast_ = MagicCast{group, effect, 0, nullptr};
}

void Screens::draw_magic_cast(Frame &frame) {
    if (!cast_)
        return;
    const auto now = this->now(frame);
    if (!cast_->started)
        cast_->started = std::max<std::uint64_t>(now, 1);
    const auto elapsed = now - cast_->started;
    shot = "magic-computer";
    bool playing = true;
    if (elapsed < 1500) {
        opaque(frame, art(frame, 'p', 18, 0), 0, 0);
        keyed(frame, art(frame, 'p', 18, 2), 182, 142);
        keyed(frame, art(frame, 'p', 18, std::size_t(11 + cast_->group)), 326, 296);
        keyed(frame, art(frame, 'p', 18, 8), 320, 384);
        centred(frame,
                std::string(magic_groups[std::size_t(cast_->group)]) + "\n\n" +
                    magic_table[std::size_t(cast_->effect)].text,
                320, 384, 18, ui::color(250, 240, 210));
    } else {
        shot = "magic-computer-movie";
        playing = draw_cast_movie(frame, cast_->movie, elapsed - 1500);
    }
    frame.hit(magic_cast_skip_id, {0, 0, 640, 480}, [this] { cast_.reset(); });
    if (!playing)
        cast_.reset();
}

// §8 prison and hospital bail screens.
namespace {
constexpr std::array<const char *, 4> npc_names{"小偷", "強盜", "流氓", "間諜"};
// _special_player_strings (rich4.asm:41232): what a bailed-out NPC 4..7 says (#0124 #0125 #0123 #0126).
constexpr std::array<const char *, 4> npc_thanks{"謝謝你！你真是\n我的再生父母！", "我先走了！大恩\n大德來日再報！",
                                                 "太感激了！我一\n定會報答你的！", "受人點水之恩，\n必當湧泉已報！"};
}

void Screens::draw_rescue(Frame &frame, GameSession &session, const GameDecision &decision, const Host &host) {
    const auto now = this->now(frame);
    if (!rescue_.active) {
        rescue_ = {};
        rescue_.active = true;
        rescue_.opened = now;
    }
    const auto &world = session.world();
    const bool hospital = decision.prompt.find("住院") != std::string::npos;
    const auto points = world.players.at(decision.player).state.points;
    const std::size_t entry = hospital ? 65 : 63;
    shot = std::string(hospital ? "hospital" : "prison") + (rescue_.selected >= 0 ? "-selected" : "");
    opaque(frame, art(frame, 'p', entry, 0), 0, 0);
    if (hospital)
        keyed(frame, art(frame, 'p', 65, 4), 104, 110);
    const auto slot_origin = [hospital](int slot) {
        if (hospital)
            return std::pair{slot < 4 ? 297 : 481, 1 + 120 * (slot % 4)};
        constexpr std::array<int, 4> xs{33, 185, 336, 487};
        return std::pair{xs[std::size_t(slot % 4)], slot < 4 ? 24 : 183};
    };
    const auto name_of = [&world](int slot) -> std::string {
        if (slot >= 4)
            return npc_names[std::size_t(slot - 4)];
        return names.at(world.players.at(std::size_t(slot)).state.character);
    };
    // A bailed-out NPC thanks the player before the screen closes (prison.asm:765-806,
    // hospital.asm:505-551); a player slot closes at once, as in the original.
    const auto confirm = [this, host, hospital](int slot, std::size_t index) {
        rescue_voice(hospital, slot); // AUDIO: nurse lines / rescued-NPC thanks
        if (slot < 4) {
            host.respond(index);
            return;
        }
        rescue_.selected = -1;
        rescue_.thanks = slot;
        rescue_.thanks_until = 1; // stamped with the frame time below
    };
    const bool thanking = rescue_.thanks >= 4 && rescue_.thanks < 8;
    for (int slot = 0; slot < 8; ++slot) {
        const auto [x, y] = slot_origin(slot);
        const auto index = rescue_.thanks == slot ? std::nullopt : option_of(decision, slot);
        if (index) {
            const auto character = slot < 4 ? world.players.at(std::size_t(slot)).state.character : 0;
            const std::size_t portrait = slot < 4 ? (hospital ? 14U : 5U) + character
                                                  : (hospital ? 22U : 13U) + std::size_t(slot);
            keyed(frame, art(frame, 'p', entry, portrait), x, y);
            if (!hospital)
                keyed(frame, art(frame, 'p', 63, 3), x, y);
            const Rect rect = hospital ? Rect{x, y, x + 147, y + 102} : Rect{x, y, x + 121, y + 137};
            if (rescue_.selected == slot) {
                ui::border(frame.canvas, rect, ui::color(255, 255, 0));
                ui::border(frame.canvas, {rect.left + 1, rect.top + 1, rect.right - 1, rect.bottom - 1},
                           ui::color(255, 255, 0));
            }
            if (!thanking) {
                frame.hit(option_id + int(*index), rect, [this, confirm, slot, points, index = *index] {
                    if (points < (slot < 4 ? 30 : 300)) {
                        rescue_.selected = -1;
                        rescue_.poor_until = 1;
                        if (sound)
                            sound(3);
                    } else if (rescue_.selected == slot)
                        confirm(slot, index);
                    else {
                        rescue_.selected = slot;
                        rescue_.poor_until = 0;
                    }
                });
            }
        } else if (!hospital)
            keyed(frame, art(frame, 'p', 63, 4), x, y);
    }
    // Points box.
    keyed(frame, art(frame, 'p', entry, hospital ? 30 : 21), hospital ? 8 : 542, 432);
    right_middle(frame, std::to_string(points), hospital ? 88 : 622, 452, 20, white);
    if (thanking) { // Panel 64 big NPC art and its thank-you balloon (63:1 / 65:1)
        shot = std::string(hospital ? "hospital" : "prison") + "-thanks";
        const auto slot = rescue_.thanks;
        if (rescue_.thanks_until == 1)
            rescue_.thanks_until = now + 2500;
        keyed(frame, art(frame, 'p', 64, std::size_t(slot - 4)), hospital ? 420 : 365, 450);
        const int bx = hospital ? 200 : 210, by = hospital ? 200 : 150;
        keyed(frame, art(frame, 'p', entry, 1), bx, by);
        centred(frame, npc_thanks[std::size_t(slot - 4)], bx + 92, by + 34, 18, dark, false, dark, true);
        if (const auto index = option_of(decision, slot)) {
            const auto finish = [this, host, index = *index] {
                rescue_ = {};
                host.respond(index);
            };
            if (now >= rescue_.thanks_until && !rescue_.thanks_scheduled) {
                rescue_.thanks_scheduled = true;
                deferred_.push_back(finish);
            }
            frame.hit(option_id + int(*index), {0, 0, 640, 480}, finish); // a tap continues at once
        }
        return;
    }
    if (hospital) {
        keyed(frame, art(frame, 'p', 65, 2), 8, 8);
        centred(frame, "您好！請問您要替誰\n辦理出院手續？", 148, 56, 18, dark, false, dark, true);
        if (voiced_.empty())
            voice_once("hospital", {127}); // AUDIO: nurse #0127, once per visit
    }
    if (rescue_.poor_until == 1)
        rescue_.poor_until = now + 2000;
    if (rescue_.poor_until > now) {
        keyed(frame, art(frame, 'p', entry, 1), 230, 300);
        centred(frame, "抱歉！\n你的點數不足！", 322, 334, 18, dark, false, dark, true);
        voice_once("poor" + std::to_string(rescue_.poor_until), {2}); // AUDIO: #0002 (ui_prison/hospital)
    }
    const auto leave = option_of(decision, -1);
    if (rescue_.selected >= 0) {
        const auto slot = rescue_.selected;
        const auto [x, y] = slot_origin(slot);
        const int tx = hospital ? x - 80 : x + 20, ty = hospital ? y : y + 120;
        keyed(frame, art(frame, 'p', entry, hospital ? 3 : 2), tx, ty);
        const int text_x = tx + (hospital ? 41 : 57);
        centred(frame, name_of(slot), text_x, ty + 26, 16, dark, false, dark, true);
        centred(frame, "保釋點數", text_x, ty + 48, 16, dark, false, dark, true);
        centred(frame, std::to_string(slot < 4 ? 30 : 300) + "點", text_x, ty + 70, 16, dark, false, dark, true);
        // _rich4_ui_yesno(320,240) (ui_hospital.asm:1313, ui_prison.asm:699): the YES/NO strip alone.
        if (const auto index = option_of(decision, slot))
            messages::draw_yes_no_strip(frame, 320, 240, yes_id, [confirm, slot, index = *index] { confirm(slot, index); },
                                        no_id, [this] { rescue_.selected = -1; });
    }
    if (leave) {
        // The original leaves on a right-click; a phone has none, so the game's own EXIT button
        // (Panel 10:35, pressed 10:36, as in the shops) stands in for it.
        const Rect exit = hospital ? Rect{150, 436, 230, 476} : Rect{55, 436, 135, 476};
        const bool down = frame.pointer_down && frame.pointer_in(exit);
        keyed(frame, art(frame, 'p', 10, down ? 36 : 35), exit.left, exit.top);
        frame.hit(option_id + int(*leave), exit, [host, index = *leave] { host.respond(index); });
    }
    frame.hit(63, {0, 0, 640, 480}, [this] { rescue_.selected = -1; });
}

// ---------------------------------------------------------------------------------------------
// Developer gallery: every screen above with synthetic state, for visual comparison with the spec.
void Screens::gallery(Frame &frame, const World &base, const std::function<void()> &background,
                      const std::function<void(const std::string &)> &save) {
    World world = base;
    auto &player = world.players.at(world.header.current_player).state;
    player.cards.fill(0);
    constexpr std::array<std::uint8_t, 13> hand{1, 4, 9, 14, 14, 17, 13, 20, 22, 26, 28, 30, 11};
    std::copy(hand.begin(), hand.end(), player.cards.begin());
    for (std::size_t i = 0; i < 13; ++i)
        player.tool_amounts[i] = static_cast<std::uint8_t>(i % 4 + 1);
    player.traffic_method = 2;
    player.points = 480;
    world.prison_roster = {0, 3, 0, 0, 0, 2, 0, 4};
    world.hospital_roster = {2, 0, 0, 0, 3, 0, 0, 1};
    GameSession game(world, 1);
    const Host host{[] {}, [](bool, std::string_view) {}, [](std::size_t) { return false; }};
    const auto shoot = [&](const std::string &name, const std::function<void()> &draw) {
        const auto shop = shop_;
        const auto magic = magic_;
        const auto rescue = rescue_;
        background(); // the board ends visits when no decision is pending
        shop_ = shop;
        magic_ = magic;
        rescue_ = rescue;
        frame.buttons.clear();
        draw();
        save(name);
    };
    const auto saved_clock = clock;
    clock = 100000;
    frame.pointer_x = frame.pointer_y = -1;
    frame.pointer_down = false;
    shoot("cards", [&] { draw_inventory(frame, game, true, host); });
    shoot("cards-pressed", [&] {
        press_ = Press{"cards", grid_tile(14, 130, 2), [] {}, 9};
        frame.pointer_down = true;
        draw_inventory(frame, game, true, host);
        frame.pointer_down = false;
        press_.reset();
    });
    shoot("tools", [&] { draw_inventory(frame, game, false, host); });
    shoot("tools-pressed", [&] {
        press_ = Press{"tools", grid_tile(14, 130, 6), [] {}, 7};
        frame.pointer_down = true;
        draw_inventory(frame, game, false, host);
        frame.pointer_down = false;
        press_.reset();
    });
    for (const std::uint8_t card : {1, 9, 14, 17, 20}) {
        popups_.push_back({card, *clock + 1000, {}});
        shoot("popup-" + std::to_string(card), [&] { draw_popup(frame); });
        popups_.clear();
    }
    // Shop: the same option layout as the core's shop prompt.
    GameDecision shop{DecisionKind::shop, world.header.current_player, 0, 0, "", {}};
    for (const std::uint8_t card : {7, 5, 25, 12, 11, 19, 16, 4, 16, 12, 7})
        shop.options.push_back({"", 100 + card});
    for (int tool = 1; tool <= 8; ++tool)
        shop.options.push_back({"", 200 + tool});
    for (const auto card : hand)
        if (!option_of(shop, 300 + card))
            shop.options.push_back({"", 300 + card});
    for (int tool = 1; tool <= 13; ++tool)
        shop.options.push_back({"", 400 + tool});
    shop.options.push_back({"", 0});
    for (int mode = 0; mode < 2; ++mode) {
        shop_ = {};
        draw_shop(frame, game, shop, host); // initialise the visit
        shop_.opened = 0;
        shop_.mode = mode;
        shop_.balloon.clear();
        (mode ? shop_.tools_bought : shop_.cards_bought).at(1) = true;
        shoot(mode ? "shop-tool" : "shop-card", [&] { draw_shop(frame, game, shop, host); });
        shop_.balloon = shop_lines[std::size_t(mode)][1];
        shop_.balloon_until = *clock + 1000;
        shoot(mode ? "shop-tool-balloon" : "shop-card-balloon", [&] { draw_shop(frame, game, shop, host); });
    }
    shop_.mode = 0;
    shop_.pick_kind = 0;
    shop_.pick_index = 3;
    shop_.pick_value = 112;
    shop_.balloon = "拆除卡　15點\n再點一次就兌換";
    shop_.balloon_until = UINT64_MAX;
    shoot("shop-card-picked", [&] { draw_shop(frame, game, shop, host); });
    shop_.pick_kind = 2;
    shop_.pick_index = 1;
    shop_.pick_value = 304;
    shop_.balloon = "賣出換地卡\n可得22點，再點一次";
    shoot("shop-card-sell-picked", [&] { draw_shop(frame, game, shop, host); });
    shop_.pick_kind = -1;
    shop_.leaving = true;
    shop_.leave_at = *clock + 1000;
    shop_.balloon = shop_lines[0][4];
    shop_.mode = 0;
    shoot("shop-leaving", [&] { draw_shop(frame, game, shop, host); });
    shop_ = {};
    // Magic house.
    GameDecision category{DecisionKind::magic_category, world.header.current_player, 0, 0, "", {}};
    for (const int group : {0, 1, 2, 3, 4, 5, 6, 9, 10, 11})
        category.options.push_back({"", group});
    shoot("magic-category", [&] { draw_magic(frame, game, category, host); });
    GameDecision effect{DecisionKind::magic_effect, world.header.current_player, 0, 0, "", {}};
    for (int i = 0; i < 12; ++i)
        effect.options.push_back({"", i});
    effect.options.push_back({"", -1});
    magic_.category = 3;
    shoot("magic-effect", [&] { draw_magic(frame, game, effect, host); });
    magic_.selected = 10;
    shoot("magic-selected", [&] { draw_magic(frame, game, effect, host); });
    magic_.casting = *clock;
    shoot("magic-casting", [&] { draw_magic(frame, game, effect, host); });
    for (const int movie_ms : {300, 900, 1500}) { // Panel 20 cast movie (frames 4, 12, 21)
        magic_.casting = *clock - 1000 - std::uint64_t(movie_ms);
        magic_.movie.reset();
        shoot("magic-movie-" + std::to_string(movie_ms), [&] { draw_magic(frame, game, effect, host); });
    }
    magic_ = {};
    for (const int at : {500, 2400}) { // a computer's cast: group/effect balloon, then the movie
        start_magic_cast(3, 10);
        cast_->started = *clock - std::uint64_t(at);
        shoot(at < 1500 ? "magic-computer" : "magic-computer-movie", [&] { draw_magic_cast(frame); });
    }
    cast_.reset();
    // Remote dice (Panel 72) with face 4 pressed, and the 嫁禍卡 chooser (Data 518).
    GameDecision dice{DecisionKind::action_target, world.header.current_player, 0, 0, "选择道具目标", {}};
    for (int value = 1; value <= 6; ++value)
        dice.options.push_back({std::to_string(value) + "点", value});
    dice.options.push_back({"取消", 0});
    shoot("remote-dice", [&] { draw_remote_dice(frame, dice, host); });
    frame.pointer_x = 104 + 40 * 3 + 15;
    frame.pointer_y = 328;
    frame.pointer_down = true;
    shoot("remote-dice-pressed", [&] { draw_remote_dice(frame, dice, host); });
    frame.pointer_x = frame.pointer_y = -1;
    frame.pointer_down = false;
    for (const std::size_t victims : {1U, 2U, 3U}) {
        GameDecision frame_up{DecisionKind::card_target, world.header.current_player, 0, 3000, "使用嫁祸卡？", {}};
        for (std::size_t i = 0; i < world.players.size() && frame_up.options.size() < victims; ++i)
            if (i != world.header.current_player)
                frame_up.options.push_back({"", std::int32_t(i)});
        frame_up.options.push_back({"保留卡片", -1});
        shoot("frame-up-" + std::to_string(victims), [&] { draw_frame_up(frame, game, frame_up, host); });
    }
    { // 搶奪卡 picker over a victim's cards and tools, then with 地雷 selected.
        GameDecision steal{DecisionKind::action_target, world.header.current_player, 0, 0, "請選擇要搶的卡片或道具", {}};
        steal.victim = world.header.current_player;
        for (const auto card : player.cards)
            if (card && !option_of(steal, std::int32_t((unsigned(CardTargetKind::card) << 16U) | card)))
                steal.options.push_back({"", std::int32_t((unsigned(CardTargetKind::card) << 16U) | card)});
        for (unsigned tool = 1; tool <= 13; ++tool)
            steal.options.push_back({"", std::int32_t((unsigned(CardTargetKind::tool) << 16U) | tool)});
        steal.options.push_back({"取消", -1});
        shoot("steal", [&] { draw_steal(frame, game, steal, host); });
        steal_pick_ = option_of(steal, std::int32_t((unsigned(CardTargetKind::tool) << 16U) | 3U));
        shoot("steal-selected", [&] { draw_steal(frame, game, steal, host); });
        steal_pick_.reset();
    }
    // Bail screens.
    for (const bool hospital : {false, true}) {
        GameDecision rescue{DecisionKind::rescue, world.header.current_player, 0, 0,
                            hospital ? "探望住院角色" : "探望入狱角色", {}};
        const auto &roster = hospital ? world.hospital_roster : world.prison_roster;
        for (int i = 0; i < 8; ++i)
            if (roster[std::size_t(i)] && (i >= 4 || std::size_t(i) < world.players.size()))
                rescue.options.push_back({"", i});
        rescue.options.push_back({"", -1});
        const std::string name = hospital ? "hospital" : "prison";
        rescue_ = {};
        shoot(name, [&] { draw_rescue(frame, game, rescue, host); });
        rescue_.selected = hospital ? 4 : 5;
        shoot(name + "-selected", [&] { draw_rescue(frame, game, rescue, host); });
        rescue_.selected = -1;
        rescue_.poor_until = *clock + 1000;
        shoot(name + "-poor", [&] { draw_rescue(frame, game, rescue, host); });
        rescue_ = {};
        for (const int npc : {4, 5, 6, 7}) { // Panel 64 thank-you of each bailed-out NPC
            rescue_ = {};
            draw_rescue(frame, game, rescue, host); // initialise the visit
            rescue_.thanks = npc;
            rescue_.thanks_until = *clock + 1000;
            shoot(name + "-thanks-" + std::to_string(npc), [&] { draw_rescue(frame, game, rescue, host); });
        }
        rescue_ = {};
    }
    clock = saved_clock;
}

} // namespace rich4::native::items
