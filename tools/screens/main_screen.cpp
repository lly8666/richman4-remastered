// SPDX-License-Identifier: GPL-3.0-or-later
// See docs/ui/main-screen.md for the original layout; section numbers refer to it.
#include "main_screen.hpp"
#include "draw.hpp"

#include "rich4/calendar.hpp"
#include "rich4/flc.hpp"
#include "rich4/holidays.hpp"
#include "rich4/stocks.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <string>

namespace rich4::native {
namespace {

constexpr auto text_ink = ui::color(0x10, 0x10, 0x10);
constexpr auto white = ui::color(255, 255, 255);
constexpr auto red = ui::color(255, 0, 0);
constexpr graphics::Rect board_area{0, 40, 440, 480};

std::vector<std::string_view> characters(std::string_view text) {
    std::vector<std::string_view> result;
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        const std::size_t length = lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : lead >= 0xc0 ? 2 : 1;
        result.push_back(text.substr(i, std::min(length, text.size() - i)));
        i += length;
    }
    return result;
}

enum class Align { left, right, center, vertical };

// _rich4_draw_text: anchors the ink bounding box. spacing is the original value (extra pixels =
// spacing - 1). An outline colour selects style 6 (bold, 1-px outline, fg at +1,+1).
struct Text {
    int size = 12;
    std::uint16_t colour = text_ink;
    int spacing = 1;
    std::optional<std::uint16_t> outline{};
    bool bold = false;
};

void run(Frame &frame, std::string_view value, int x, int y, const Text &style, std::uint16_t colour) {
    for (const auto ch : characters(value)) {
        // Weight 700 (and large MingLiU, which shows 2-px stems in the reference shots) is emulated by
        // over-striking; the stroke widens with the size.
        const int strikes = style.bold ? std::max(1, style.size / 20) : style.size >= 18 ? 1 : 0;
        for (int dx = 0; dx <= strikes; ++dx)
            frame.font.draw(frame.canvas, ch, x + dx, y, style.size, colour, 0, true);
        x += frame.font.measure(ch, style.size, true) + style.spacing - 1;
    }
}

void styled(Frame &frame, std::string_view value, int x, int y, const Text &style) {
    if (style.outline) {
        for (int dy = 0; dy < 3; ++dy)
            for (int dx = 0; dx < 3; ++dx)
                if (dx != 1 || dy != 1)
                    run(frame, value, x + dx - 1, y + dy - 1, style, *style.outline);
    }
    run(frame, value, x, y, style, style.colour);
}

void text(Frame &frame, std::string_view value, int x, int y, Align align, const Text &style = {}) {
    if (align == Align::vertical) {
        const auto glyphs = characters(value);
        const int advance = style.size + style.spacing + (style.outline || style.bold ? 1 : 0);
        int top = y - (int(glyphs.size()) * advance - style.spacing) / 2;
        for (const auto ch : glyphs) {
            const auto ink = frame.font.ink_bounds(ch, style.size, 0, true);
            styled(frame, ch, x - (ink.left + ink.right) / 2, top + (style.size - ink.bottom - ink.top) / 2, style);
            top += advance;
        }
        return;
    }
    const auto ink = frame.font.ink_bounds(value, style.size, style.spacing - 1, true);
    const int origin_y = align == Align::center ? y - (ink.top + ink.bottom) / 2 : y - ink.top;
    const int origin_x = align == Align::left    ? x - ink.left
                         : align == Align::right ? x - ink.right
                                                 : x - (ink.left + ink.right) / 2;
    styled(frame, value, origin_x, origin_y, style);
}

// fcn_00456384: every opaque pixel of a marker becomes the owner's colour.
void stamp(graphics::Image &canvas, const graphics::Image &marker, int x, int y, std::uint16_t colour,
           graphics::Rect clip) {
    const int left = x - marker.origin_x, top = y - marker.origin_y;
    for (int row = 0; row < marker.height; ++row)
        for (int column = 0; column < marker.width; ++column) {
            const auto index = std::size_t(row * marker.width + column);
            const bool visible =
                marker.opaque_pixels.empty() ? marker.pixels[index] != 0 : marker.opaque_pixels[index] != 0;
            const int px = left + column, py = top + row;
            if (visible && ui::contains(clip, px, py))
                canvas.pixels[std::size_t(py * canvas.width + px)] = colour;
        }
}

void outline(graphics::Image &canvas, graphics::Rect rect, std::uint16_t colour, graphics::Rect clip) {
    auto plot = [&](int x, int y) {
        if (ui::contains(clip, x, y))
            canvas.pixels[std::size_t(y * canvas.width + x)] = colour;
    };
    for (int x = rect.left; x < rect.right; ++x) {
        plot(x, rect.top);
        plot(x, rect.bottom - 1);
    }
    for (int y = rect.top; y < rect.bottom; ++y) {
        plot(rect.left, y);
        plot(rect.right - 1, y);
    }
}

void anchored(Frame &frame, char archive, std::size_t entry, std::size_t index, int x, int y,
              graphics::Transparency transparency = graphics::Transparency::source_key) {
    graphics::blit(frame.canvas, frame.artwork.frame(archive, entry, index), x, y, transparency);
}

int minimap_scale(int value) {
    return int((std::int64_t(value) * 5696) >> 16);
}
int full_map_scale(int value) {
    return int((std::int64_t(value) * 11392) >> 16);
}

std::size_t map_index(const World &world) {
    return std::size_t(world.header.stage_id) * 4 + world.header.map_id;
}

} // namespace

