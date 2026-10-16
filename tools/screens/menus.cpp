// SPDX-License-Identifier: GPL-3.0-or-later
#include "menus.hpp"

#include "rich4/calendar.hpp"
#include "rich4/economy.hpp"
#include "rich4/services.hpp"
#include "rich4/stocks.hpp"
#include "../native_stock_names.hpp"

#include <algorithm>
#include <bit>
#include <cstdio>

namespace rich4::native::screens {
namespace {

std::string format(const char *pattern, long long value) {
    char buffer[96];
    std::snprintf(buffer, sizeof buffer, pattern, value);
    return buffer;
}

graphics::Rect anchored_rect(const graphics::Image &image, int x, int y) {
    return {x - image.origin_x, y - image.origin_y, x - image.origin_x + image.width,
            y - image.origin_y + image.height};
}

} // namespace

// ---------------------------------------------------------------------------------------
// §1 Main menu (rich4_ui_main.asm, Data 1)

void MainMenu::draw(Host &host, const MenuActions &actions) {
    auto &frame = host.frame;
    backdrop(frame, 'd', 1, 0);
    struct Entry {
        int id, x, y;
        const std::function<void()> *action;
        bool enabled;
    };
    // ref_0046cb28 centres; index i uses frame 2i+1 (normal) and 2i+2 (hover).
    const std::array<Entry, 5> entries{{{ids::menu_start, 190, 380, &actions.start, true},
                                        {ids::menu_load, 328, 380, &actions.load, actions.load_enabled},
                                        {ids::menu_options, 468, 378, &actions.options, true},
                                        {ids::menu_exit, 328, 450, &actions.exit, true},
                                        {ids::menu_new_stage, 62, 380, &actions.new_stage, true}}};
    int hovered = -1;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto &entry = entries[i];
        graphics::blit(frame.canvas, frame.artwork.frame('d', 1, 2 * i + 1), entry.x, entry.y);
        const auto rect = anchored_rect(frame.artwork.frame('d', 1, 2 * i + 2), entry.x, entry.y);
        if (hovered < 0 && frame.pointer_in(rect))
            hovered = int(i);
    }
    if (hovered != hovered_) {
        if (hovered >= 0 && host.sound)
            host.sound(hover_sound);
        hovered_ = hovered;
    }
    if (hovered >= 0)
        graphics::blit(frame.canvas, frame.artwork.frame('d', 1, 2 * std::size_t(hovered) + 2),
                       entries[std::size_t(hovered)].x, entries[std::size_t(hovered)].y);
    text(frame, "V3.11", 638, 470, 6, white_text);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto &entry = entries[i];
        auto rect = anchored_rect(frame.artwork.frame('d', 1, 2 * i + 2), entry.x, entry.y);
        if (entry.id == ids::menu_exit) // finger-sized target around the small EXIT label
            rect = {rect.left - 10, rect.top - 6, rect.right + 10, 480};
        hit(host, entry.id, rect, entry.id == ids::menu_exit ? exit_sound : click_sound, *entry.action,
            entry.enabled);
    }
}

// ---------------------------------------------------------------------------------------
// §2 New game setup (rich4_new_game.asm, jump.mkf 8)

namespace {
constexpr std::array<const char *, 12> spaced_names{"約 翰 喬", "沙隆巴斯", "忍 太 郎", "錢 夫 人",
                                                    "阿 土 伯", "莎拉公主", "宮本寶藏", "糖 糖",
                                                    "烏 咪",   "孫 小 美", "小 丹 尼", "金 貝 貝"};
// MOD adds a seventh choice without removing any original starting fund.
constexpr std::array<std::int32_t, 8> funds{900000000, 30000000, 300000, 200000, 100000, 50000, 30000, 10000};
constexpr std::array<const char *, 6> periods{"無限期", "二年", "一年", "六個月", "三個月", "一個月"};
constexpr std::array<int, 6> victory_multipliers{0, 100, 50, 10, 5, 3};
constexpr std::array<const char *, 6> setting_labels{"遊戲人數", "總 資 金", "行進方式",
                                                     "土地權限", "遊戲時間", "勝利條件"};
// Drop-down list rectangles (0x46cc88, inclusive) and frames (0x46ccb8).
constexpr std::array<graphics::Rect, 6> dropdown_rects{
    {{561, 251, 602, 320}, {502, 240, 626, 427}, {561, 323, 602, 392},
     {536, 195, 602, 333}, {536, 231, 602, 369}, {516, 267, 602, 405}}};
constexpr std::array<std::size_t, 6> dropdown_frames{5, 6, 5, 6, 6, 7};
// Rows 4 and 5 (day limit, target wealth) have no NewGameOptions field or end rule natively.
constexpr bool row_supported(int row) {
    return row < 4;
}
int item_count(int row) {
    return row == 0 || row == 2 ? 3 : row == 1 ? 8 : 6;
}
std::string item_label(int row, int index, int fund_index) {
    switch (row) {
    case 0:
        return std::array{"二人", "三人", "四人"}[std::size_t(index)];
    case 1:
        return std::to_string(funds[std::size_t(index)]);
    case 2:
        return std::array{"步行", "機車", "汽車"}[std::size_t(index)];
    case 5:
        return index == 0 ? "無限"
                          : std::to_string(std::int64_t(funds[std::size_t(fund_index)]) *
                                           victory_multipliers[std::size_t(index)]);
    default:
        return periods[std::size_t(index)];
    }
}
// 0x46cb58: x of each riding sprite, first pick rightmost.
int strip_x(int players, std::size_t slot) {
    static constexpr std::array<std::array<int, 4>, 3> table{{{330, 110, 0, 0}, {366, 220, 74, 0}, {385, 275, 165, 55}}};
    return table[std::size_t(std::clamp(players, 2, 4) - 2)][slot];
}
} // namespace