MainScreen::MainScreen(std::string_view preset) {
    while (!preset.empty()) {
        const auto comma = preset.find(',');
        const auto item = preset.substr(0, comma);
        preset = comma == std::string_view::npos ? std::string_view{} : preset.substr(comma + 1);
        const auto equals = item.find('=');
        if (equals == std::string_view::npos)
            continue;
        const auto key = item.substr(0, equals);
        const unsigned value = unsigned(std::atoi(std::string(item.substr(equals + 1)).c_str()));
        if (key == "view")
            view_mode_ = value % 3;
        else if (key == "tab")
            tab_.fill(static_cast<std::uint8_t>(value % 4));
        else if (key == "calendar")
            calendar_mode_ = value % 2;
        else if (key == "map")
            full_map_ = value != 0;
    }
}

namespace {
// Dice FLC frame time (original: [5,3,2][game_speed] x 10 ms) and the 500 ms faces hold.
constexpr unsigned dice_frame_ms = 28, dice_hold_ms = 500;
// Movement state machine tick at the default game speed 1: the 20 ms multimedia timer
// (rich4_mouse_utils.asm:299) sets the tick flag every [6,4,2][game_speed] = 4 periods.
constexpr unsigned toss_frame_ms = 80;
// The dice sound (Effect 10, preset ref_0048235a) is armed to fire at FLC frame 30.
constexpr unsigned dice_sound_frame = 30, dice_sound = 10;
} // namespace

void MainScreen::on_event(const GameEvent &event, const GameSession &session, Artwork &artwork) {
    if (event.kind == GameEventKind::turn_started)
        free_scroll_.reset();
    if (event.kind != GameEventKind::dice_rolled)
        return;
    free_scroll_.reset();
    dice_faces_ = session.dice();
    if (dice_faces_.empty() || event.player >= session.world().players.size()) {
        dice_active_ = false;
        return;
    }
    if (dice_faces_.size() > 3)
        dice_faces_.resize(3);
    auto &frames = dice_animations_[dice_faces_.size()];
    if (frames.empty()) {
        try {
            flc::Decoder movie(artwork.archive('p').decode(3 + dice_faces_.size())); // core decoder
            while (movie.next())
                frames.push_back(movie.image());
        } catch (const std::exception &) {
            frames.clear(); // falls back to cycling the Panel 3 faces
        }
    }
    dice_frames_ = frames.empty() ? 12U : unsigned(frames.size());
    const auto &world = session.world();
    const auto &player = world.players[event.player];
    dice_direction_ = player.state.direction;
    dice_elapsed_ = 0;
    dice_sound_played_ = false;
    dice_active_ = true;
    // §6.1 toss: fcn_0040dd1f puts the roller in movement state 2 and fcn_0040d7c4 advances one
    // frame of the pose graph [0x498ec4 + slot*0x34 + anim*4] per game tick; the dice roll
    // (fcn_00419572) starts on the tick its counter reaches the frames per direction.
    board::PlayerSprite pose{};
    pose.character = player.state.character;
    pose.traffic_method = static_cast<std::uint8_t>(player.state.traffic_method & 3U);
    pose.on_water = player.state.node_id < world.nodes.size() &&
                    (world.nodes[player.state.node_id].flags & 0x80000000U) != 0;
    pose.sleepwalking = player.status.days_sleep_walking != 0;
    pose.injured = (player.state.controller_flags & 0x40U) != 0;
    pose.tossing = true;
    toss_slot_ = event.player;
    toss_frames_ = 0;
    if (!pose.injured && pose.character < 12) {
        try {
            toss_frames_ = unsigned(artwork.frame_count('d', board::player_resource(pose)) / 8);
        } catch (const std::exception &) {
            toss_frames_ = 0; // no toss art: straight to the dice
        }
    }
    toss_ms_ = skip_toss_ ? 0U : toss_frames_ * toss_frame_ms;
}

bool MainScreen::dice_busy() const {
    return dice_active_ && dice_elapsed_ < toss_ms_ + dice_frames_ * dice_frame_ms + dice_hold_ms;
}

std::optional<std::uint8_t> MainScreen::toss_frame(std::size_t slot) const {
    if (!dice_busy() || slot != toss_slot_ || toss_frames_ == 0)
        return std::nullopt;
    if (dice_elapsed_ >= toss_ms_)
        return static_cast<std::uint8_t>(toss_frames_ - 1);
    return static_cast<std::uint8_t>(std::min(dice_elapsed_ / toss_frame_ms, toss_frames_ - 1));
}

void MainScreen::advance(unsigned elapsed_ms) {
    if (dice_active_)
        dice_elapsed_ += elapsed_ms;
}

void MainScreen::pointer_motion(int x, int y, bool left_down) {
    if (!left_down) {
        go_drag_.reset();
        minimap_drag_ = false;
        return;
    }
    if (pending_roll_ && std::abs(x - press_point_.first) + std::abs(y - press_point_.second) > 6)
        pending_roll_ = nullptr; // moved: this press is a drag, not a tap
    if (go_drag_ && !pending_roll_) {
        go_x_ = std::clamp(x - go_drag_->first, 0, 568);
        go_y_ = std::clamp(y - go_drag_->second, 0, 413);
    }
    if (minimap_drag_) {
        const int centre_x = std::clamp(((x - 440) * 761856) >> 16, 220, 2084);
        const int centre_y = std::clamp(((y - minimap_top_) * 761856) >> 16, 220, 2084);
        free_scroll_ = board::Point{centre_x, centre_y};
    }
}

void MainScreen::pointer_up(int x, int y, bool right_button) {
    if (auto roll = std::exchange(pending_roll_, nullptr); roll && !right_button)
        roll();
    go_drag_.reset();
    minimap_drag_ = false;
    if (right_button && view_mode_ != 0 && x > 440 && y > minimap_top_ && y < minimap_top_ + 200)
        free_scroll_.reset();
}

std::uint8_t MainScreen::go_mask(Frame &frame, int x, int y) {
    if (go_mask_.empty())
        go_mask_ = frame.artwork.archive('p').decode(8);
    const int lx = x - go_x_, ly = y - go_y_;
    if (lx < 0 || ly < 0 || lx >= 72 || ly >= 67 || std::size_t(ly * 72 + lx) >= go_mask_.size())
        return 0;
    return go_mask_[std::size_t(ly * 72 + lx)];
}

void MainScreen::draw(Frame &frame, const GameSession &session, unsigned rotation, const MainScreenActions &actions) {
    const auto &world = session.world();
    draw_dice(frame, session, rotation, actions);
    if (human_turn_shown(session))
        draw_go(frame, session, actions);
    else {
        go_drag_.reset();
        pending_roll_ = nullptr;
    }
    draw_toolbar(frame, session, actions);
    if (view_mode_ == 2) {
        draw_header(frame, world);
        draw_minimap(frame, world, 80, actions);
    } else {
        draw_info_panel(frame, world, actions);
        if (view_mode_ == 1)
            draw_minimap(frame, world, 280, actions);
    }
    if (view_mode_ != 1)
        draw_calendar(frame, world, actions);
    if (full_map_ && session.phase() != GamePhase::human_ready)
        full_map_ = false;
    if (full_map_)
        draw_full_map(frame, world);
}