void NewGameSetup::open(unsigned stage, unsigned map, std::uint32_t seed) {
    const auto saved_values = values_;
    *this = {};
    values_ = saved_values;
    stage_ = stage;
    map_ = map % 4;
    random_.seed(seed);
}

NewGameChoice NewGameSetup::choice() const {
    NewGameChoice result;
    result.stage = stage_;
    result.map = map_;
    result.fund = funds[std::size_t(values_[1])];
    result.lease_mode = std::uint32_t(values_[3]);
    for (std::size_t i = 0; i < picks_.size(); ++i)
        result.players.push_back({picks_[i], static_cast<std::uint8_t>(i < human_count_ ? 1 : 2),
                                  static_cast<std::uint8_t>(values_[2]), 0});
    return result;
}

void NewGameSetup::fill_one() {
    std::vector<std::uint8_t> available;
    for (std::uint8_t i = 0; i < 12; ++i)
        if (std::find(picks_.begin(), picks_.end(), i) == picks_.end())
            available.push_back(i);
    if (!available.empty() && int(picks_.size()) < player_count())
        picks_.push_back(available[random_() % available.size()]);
}

void NewGameSetup::cancel(Host &host, const std::function<void()> &exit) {
    if (host.sound)
        host.sound(cancel_sound);
    if (dropdown_ >= 0)
        dropdown_ = -1;
    else if (!busy())
        exit();
}

void NewGameSetup::draw_backdrop(Host &host) {
    auto &frame = host.frame;
    const int key = int(stage_ * 4 + map_);
    if (backdrop_key_ != key) {
        backdrop_ = raw_picture(frame.artwork, 'j', std::size_t(key), 640, 480);
        darken(backdrop_, {0, 0, 640, 480}, -16);
        backdrop_key_ = key;
    }
    const int shift = int((frame.ticks / 100) * 2 % 640);
    graphics::blit(frame.canvas, backdrop_, -shift, 0, graphics::Transparency::opaque);
    graphics::blit(frame.canvas, backdrop_, 640 - shift, 0, graphics::Transparency::opaque);
}