// §2
void MainScreen::draw_toolbar(Frame &frame, const GameSession &session, const MainScreenActions &actions) {
    graphics::blit(frame.canvas, frame.artwork.frame('p', 1, 0), 0, 0, graphics::Transparency::opaque);
    const int hover =
        frame.pointer_x >= 0 && frame.pointer_x < 440 && frame.pointer_y >= 0 && frame.pointer_y < 40
            ? frame.pointer_x / 40
            : -1;
    if (hover != hover_ && hover >= 0 && hover < 11 && actions.sound)
        actions.sound(0);
    hover_ = hover;
    const bool ready = human_turn_shown(session);
    struct Icon {
        int id;
        std::function<void()> action;
        bool enabled;
    };
    const std::array<Icon, 11> icons{{
        {main_ids::help, actions.help, true},
        {main_ids::options, actions.options, true},
        {main_ids::ai, actions.ai, true},
        {main_ids::load, actions.load, actions.can_load},
        {main_ids::save, actions.save, actions.can_save},
        {main_ids::full_map, [this] { full_map_ = true; }, ready},
        {main_ids::query, actions.query, true},
        {main_ids::tools, actions.tools, ready},
        {main_ids::cards, actions.cards, ready},
        {main_ids::trading, actions.trading, ready},
        {main_ids::stocks, actions.stocks, ready},
    }};
    for (int i = 0; i < 11; ++i) {
        anchored(frame, 'p', 1, std::size_t(i == hover_ ? 12 + i : 1 + i), 40 * i + 20, 20);
        frame.hit(icons[std::size_t(i)].id, {40 * i, 0, 40 * i + 40, 40}, icons[std::size_t(i)].action,
                  icons[std::size_t(i)].enabled);
    }
}