void NewGameSetup::draw(Host &host, const std::function<void(const NewGameChoice &)> &start,
                        const std::function<void()> &exit) {
    auto &frame = host.frame;
    if (filling_ && frame.ticks - fill_tick_ >= 1100) {
        fill_one();
        fill_tick_ = frame.ticks;
        if (int(picks_.size()) >= player_count()) {
            filling_ = false;
            leaving_ = true;
            leave_tick_ = frame.ticks;
        }
    }
    int shift = 0;
    if (leaving_) {
        // NG:2578: panels slide away and the riders leave, accelerating.
        const auto steps = int((frame.ticks - leave_tick_) / 50);
        shift = 4 * steps + 3 * steps * steps;
        if (shift > 660) {
            leaving_ = false;
            start(choice());
            return;
        }
    }
    draw_backdrop(host);
    const int count = player_count();
    // Portrait panel.
    frame.picture('j', 8, 0, 4, 10 - shift);
    for (int i = 0; i < 12; ++i) {
        const int x = 8 + 72 * (i % 6), y = 15 + 72 * (i / 6) - shift;
        frame.picture('d', 2, std::size_t(i), x, y);
        if (std::find(picks_.begin(), picks_.end(), i) != picks_.end())
            darken(frame.canvas, {x, y, x + 72, y + 72}, -16);
    }
    int hovered = -1;
    if (!busy() && dropdown_ < 0 && frame.pointer_in({8, 15, 440, 159})) {
        const int i = ((frame.pointer_y - 15) / 72) * 6 + (frame.pointer_x - 8) / 72;
        if (std::find(picks_.begin(), picks_.end(), i) == picks_.end())
            hovered = i;
    }
    if (hovered != hovered_) {
        if (hovered >= 0 && host.sound)
            host.sound(hover_sound);
        hovered_ = hovered;
    }
    if (hovered >= 0) {
        const int column = hovered % 6, row = hovered / 6;
        frame.picture('d', 2, std::size_t(hovered), 12 + 72 * column, 11 + 72 * row);
        const int tag = row == 0 ? 83 : 63;
        darken(frame.canvas, {13 + 72 * column, tag, 83 + 72 * column, tag + 20}, -20);
        text(frame, spaced_names[std::size_t(hovered)], 48 + 72 * column, tag + 10, 2, white_text);
    }
    // Settings panel (_rich4_draw_game_config_menu).
    const int sx = shift;
    frame.picture('j', 8, std::size_t(20 * stage_ + 1), 445 + sx, 10);
    frame.picture('j', 8, 8, 595 + sx, 30 + 32 * int(map_));
    const Style label{15, 0xffffff, 0x101010, shadow | bold}, grey_label{15, 0x909090, 0x101010, shadow | bold};
    const Style value{15, 0x101010, 0x101010, bold}, grey_value{15, 0x808080, 0x101010, bold};
    for (int row = 0; row < 6; ++row) {
        const int y = 238 + 36 * row;
        text(frame, setting_labels[std::size_t(row)], 453 + sx, y, 5, row_supported(row) ? label : grey_label);
        text(frame, item_label(row, values_[std::size_t(row)], values_[1]), 600 + sx, y, 6,
             row == 1 ? Style{12, 0x101010, 0x101010, bold}
                      : row_supported(row) ? value : grey_value);
    }
    const bool pressing = frame.pointer_down && !busy() && dropdown_ < 0;
    if (pressing && frame.pointer_in({456, 176, 536, 216}) && !picks_.empty())
        frame.picture('j', 8, 2, 456, 176);
    if (pressing && frame.pointer_in({544, 176, 624, 216}))
        frame.picture('j', 8, 3, 544, 176);
    for (int row = 0; row < 4; ++row)
        if ((pressing || dropdown_ == row) && (dropdown_ == row || frame.pointer_in({602, 226 + 36 * row, 626, 251 + 36 * row})))
            frame.picture('j', 8, 4, 602, 226 + 36 * row);
    // Riders, feet at y=440 (_rich4_select_nth_player).
    for (std::size_t slot = 0; slot < picks_.size(); ++slot) {
        const auto entry = std::size_t(9 + 3 * picks_[slot] + values_[2]);
        const auto frames = std::max<std::size_t>(1, frame.artwork.frame_count('j', entry));
        graphics::blit(frame.canvas, frame.artwork.frame('j', entry, (frame.ticks / 100) % frames),
                       strip_x(count, slot) + shift, 440);
    }
    if (leaving_) {
        hit(host, ids::setup_dropdown_close, {0, 0, 640, 480}, no_sound, [this] { leave_tick_ = 0; });
        return;
    }
    if (filling_)
        return;
    if (dropdown_ >= 0) {
        const auto &list = dropdown_rects[std::size_t(dropdown_)];
        frame.picture('j', 8, dropdown_frames[std::size_t(dropdown_)], list.left, list.top);
        if (dropdown_ == 1) // MOD has eight amounts; the legacy frame only has six rows.
            ui::fill(frame.canvas, {list.left, list.top + 138, list.right, list.bottom},
                     rgb(0xf0ead0));
        const int width = list.right - list.left;
        for (int n = 0; n < item_count(dropdown_); ++n) {
            const graphics::Rect item{list.left + 2, list.top + 2 + 23 * n, list.left + 2 + width - 3,
                                      list.top + 22 + 23 * n};
            const bool lit = frame.pointer_in(item);
            if (lit)
                ui::fill(frame.canvas, item, rgb(0xaa0000));
            text(frame, item_label(dropdown_, n, values_[1]), list.right - 2, list.top + 13 + 23 * n, 6,
                 lit ? Style{dropdown_ == 1 ? 13 : 15, 0xffffff, 0x101010, bold}
                     : Style{dropdown_ == 1 ? 13 : 15, 0x101010, 0x101010, bold});
            hit(host, ids::setup_item + n, item, click_sound, [this, n] {
                values_[std::size_t(dropdown_)] = n;
                if (dropdown_ == 0 && int(picks_.size()) > n + 2)
                    picks_.resize(std::size_t(n + 2)); // Lowering the player count drops excess picks.
                dropdown_ = -1;
            });
        }
        hit(host, ids::setup_dropdown_close, {0, 0, 640, 480}, cancel_sound, [this] { dropdown_ = -1; });
        return;
    }
    for (int i = 0; i < 12; ++i) {
        const int x = 8 + 72 * (i % 6), y = 15 + 72 * (i / 6);
        hit(host, ids::setup_character + i, {x, y, x + 72, y + 72}, click_sound, [this, i, count] {
            const auto found = std::find(picks_.begin(), picks_.end(), i);
            if (found != picks_.end())
                picks_.erase(found);
            else if (int(picks_.size()) < count)
                picks_.push_back(static_cast<std::uint8_t>(i));
        });
    }
    for (unsigned m = 0; m < 4; ++m)
        hit(host, ids::setup_map + int(m), {457, 31 + 32 * int(m), 626, 63 + 32 * int(m)}, click_sound,
            [this, m] { map_ = m; });
    hit(host, ids::setup_ok, {456, 176, 536, 216}, click_sound,
        [this, start, instant = host.instant, frame_pointer = &frame] {
            human_count_ = picks_.size();
            if (instant) {
                // Replays fill the remaining seats with the first free characters so a seed
                // always reproduces the same line-up.
                for (std::uint8_t i = 0; i < 12 && int(picks_.size()) < player_count(); ++i)
                    if (std::find(picks_.begin(), picks_.end(), i) == picks_.end())
                        picks_.push_back(i);
                start(choice());
                return;
            }
            filling_ = true;
            fill_tick_ = frame_pointer->ticks;
        },
        !picks_.empty());
    hit(host, ids::setup_exit, {544, 176, 624, 216}, exit_sound, exit);
    for (int row = 0; row < 6; ++row)
        hit(host, ids::setup_dropdown + row, {dropdown_rects[std::size_t(row)].left, 220 + 36 * row, 630, 256 + 36 * row},
            hover_sound,
            [this, row] { dropdown_ = row; }, row_supported(row));
}

// ---------------------------------------------------------------------------------------
// §6 Save / load slots (rich4_ui_save_load.asm, Data 520)

void SaveLoadScreen::draw(Host &host, const std::function<void(int)> &choose, const std::function<void()> &cancel) {
    auto &frame = host.frame;
    const graphics::Rect window = saving_ ? graphics::Rect{40, 48, 595, 429} : graphics::Rect{40, 15, 595, 466};
    frame.picture('d', 520, saving_ ? 1 : 0, window.left, window.top);
    const int first = saving_ ? 1 : 0;
    const auto row_top = [this](int slot) { return saving_ ? 57 + 72 * (slot - 1) : 24 + 72 * slot; };
    for (int slot = first; slot <= 5; ++slot) {
        const auto &entry = slots_[std::size_t(slot)];
        const int y = row_top(slot);
        if (!entry.valid)
            continue;
        frame.picture('d', 520, 10, 129, y);
        if (slot == 0)
            text(frame, "AUTO", 165, y + 15, 2, white_text);
        text(frame, std::to_string(entry.date.year), 165, y + 36, 2, white_text);
        text(frame, std::to_string(entry.date.month) + "/" + std::to_string(entry.date.day), 165, y + 57, 2,
             white_text);
        frame.picture('d', 520, std::size_t(2 + std::min<int>(entry.map, 3) + 4 * std::min<int>(entry.stage, 1)), 209,
                      y);
        for (std::size_t k = 0; k < entry.characters.size() && k < 4; ++k)
            if (entry.characters[k] < 12)
                frame.picture('d', 2, entry.characters[k], 289 + 72 * int(k), y);
    }
    int hovered = -1;
    if (frame.pointer_x >= 130 && frame.pointer_x < 577) {
        const int slot = saving_ ? (frame.pointer_y >= 58 && frame.pointer_y < 417 ? (frame.pointer_y - 57) / 72 + 1 : -1)
                                 : (frame.pointer_y >= 25 && frame.pointer_y < 456 ? (frame.pointer_y - 24) / 72 : -1);
        if (slot >= first && slot <= 5)
            hovered = slot;
    }
    if (hovered != hovered_) {
        if (hovered >= 0 && host.sound)
            host.sound(hover_sound);
        hovered_ = hovered;
    }
    if (hovered >= 0) {
        const int y = row_top(hovered);
        outline_rect(frame.canvas, {127, y - 2, 127 + 452, y - 2 + 76}, rgb(0xffff00));
        outline_rect(frame.canvas, {128, y - 1, 128 + 450, y - 1 + 74}, rgb(0xffff00));
    }
    for (int slot = first; slot <= 5; ++slot) {
        const int y = row_top(slot);
        hit(host, (saving_ ? ids::save_slot : ids::load_slot) + slot, {130, y + 1, 577, y + 73}, exit_sound,
            [choose, slot] { choose(slot); }, saving_ || slots_[std::size_t(slot)].valid);
    }
    hit(host, ids::slot_cancel - 100, window, no_sound, [] {}); // the window body itself does nothing
    // Touch has no right button: a tap outside the window cancels, like the original right-click.
    hit(host, ids::slot_cancel, {0, 0, 640, 480}, cancel_sound, cancel);
}

// ---------------------------------------------------------------------------------------
// §10 Options (rich4_ui_options.asm, Data 3). The three 視窗 choices use the
// original RICH4.CFG[5] layout values; the game already renders all three modes.