// §3.1-3.2
void MainScreen::draw_info_panel(Frame &frame, const World &world, const MainScreenActions &actions) {
    const auto p = displayed_player(world);
    const auto &player = world.players[p];
    const auto &state = player.state;
    const unsigned tab = tab_[p] % 4;
    graphics::blit(frame.canvas, frame.artwork.frame('p', 0, tab), 440, 0, graphics::Transparency::opaque);
    static constexpr std::array<std::array<const char *, 3>, 4> labels{{{"現　金", "存　款", "總資產"},
                                                                        {"土　地", "連鎖店", "設　施"},
                                                                        {"總市值", "成　本", "經營權"},
                                                                        {"點　卷", "貸　款", "保險期"}}};
    for (int row = 0; row < 3; ++row)
        text(frame, labels[tab][std::size_t(row)], 450, std::array{80, 145, 208}[std::size_t(row)], Align::left);
    text(frame, "物價指數  " + std::to_string(world.header.price_index), 450, 260, Align::left);
    static constexpr std::array<const char *, 4> tab_names{"資金", "地產", "股票", "其他"};
    for (unsigned i = 0; i < 4; ++i) {
        text(frame, tab_names[i], 627, std::array{35, 108, 178, 250}[i], Align::vertical,
             {18, i == tab ? text_ink : ui::color(0x40, 0x40, 0x40), 0});
        frame.hit(main_ids::tab_first + int(i), {616, int(i) * 70, 640, int(i) * 70 + 70}, [this, p, i, actions] {
            if (tab_[p] != i && actions.sound)
                actions.sound(1);
            tab_[p] = static_cast<std::uint8_t>(i);
        });
    }
    ui::fill(frame.canvas, {523, 57, 609, 69}, ui::color(0, 0, 0));
    ui::fill(frame.canvas, {522, 56, 608, 68}, ui::player_color(state.color));
    anchored(frame, 'm', 27 + std::size_t(state.character), 0, 482, 40);
    if (const auto ally = player.status.allied_player; ally && ally <= world.players.size())
        anchored(frame, 'm', 27 + std::size_t(world.players[ally - 1u].state.character), 1, 524, 64);
    // Plain 22 px with spacing 0; short names are spread to fill the bar as in the reference shots.
    const std::string_view name = names.at(state.character % names.size());
    const int count = int(characters(name).size());
    const int extra = count > 1 ? std::clamp((86 - count * 22) / (count - 1), -1, 10) : 0;
    text(frame, name, 564, 40, Align::center, {22, text_ink, extra + 1});

    std::array<std::string, 3> values;
    const auto owner = static_cast<std::uint8_t>(p + 1);
    if (tab == 0) {
        values = {ui::money(state.cash), ui::money(state.bank_balance),
                  ui::money(stocks::player_wealth(world, p))};
    } else if (tab == 1) {
        int properties = 0, chains = 0, built = 0;
        for (std::size_t i = 1; i < world.lands.size(); ++i)
            if (world.lands[i].economy.owner_code == owner) {
                ++properties;
                chains += world.lands[i].economy.chain_store != 0;
            }
        for (std::size_t i = 1; i < world.facilities.size(); ++i)
            if (world.facilities[i].owner_code == owner) {
                ++properties;
                built += world.facilities[i].level != 0;
            }
        values = {std::to_string(properties), std::to_string(chains), std::to_string(built)};
    } else if (tab == 2) {
        std::int64_t market = 0, cost = 0;
        for (std::size_t k = 0; k < world.stocks.size(); ++k) {
            const auto amount = player.stocks[k].amount;
            market = std::int64_t(double(market) + double(amount) * std::bit_cast<float>(world.stocks[k].float_bits[2]));
            cost = std::int64_t(double(cost) + double(amount) * std::bit_cast<float>(player.stocks[k].raw_cost_basis));
        }
        int companies = 0;
        for (const auto &company : world.commercials)
            companies += company.id && company.owner_code == owner;
        values = {ui::money(market), ui::money(cost), std::to_string(companies)};
    } else {
        values = {std::to_string(state.points), ui::money(state.loan),
                  std::to_string(player.status.days_assurance) + "天"};
        if (state.loan && player.status.loan_due_date) {
            const auto &date = world.header.date;
            const auto days = calendar::days_between({date.year, date.month, date.day},
                                                     calendar::unpack(player.status.loan_due_date));
            if (days)
                text(frame, std::to_string(days) + "天", 500, 146, Align::left);
        }
    }
    for (int row = 0; row < 3; ++row)
        text(frame, values[std::size_t(row)], 600, std::array{102, 166, 230}[std::size_t(row)], Align::right,
             {22, text_ink, 0});
}

// §3.4
void MainScreen::draw_header(Frame &frame, const World &world) {
    const auto p = displayed_player(world);
    const auto &player = world.players[p];
    graphics::blit(frame.canvas, frame.artwork.frame('p', 0, 4), 440, 0, graphics::Transparency::opaque);
    text(frame, "現金", 530, 40, Align::left);
    text(frame, "存款", 530, 62, Align::left);
    ui::fill(frame.canvas, {529, 33, 635, 37}, ui::color(0, 0, 0));
    ui::fill(frame.canvas, {528, 32, 634, 36}, ui::player_color(player.state.color));
    anchored(frame, 'm', 27 + std::size_t(player.state.character), 0, 482, 40);
    if (const auto ally = player.status.allied_player; ally && ally <= world.players.size())
        anchored(frame, 'm', 27 + std::size_t(world.players[ally - 1u].state.character), 1, 524, 64);
    text(frame, names.at(player.state.character % names.size()), 582, 16, Align::center, {20, text_ink, 0});
    text(frame, ui::money(player.state.cash), 634, 41, Align::right);
    text(frame, ui::money(player.state.bank_balance), 634, 63, Align::right);
}