void OptionsScreen::draw(Host &host, const OptionsActions &actions) {
    auto &frame = host.frame;
    constexpr int ox = 147, oy = 59;
    const auto at = [](int x, int y, int x2, int y2) { return graphics::Rect{ox + x, oy + y, ox + x2, oy + y2}; };
    frame.picture('d', 3, 0, ox, oy);
    frame.picture('d', 3, in_game_ ? 11 : 10, ox + 168, oy + 2);
    const Style label{15, 0x101010, 0, bold};
    text(frame, "遊戲速度", ox + 14, oy + 25, 5, label);
    text(frame, "動畫過程", ox + 14, oy + 58, 5, label);
    text(frame, "音 樂", ox + 14, oy + 90, 5, label);
    text(frame, "音 效", ox + 14, oy + 122, 5, label);
    text(frame, "自動存檔", ox + 14, oy + 155, 5, label);
    text(frame, "樂  曲", ox + 49, oy + 202, 2, label);
    text(frame, "視  窗", ox + 209, oy + 202, 2, label);
    text(frame, "日、月曆", ox + 286, oy + 226, 2, label);
    text(frame, "縮小地圖", ox + 286, oy + 258, 2, label);
    text(frame, "組合畫面", ox + 286, oy + 290, 2, label);
    text(frame, "取 消", ox + 224, oy + 328, 2, {20, 0x101010, 0, bold});
    text(frame, "確 定", ox + 296, oy + 328, 2, {20, 0x101010, 0, bold});
    const std::array<const char *, 3> big = in_game_ ? std::array<const char *, 3>{"重新遊戲", "認輸投降", "結束遊戲"}
                                                     : std::array<const char *, 3>{"日期更改", "熱鍵設定", "遊戲說明"};
    const std::array<graphics::Rect, 3> big_rects{at(227, 14, 327, 49), at(227, 68, 327, 103), at(227, 119, 327, 154)};
    for (std::size_t i = 0; i < 3; ++i) {
        // Out of a game only 遊戲說明 (fcn_00411a96 -> _rich4_ui_help_entry(-1, -1)) is ported.
        const bool enabled = in_game_ ? i != 1 : i == 2 && bool(actions.help);
        text(frame, big[i], (big_rects[i].left + big_rects[i].right) / 2, (big_rects[i].top + big_rects[i].bottom) / 2,
             2, enabled ? Style{16, 0x101010, 0, bold} : Style{16, 0x8a8070, 0, bold});
        if (enabled && frame.pointer_down && frame.pointer_in(big_rects[i]) && confirm_ < 0)
            frame.picture('d', 3, 6, big_rects[i].left, big_rects[i].top);
    }
    // State (fcn_0040fd49).
    const int speed_index = state_.speed >= 4 ? 2 : state_.speed >= 2 ? 1 : 0;
    for (int i = 0; i <= speed_index; ++i)
        frame.picture('d', 3, 5, ox + 81 + 16 * i, oy + 17);
    for (int i = 0; i < 4; ++i) {
        if (state_.music)
            frame.picture('d', 3, 5, ox + 89 + 16 * i, oy + 81);
        if (state_.sound)
            frame.picture('d', 3, 5, ox + 89 + 16 * i, oy + 113);
    }
    if (state_.music)
        frame.picture('d', 3, 9, ox + 66, oy + 82);
    if (state_.sound)
        frame.picture('d', 3, 9, ox + 66, oy + 114);
    if (state_.animation) // fcn_0040fd49: cfg+1 on mark at ref_00474c22/26 = (98, 50)
        frame.picture('d', 3, 9, ox + 98, oy + 50);
    frame.picture('d', 3, 9, ox + 98, oy + 146); // the native game always autosaves daily
    // Original fcn_0040fd49: radio dot at (218, 218/250/281) for window modes 0/1/2.
    constexpr std::array<int, 3> view_mark_y{218, 250, 281};
    frame.picture('d', 3, 9, ox + 218, oy + view_mark_y[state_.view_mode]);
    constexpr std::array<const char *, 8> songs{"1.星際總動員", "2.重回侏儸紀", "3.夢幻伊甸園", "4.打拼為將來",
                                                "5.椰林風情畫", "6.浪漫月世界", "7.熱情的夏夜", "8.漫步星空下"};
    for (int i = 0; i < 8; ++i) {
        if (actions.playing_song == unsigned(i + 1) && state_.music)
            ui::fill(frame.canvas, at(18, 226 + 15 * i, 18 + 159, 226 + 15 * i + 14), rgb(0xff0000));
        text(frame, songs[std::size_t(i)], ox + 26, oy + 233 + 15 * i, 5, {12, 0xf0f0f0, 0x101010, shadow | bold});
    }
    if (frame.pointer_down && confirm_ < 0) {
        if (frame.pointer_in(at(194, 314, 256, 344)))
            frame.picture('d', 3, 3, ox + 194, oy + 314);
        if (frame.pointer_in(at(266, 314, 328, 344)))
            frame.picture('d', 3, 4, ox + 266, oy + 314);
    }
    if (confirm_ >= 0) {
        popup(host, confirm_ == 0 ? "確定要重新遊戲嗎？" : "確定要結束遊戲嗎？", 186);
        const graphics::Rect yes{250, 240, 306, 271}, no{334, 240, 390, 271};
        frame.picture('d', 3, 14, yes.left, yes.top);
        frame.picture('d', 3, 14, no.left, no.top);
        text(frame, "是", 278, 255, 2, white_text);
        text(frame, "否", 362, 255, 2, white_text);
        hit(host, ids::options_confirm_yes, yes, click_sound, [this, actions] {
            const auto which = confirm_;
            confirm_ = -1;
            if (which == 0 && actions.restart)
                actions.restart();
            else if (which == 2 && actions.quit)
                actions.quit();
        });
        hit(host, ids::options_confirm_no, {0, 0, 640, 480}, cancel_sound, [this] { confirm_ = -1; });
        return;
    }
    auto *frame_pointer = &frame;
    hit(host, ids::options_speed, at(81, 9, 135, 41), click_sound, [this, frame_pointer] {
        const int index = std::clamp((frame_pointer->pointer_x - ox - 81) / 16, 0, 2);
        state_.speed = 1U << unsigned(index);
    });
    hit(host, ids::options_volume, at(89, 74, 160, 105), click_sound, [this] { state_.music = true; });
    hit(host, ids::options_volume + 1, at(89, 106, 160, 137), click_sound, [this] { state_.sound = true; });
    hit(host, ids::options_music, at(4, 74, 89, 105), click_sound, [this] { state_.music = !state_.music; });
    hit(host, ids::options_sound, at(4, 106, 89, 137), click_sound, [this] { state_.sound = !state_.sound; });
    hit(host, ids::options_animation, at(4, 42, 160, 73), click_sound, [this] { state_.animation = !state_.animation; });
    // Slightly larger tap targets than the original 217..324 boxes, without overlapping rows.
    // Selection is provisional until 確定, so 取消 leaves the previous layout unchanged.
    for (unsigned mode = 0; mode < 3; ++mode) {
        const int y = 210 + 32 * int(mode);
        hit(host, ids::options_view_first + int(mode), at(206, y, 338, y + 32), click_sound,
            [this, mode] { state_.view_mode = mode; });
    }
    hit(host, ids::options_song, at(18, 226, 177, 346), click_sound, [actions, frame_pointer] {
        const int index = std::clamp((frame_pointer->pointer_y - oy - 226) / 15, 0, 7);
        if (actions.play_song)
            actions.play_song(unsigned(index + 1));
    });
    for (int i = 0; i < 3; ++i)
        hit(host, ids::options_big[std::size_t(i)], big_rects[std::size_t(i)], click_sound,
            [this, i, actions] {
                if (in_game_)
                    confirm_ = i;
                else if (actions.help)
                    actions.help(); // 遊戲說明
            },
            in_game_ ? i != 1 : i == 2 && bool(actions.help));
    hit(host, ids::options_cancel, at(190, 308, 261, 352), exit_sound, actions.close);
    hit(host, ids::options_ok, at(262, 308, 336, 352), click_sound, [this, actions] {
        actions.apply(state_);
        actions.close();
    });
    hit(host, 0, at(0, 0, 347, 363), no_sound, [] {});
    hit(host, ids::options_outside, {0, 0, 640, 480}, cancel_sound, actions.close); // tap outside = right-click cancel
}

// ---------------------------------------------------------------------------------------
// §5 Player information (rich4_ui_player_info_view.asm, Panel 9)

void PlayerInfo::draw(Host &host, const World &world, const std::function<void()> &exit) {
    auto &frame = host.frame;
    if (player_ >= world.players.size())
        player_ = 0;
    ui::fill(frame.canvas, {0, 0, 640, 480}, 0);
    backdrop(frame, 'p', 9, std::size_t(page_));
    const auto &player = world.players[player_];
    // Player tabs.
    for (std::size_t i = 0; i < world.players.size() && i < 4; ++i) {
        const int x = 16 + 88 * int(i);
        if (i == player_)
            frame.picture('p', 9, 3, x, 14);
        else
            frame.picture('p', 9, 4, x, 15);
        text(frame, names.at(world.players[i].state.character), x + 44, 30, 2, {20, 0x101010, 0, bold});
        hit(host, ids::info_player + int(i), {x, 14, x + 88, 48}, click_sound, [this, i] {
            player_ = i;
            scroll_ = 0;
        });
    }
    const bool exit_down = frame.pointer_down && frame.pointer_in({492, 9, 603, 39});
    graphics::blit(frame.canvas, frame.artwork.frame('p', 9, exit_down ? 6 : 5), 547, 23);
    graphics::blit(frame.canvas, frame.artwork.frame('m', 27 + player.state.character, 0), 60, 100);
    if (player.status.god_info && player.status.god_info <= world.objects.size()) {
        constexpr std::array<int, 13> gods{0, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 0, 23};
        const auto &god = world.objects[player.status.god_info - 1];
        if (god.kind < gods.size() && gods[god.kind])
            graphics::blit(frame.canvas, frame.artwork.frame('p', 9, std::size_t(gods[god.kind])), 60, 188);
        text(frame, format("%lld天", god.remaining_days), 60, 234, 2, {16, 0x101010, 0, 0});
    }
    constexpr std::array<const char *, 3> pages{"資產清單", "地產清單", "股票清單"};
    for (int i = 0; i < 3; ++i) {
        if (frame.pointer_down && frame.pointer_in({12, 282 + 64 * i, 110, 323 + 64 * i}))
            frame.picture('p', 9, 12, 12, 282 + 64 * i);
        text(frame, pages[std::size_t(i)], 60, 302 + 64 * i, 2,
             {20, i == page_ ? 0xffffffU : 0xc0c0c0U, 0x101010, shadow | bold});
        hit(host, ids::info_page + i, {12, 282 + 64 * i, 110, 323 + 64 * i}, click_sound, [this, i] {
            page_ = i;
            scroll_ = 0;
        });
    }
    if (page_ == 0)
        draw_assets(host, world);
    else if (page_ == 1)
        draw_properties(host, world);
    else
        draw_stocks(host, world);
    hit(host, ids::info_exit, {492, 0, 610, 48}, exit_sound, exit);
}