// §4.1
void MainScreen::draw_minimap(Frame &frame, const World &world, int top, const MainScreenActions &actions) {
    minimap_top_ = top;
    const graphics::Rect area{440, top, 640, top + 200};
    const auto map = map_index(world);
    graphics::blit(frame.canvas, frame.artwork.frame('m', 16 + map, 0), 440, top,
                   graphics::Transparency::opaque);
    auto owner_colour = [&world](std::uint8_t owner) {
        return owner && owner <= world.players.size() ? ui::player_color(world.players[owner - 1u].state.color) : white;
    };
    for (std::size_t i = 1; i < world.lands.size(); ++i)
        if (const auto &land = world.lands[i]; land.economy.owner_code)
            stamp(frame.canvas, frame.artwork.frame('d', 517, 22 + (land.direction & 1U)),
                  440 + minimap_scale(land.x), top + minimap_scale(land.y), owner_colour(land.economy.owner_code), area);
    for (std::size_t i = 1; i < world.facilities.size(); ++i)
        if (const auto &facility = world.facilities[i]; facility.owner_code)
            stamp(frame.canvas, frame.artwork.frame('d', 517, 24 + (facility.direction & 1U)),
                  440 + minimap_scale(facility.x), top + minimap_scale(facility.y),
                  owner_colour(facility.owner_code), area);
    for (const auto &company : world.commercials)
        if (company.id && company.owner_code)
            stamp(frame.canvas, frame.artwork.frame('d', 517, 24 + (company.direction & 1U)),
                  440 + minimap_scale(company.x), top + minimap_scale(company.y), owner_colour(company.owner_code),
                  area);
    // Rotate buttons: pressed frames while held.
    const bool left_held = frame.pointer_down && ui::contains({443, top + 3, 468, top + 29}, frame.pointer_x, frame.pointer_y);
    const bool right_held = frame.pointer_down && ui::contains({468, top + 3, 494, top + 29}, frame.pointer_x, frame.pointer_y);
    anchored(frame, 'd', 517, left_held ? 18 : 20, 443, top + 3);
    anchored(frame, 'd', 517, right_held ? 19 : 21, 468, top + 3);
    frame.hit(main_ids::rotate_left, {443, top + 3, 468, top + 29}, [actions] {
        if (actions.sound)
            actions.sound(1);
        actions.rotate(7);
    });
    frame.hit(main_ids::rotate_right, {468, top + 3, 494, top + 29}, [actions] {
        if (actions.sound)
            actions.sound(1);
        actions.rotate(1);
    });
    for (std::size_t slot = 0; slot < world.players.size() && slot < world.header.player_count; ++slot) {
        const auto &state = world.players[slot].state;
        if (!state.x || !(state.controller_flags & 3U))
            continue;
        anchored(frame, 'm', 27 + std::size_t(state.character), 6, 440 + minimap_scale(state.x),
                 top + minimap_scale(state.y));
    }
    const auto &active = world.players[displayed_player(world)].state;
    const int box_x = 440 + minimap_scale(active.x), box_y = top + minimap_scale(active.y);
    outline(frame.canvas, {box_x - 15, box_y - 15, box_x + 15, box_y + 15}, white, area);
    if (free_scroll_) {
        const int x = 440 + minimap_scale(free_scroll_->x), y = top + minimap_scale(free_scroll_->y);
        if (x != box_x || y != box_y)
            outline(frame.canvas, {x - 15, y - 15, x + 15, y + 15}, red, area);
    }
    frame.hit(main_ids::minimap, area, [this, &frame] {
        minimap_drag_ = true;
        pointer_motion(frame.pointer_x, frame.pointer_y, true);
    });
}

// §5
void MainScreen::draw_calendar(Frame &frame, const World &world, const MainScreenActions &actions) {
    static constexpr std::array<std::uint8_t, 12> seasons{3, 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3};
    const auto &date = world.header.date;
    const calendar::Date today{date.year, date.month, date.day};
    const auto season = seasons[std::size_t((date.month + 11) % 12)];
    const auto map = world.header.map_id, stage = world.header.stage_id;
    const Text big{24, text_ink, 1, white, true};
    if (calendar_mode_ == 0) {
        // loc_00416b4c: on a holiday-table date the festival picture replaces the season picture
        // and the weekday/day turn red even when the day is not a closing day.
        const auto *festival = screens::festival_picture(frame.artwork, world);
        graphics::blit(frame.canvas, festival ? *festival : frame.artwork.frame('p', 2, season), 440, 280,
                       graphics::Transparency::opaque);
        anchored(frame, 'p', 2, 8, 462, 300);
        anchored(frame, 'p', 2, 11, 492, 301);
        const auto weekday = calendar::weekday(today);
        static constexpr std::array<const char *, 7> weekdays{"星期日", "星期一", "星期二", "星期三",
                                                              "星期四", "星期五", "星期六"};
        const bool holiday = weekday == 0 || festival != nullptr;
        const Text style{16, holiday ? red : text_ink, 1, holiday ? text_ink : white, true};
        text(frame, weekdays[weekday % 7], 454, 352, Align::vertical, style);
        auto day_style = style;
        day_style.size = 60;
        text(frame, std::to_string(date.day), 500, 376, Align::center, day_style);
    } else {
        graphics::blit(frame.canvas, frame.artwork.frame('p', 2, 4 + season), 440, 280,
                       graphics::Transparency::opaque);
        const int first = calendar::weekday({date.year, date.month, 1});
        const int days = calendar::days_in_month(date.year, date.month);
        for (int d = 1; d <= days; ++d) {
            const int cell = first + d - 1;
            const int x = 470 + 23 * (cell % 7), y = 378 + 14 * (cell / 7);
            if (d == date.day)
                outline(frame.canvas, {x - 10, y - 6, x + 10, y + 8}, red, {440, 280, 640, 480});
            const bool closed =
                holidays::is_closed({date.year, date.month, static_cast<std::uint8_t>(d)}, map, stage);
            text(frame, std::to_string(d), x, y, Align::center, {12, closed ? red : text_ink, 1});
        }
    }
    text(frame, std::to_string(date.year), 580, 288, Align::left, big);
    auto month_style = big;
    month_style.size = 28;
    text(frame, std::to_string(date.month) + "月", 500, 328, Align::center, month_style);
    frame.hit(main_ids::sun, {448, 288, 475, 315}, [this, actions] {
        if (calendar_mode_ != 0 && actions.sound)
            actions.sound(1);
        calendar_mode_ = 0;
    });
    frame.hit(main_ids::moon, {478, 288, 505, 315}, [this, actions] {
        if (calendar_mode_ != 1 && actions.sound)
            actions.sound(1);
        calendar_mode_ = 1;
    });
}

// §6.1
void MainScreen::draw_go(Frame &frame, const GameSession &session, const MainScreenActions &actions) {
    const auto &player = session.world().active_player();
    const auto &status = player.status;
    const bool stopped = status.days_stopping != 0;
    const unsigned kind = status.days_tortoise_walking ? 4 : stopped ? 2 : 0;
    const unsigned blink = unsigned(frame.ticks / 500) % 2;
    const int gx = go_x_, gy = go_y_;
    anchored(frame, 'p', 7, blink + kind, gx, gy);
    const unsigned traffic = player.state.traffic_method & 3U;
    const unsigned dice = player.state.dice_count;
    if (traffic == 0 || traffic == 3) {
        anchored(frame, 'p', 7, stopped ? 6 : 7, stopped ? gx + 8 : gx + 7, gy + 26);
    } else {
        const unsigned count = traffic == 1 ? 2 : 3;
        for (unsigned i = 0; i < count; ++i) {
            const int y = traffic == 1 ? gy + 16 + 19 * int(i) : gy + 9 + 16 * int(i);
            const bool active = i < dice && !stopped;
            anchored(frame, 'p', 7, 2 * i + (active ? 7 : 6), active ? gx + 7 : gx + 8, y);
        }
        frame.hit(main_ids::dice_count, {gx + 7, gy + 9, gx + 23, gy + 57}, [this, &frame, traffic, actions] {
            const int ly = frame.pointer_y - go_y_;
            const int requested = traffic == 1 ? std::clamp((ly - 16) / 19, 0, 1) : std::clamp((ly - 9) / 16, 0, 2);
            actions.dice_count(unsigned(requested + 1));
        }, !stopped);
    }
    frame.hit(main_ids::roll, {gx + 30, gy + 14, gx + 65, gy + 55}, [this, actions] {
        go_drag_.reset();
        actions.roll();
    });
    frame.hit(main_ids::go_frame, {gx, gy, gx + 72, gy + 67}, [this, &frame, actions] {
        const int x = frame.pointer_x, y = frame.pointer_y;
        go_drag_ = std::pair(x - go_x_, y - go_y_);
        press_point_ = {x, y};
        pending_roll_ = [this, actions] {
            go_drag_.reset();
            actions.roll();
        };
    });
}