void PlayerInfo::draw_assets(Host &host, const World &world) {
    auto &frame = host.frame;
    const auto &player = world.players[player_];
    const auto owner = player_ + 1;
    const Style label{20, 0x101010, 0, bold}, value{28, 0x101010, 0, bold}, count{20, 0x101010, 0, bold};
    const std::array<int, 4> rows{88, 136, 184, 232}, lower{296, 344, 392, 440};
    const std::array<const char *, 4> left{"現  金", "存  款", "貸  款", "總資產"};
    const std::array<const char *, 4> right{"股  票", "點  卷", "保險期", "企  業"};
    const std::array<const char *, 4> bottom{"土  地", "連鎖店", "房  屋", "設  施"};
    std::int64_t stock_value = 0;
    for (std::size_t i = 0; i < world.stocks.size(); ++i)
        stock_value += std::int64_t(player.stocks[i].amount * double(std::bit_cast<float>(world.stocks[i].float_bits[2])));
    int companies = 0, lands = 0, chains = 0, houses = 0, facilities = 0;
    for (const auto &commercial : world.commercials)
        companies += commercial.id && commercial.owner_code == owner;
    for (std::size_t i = 1; i < world.lands.size(); ++i) {
        const auto &land = world.lands[i].economy;
        if (land.owner_code != owner)
            continue;
        ++lands;
        chains += land.chain_store != 0;
        houses += land.level > 0 && !land.chain_store;
    }
    for (std::size_t i = 1; i < world.facilities.size(); ++i)
        facilities += world.facilities[i].owner_code == owner;
    const std::array<std::string, 4> left_values{
        ui::money(player.state.cash), ui::money(player.state.bank_balance), ui::money(player.state.loan),
        ui::money(stocks::player_wealth(world, player_))};
    const std::array<std::string, 4> right_values{ui::money(stock_value), grouped(player.state.points),
                                                  format("%lld天", player.status.days_assurance),
                                                  format("×%lld", companies)};
    const std::array<int, 4> counts{lands, chains, houses, facilities};
    for (std::size_t i = 0; i < 4; ++i) {
        text(frame, left[i], 142, rows[i], 2, label);
        text(frame, left_values[i], 330, rows[i], 6, value);
        text(frame, right[i], 430, rows[i], 2, label);
        text(frame, right_values[i], 602, rows[i], 6, value);
        text(frame, bottom[i], 142, lower[i], 2, label);
        text(frame, format("×%lld", counts[i]), 250, lower[i], 6, count);
    }
    // Tool grid (Panel 74 icons) and the 15 card slots in hand order.
    int cell = 0;
    for (std::size_t tool = 0; tool < services::tool_names.size() && cell < 15; ++tool) {
        if (!player.state.tool_amounts[tool])
            continue;
        // Original rich4.asm: player-info tool grid starts at y=281, not 298.
        const int cx = 300 + 72 * (cell % 5), cy = 281 + 32 * (cell / 5);
        if (tool < 13)
            graphics::blit(frame.canvas, frame.artwork.frame('p', 74, tool), cx - 16, cy);
        text(frame, format("×%lld", player.state.tool_amounts[tool]), cx + 30, cy, 6, white_text);
        ++cell;
    }
    cell = 0;
    for (const auto card : player.state.cards) {
        if (card >= 1 && card <= services::card_names.size()) {
            // The card grid starts at y=385 in the original player-info screen.
            const int cx = 300 + 72 * (cell % 5), cy = 385 + 32 * (cell / 5);
            text(frame, services::card_names[card - 1], cx, cy, 2, {14, 0xffffff, 0x101010, shadow | bold});
        }
        ++cell;
    }
}