// §6.2
void MainScreen::draw_dice(Frame &frame, const GameSession &session, unsigned rotation,
                           const MainScreenActions &actions) {
    if (!dice_active_)
        return;
    const unsigned total = dice_frames_ * dice_frame_ms;
    if (dice_elapsed_ >= toss_ms_ + total + dice_hold_ms && session.phase() != GamePhase::rolling) {
        dice_active_ = false;
        return;
    }
    if (dice_elapsed_ < toss_ms_)
        return; // the character is still tossing (toss_frame)
    const unsigned dice_elapsed = dice_elapsed_ - toss_ms_;
    if (!dice_sound_played_ && dice_elapsed / dice_frame_ms >= std::min(dice_sound_frame, dice_frames_ - 1)) {
        dice_sound_played_ = true;
        if (actions.sound)
            actions.sound(dice_sound);
    }
    static constexpr std::array<int, 8> dx{4, 12, 8, -4, -12, -24, -20, -12};
    static constexpr std::array<int, 8> dy{12, 12, 6, -6, -12, -12, -6, 6};
    const auto facing = std::size_t((8 - rotation + dice_direction_) & 7U);
    const int ax = 136 + dx[facing], ay = 48 + dy[facing];
    const auto &animation = dice_animations_[dice_faces_.size()];
    if (dice_elapsed < total) {
        const auto index = std::min<std::size_t>(dice_elapsed / dice_frame_ms, dice_frames_ - 1);
        if (!animation.empty()) {
            graphics::blit(frame.canvas, animation[index], ax, ay, board_area);
            return;
        }
        // Fallback when the FLC cannot be decoded: tumble the final-position faces.
        for (std::size_t i = 0; i < dice_faces_.size(); ++i)
            graphics::blit(frame.canvas, frame.artwork.frame('p', 3, 6 * i + (index + i * 2) % 6), ax + 85,
                           ay + 145, board_area);
        return;
    }
    for (std::size_t i = 0; i < dice_faces_.size(); ++i)
        graphics::blit(frame.canvas,
                       frame.artwork.frame('p', 3, 6 * i + std::clamp<std::size_t>(dice_faces_[i], 1, 6) - 1),
                       ax + 85, ay + 145, board_area);
}

// §4.3
void MainScreen::draw_full_map(Frame &frame, const World &world) {
    frame.buttons.clear();
    const auto map = map_index(world);
    graphics::blit(frame.canvas, frame.artwork.frame('m', 16 + map, 1), 20, 60, graphics::Transparency::opaque);
    const graphics::Rect area{20, 60, 420, 460};
    auto owner_colour = [&world](std::uint8_t owner) {
        return owner && owner <= world.players.size() ? ui::player_color(world.players[owner - 1u].state.color) : white;
    };
    for (std::size_t i = 1; i < world.lands.size(); ++i)
        if (const auto &land = world.lands[i]; land.economy.owner_code)
            stamp(frame.canvas, frame.artwork.frame('d', 517, 26 + (land.direction & 1U)),
                  20 + full_map_scale(land.x), 60 + full_map_scale(land.y), owner_colour(land.economy.owner_code),
                  area);
    for (std::size_t i = 1; i < world.facilities.size(); ++i)
        if (const auto &facility = world.facilities[i]; facility.owner_code)
            stamp(frame.canvas, frame.artwork.frame('d', 517, 28 + (facility.direction & 1U)),
                  20 + full_map_scale(facility.x), 60 + full_map_scale(facility.y),
                  owner_colour(facility.owner_code), area);
    for (const auto &company : world.commercials)
        if (company.id && company.owner_code)
            stamp(frame.canvas, frame.artwork.frame('d', 517, 28 + (company.direction & 1U)),
                  20 + full_map_scale(company.x), 60 + full_map_scale(company.y), owner_colour(company.owner_code),
                  area);
    for (std::size_t slot = 0; slot < world.players.size() && slot < world.header.player_count; ++slot) {
        const auto &state = world.players[slot].state;
        if (state.x && (state.controller_flags & 3U))
            anchored(frame, 'm', 27 + std::size_t(state.character), 5, 20 + full_map_scale(state.x),
                     60 + full_map_scale(state.y));
    }
    frame.hit(main_ids::full_map_close, {0, 0, 640, 480}, [this] { full_map_ = false; });
}

} // namespace rich4::native