void PlayerInfo::draw_properties(Host &host, const World &world) {
    auto &frame = host.frame;
    const auto owner = player_ + 1;
    constexpr std::array<const char *, 5> tabs{"全  部", "住宅區", "商業區", "房  屋", "連鎖店"};
    for (int i = 0; i < 5; ++i) {
        if (i == category_)
            frame.picture('p', 9, 11, 120 + 75 * i, 64);
        text(frame, tabs[std::size_t(i)], 157 + 75 * i, 80, 2, {16, 0x101010, 0, bold});
        hit(host, ids::info_category + i, {120 + 75 * i, 64, 195 + 75 * i, 98}, click_sound, [this, i] {
            category_ = i;
            scroll_ = 0;
        });
    }
    const std::array<std::pair<const char *, int>, 5> headers{
        {{"地  點", 168}, {"開發狀況", 264}, {"價  格", 356}, {"收  費", 448}, {"租  期", 540}}};
    for (const auto &[label, x] : headers)
        text(frame, label, x, 112, 2, {16, 0x101010, 0, bold});
    struct Row {
        std::string place, development;
        std::int64_t price, toll;
        std::uint32_t expiry;
    };
    std::vector<Row> rows;
    std::vector<LandEconomyRecord> records;
    for (std::size_t i = 1; i < world.lands.size(); ++i)
        records.push_back(world.lands[i].economy);
    const auto index = world.header.price_index;
    for (std::size_t i = 1; i < world.lands.size(); ++i) {
        const auto &land = world.lands[i];
        if (land.economy.owner_code != owner)
            continue;
        const bool chain = land.economy.chain_store != 0;
        if ((category_ == 1 && chain) || category_ == 2 || (category_ == 3 && (chain || !land.economy.level)) ||
            (category_ == 4 && !chain))
            continue;
        const auto street = ui::big5(land.economy.street_name);
        const auto toll = chain ? land_toll(records, owner, index)
                                : land_toll(records, owner, index, std::string_view(land.economy.street_name));
        rows.push_back({street, chain ? "連鎖店" : land.economy.level ? format("%lld層", land.economy.level) : "空地",
                        (std::int64_t(land.land_price) + std::int64_t(land.price_per_level) * land.economy.level) * index,
                        toll, land.expired_date});
    }
    if (category_ == 0 || category_ == 2)
        for (std::size_t i = 1; i < world.facilities.size(); ++i) {
            const auto &facility = world.facilities[i];
            if (facility.owner_code != owner)
                continue;
            rows.push_back({ui::big5(facility.name), format("%lld級", facility.level),
                            (std::int64_t(facility.land_price) + std::int64_t(facility.price_per_level()) * facility.level) *
                                index,
                            std::int64_t(facility.toll_by_level[std::min<std::size_t>(facility.level, 5)]) * index,
                            facility.expired_date});
        }
    if (scroll_ > rows.size())
        scroll_ = 0;
    const Style cell{16, 0x101010, 0, bold};
    for (std::size_t r = 0; r < 10 && scroll_ + r < rows.size(); ++r) {
        const auto &row = rows[scroll_ + r];
        const int y = 144 + 32 * int(r);
        text(frame, row.place, 168, y, 2, cell);
        text(frame, row.development, 264, y, 2, cell);
        text(frame, financial(row.price), 398, y, 6, cell);
        text(frame, financial(row.toll), 490, y, 6, cell);
        const auto date = calendar::unpack(row.expiry);
        text(frame, row.expiry ? format("%02lld/", date.month) + std::to_string(date.day) + "/" + std::to_string(date.year)
                               : std::string("無限期"),
             540, y, 2, cell);
    }
    if (frame.pointer_down && frame.pointer_in({593, 369, 624, 400}))
        frame.picture('p', 9, 7, 593, 369);
    if (frame.pointer_down && frame.pointer_in({593, 417, 624, 448}))
        frame.picture('p', 9, 8, 593, 417);
    hit(host, ids::info_up, {586, 360, 634, 408}, click_sound, [this] { scroll_ = scroll_ >= 10 ? scroll_ - 10 : 0; });
    hit(host, ids::info_down, {586, 409, 634, 457}, click_sound,
        [this, total = rows.size()] {
            if (scroll_ + 10 < total)
                scroll_ += 10;
        });
}

void PlayerInfo::draw_stocks(Host &host, const World &world) {
    auto &frame = host.frame;
    const auto &player = world.players[player_];
    const Style header{16, 0x101010, 0, bold};
    text(frame, "股票名稱", 204, 68, 2, header);
    text(frame, "持有張數", 332, 68, 2, header);
    text(frame, "總 市 價", 476, 68, 2, header);
    int row = 0;
    const auto offset = std::size_t(world.header.stage_id * 4 + world.header.map_id) * 12;
    for (std::size_t i = 0; i < world.stocks.size() && row < 12; ++i) {
        const auto amount = player.stocks[i].amount;
        if (amount <= 0)
            continue;
        const int y = 100 + 32 * row++;
        const auto value = std::int64_t(amount * double(std::bit_cast<float>(world.stocks[i].float_bits[2])));
        text(frame, native_stock_names.at(offset + i), 204, y, 2, header);
        text(frame, grouped(amount), 380, y, 6, header);
        text(frame, financial(value), 540, y, 6, header);
    }
}

} // namespace rich4::native::screens
