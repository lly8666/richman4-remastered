// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai_settings.hpp"

#include "rich4/world.hpp"

#include <algorithm>
#include <array>

namespace rich4::native::screens {
namespace {
using graphics::Rect;

// _rich4_create_font(20, f0f0f0, 101010, 3) / (16, ...) / (20, 101010, 0, 2) of the window init.
constexpr Style title_font{20, 0xf0f0f0, 0x101010, shadow | bold};
constexpr Style label_font{16, 0xf0f0f0, 0x101010, shadow | bold};
constexpr Style button_font{20, 0x101010, 0, bold};
constexpr std::array<int, 3> personality_y{195, 227, 259}; // red dot rows (ref_004752b8)
constexpr std::array<int, 3> personality_label{203, 235, 268};

// Finger-sized hit areas: the original rects are as small as 11x22 (arrows) and 96x18 (options);
// each is widened to its visible artwork / row (rows are only ~32 px apart, so they share the gap).
Rect tab_rect(std::size_t slot) {
    return {110, 70 + 83 * int(slot), 226, 153 + 83 * int(slot)};
}
Rect option_rect(int centre_y) {
    return {280, centre_y - 15, 420, centre_y + 16};
}
constexpr Rect cards_rect{280, 100, 420, 130}, tools_rect{280, 131, 420, 161};
constexpr std::array<Rect, 2> down_rect{{{266, 322, 310, 356}, {266, 357, 310, 391}}};
constexpr std::array<Rect, 2> up_rect{{{390, 322, 436, 356}, {390, 357, 436, 391}}};
constexpr std::array<Rect, 2> bar_rect{{{310, 322, 390, 356}, {310, 357, 390, 391}}};
constexpr Rect ok_rect{468, 150, 532, 226}, cancel_rect{468, 238, 532, 314};

std::uint8_t &ratio(AiSettings &settings, int row) {
    return row ? settings.stock_ratio : settings.cash_ratio;
}
} // namespace

bool AiSettingsScreen::open(const GameSession &session, std::size_t current) {
    edits_ = {};
    original_ = {};
    std::optional<std::size_t> first;
    for (std::size_t slot = 0; slot < edits_.size(); ++slot)
        if ((original_[slot] = edits_[slot] = session.ai_settings(slot)) && !first)
            first = slot;
    if (!first)
        return false;
    selected_ = current < edits_.size() && edits_[current] ? current : *first;
    return true;
}

void AiSettingsScreen::cancel(Host &host, const AiSettingsActions &actions) {
    host.sound(cancel_sound);
    actions.close();
}

void AiSettingsScreen::draw(Host &host, const World &world, const AiSettingsActions &actions) {
    auto &frame = host.frame;
    const auto picture = [&frame](std::size_t index, int x, int y) { // anchor (fcn_00456418)
        graphics::blit(frame.canvas, frame.artwork.frame('p', 77, index), x, y);
    };
    picture(0, 102, 62);
    text(frame, "託管AI", 275, 88, 2, title_font);
    text(frame, "個  性", 275, 176, 2, title_font);
    text(frame, "資金運用比例", 351, 308, 2, title_font);
    text(frame, "使用卡片", 346, 115, 2, label_font);
    text(frame, "使用道具", 346, 146, 2, label_font);
    text(frame, "乖寶寶", 346, personality_label[0], 2, label_font);
    text(frame, "普通人", 346, personality_label[1], 2, label_font);
    text(frame, "大老奸", 346, personality_label[2], 2, label_font);
    text(frame, "現金", 293, 339, 6, label_font);
    text(frame, "存款", 407, 339, 5, label_font);
    text(frame, "股票", 293, 372, 6, label_font);
    text(frame, "資金", 407, 372, 5, label_font);
    text(frame, "確定", 499, 187, 3, button_font);
    text(frame, "取消", 499, 275, 3, button_font);
    // Original rich4_ui_ai_settings.asm: 77:1 is a normal tab, 77:2 is selected.
    for (std::size_t slot = 0; slot < edits_.size() && slot < world.players.size(); ++slot) {
        if (!edits_[slot])
            continue;
        const int y = 70 + 83 * int(slot);
        picture(slot == selected_ ? 2 : 1, 110, y);
        picture(6 + std::size_t(world.players[slot].state.character % 12), 182, 110 + 83 * int(slot));
        if (edits_[slot]->autopilot)
            picture(3, 121, 106 + 83 * int(slot));
        hit(host, ids::ai_tab + int(slot), tab_rect(slot), click_sound, [this, slot] {
            if (selected_ == slot && edits_[slot])
                edits_[slot]->autopilot = !edits_[slot]->autopilot;
            selected_ = slot;
        });
    }
    if (selected_ >= edits_.size() || !edits_[selected_])
        return;
    auto &edit = *edits_[selected_];
    if (edit.card_strategy & 1U)
        picture(3, 288, 107);
    if (edit.card_strategy & 2U)
        picture(3, 288, 139);
    picture(3, 288, personality_y[std::min<std::size_t>(edit.personality, 2)]);
    hit(host, ids::ai_cards, cards_rect, click_sound, [&edit] { edit.card_strategy ^= 1U; });
    hit(host, ids::ai_tools, tools_rect, click_sound, [&edit] { edit.card_strategy ^= 2U; });
    for (int p = 0; p < 3; ++p)
        hit(host, ids::ai_personality + p, option_rect(personality_label[std::size_t(p)]), click_sound,
            [&edit, p] { edit.personality = std::uint8_t(p); });
    // 現金/存款 and 股票/資金 bars: ten 7x22 red cells per row; arrows step by 10, the bar is dragged.
    for (int row = 0; row < 2; ++row) {
        auto &value = ratio(edit, row);
        const auto bar = bar_rect[std::size_t(row)];
        if (frame.pointer_down && frame.pointer_in(bar)) // drag: value = ((x-310)/8)*10
            value = std::uint8_t(std::clamp((frame.pointer_x - 310) / 8 * 10, 0, 100));
        const int top = row ? 361 : 328;
        for (int k = 0; k < 10; ++k)
            if (value > k * 10)
                ui::fill(frame.canvas, {311 + 8 * k, top, 318 + 8 * k, top + 22}, rgb(0xff0000));
        if (frame.pointer_down && frame.pointer_in(down_rect[std::size_t(row)]))
            picture(4, 299, top + 1);
        if (frame.pointer_down && frame.pointer_in(up_rect[std::size_t(row)]))
            picture(5, 393, top + 1);
        hit(host, row ? ids::ai_stock_down : ids::ai_cash_down, down_rect[std::size_t(row)], click_sound,
            [&value] { value = std::uint8_t(std::max(0, value - 10)); });
        hit(host, row ? ids::ai_stock_up : ids::ai_cash_up, up_rect[std::size_t(row)], click_sound,
            [&value] { value = std::uint8_t(std::min(100, value + 10)); });
        hit(host, row ? ids::ai_stock_bar : ids::ai_cash_bar, bar, no_sound, [] {}); // drag handled above
    }
    if (frame.pointer_down && frame.pointer_in(ok_rect))
        pressed_bevel(frame.canvas, {479, 159, 519, 215});
    if (frame.pointer_down && frame.pointer_in(cancel_rect))
        pressed_bevel(frame.canvas, {479, 247, 519, 303});
    hit(host, ids::ai_ok, ok_rect, exit_sound, [this, actions] {
        for (std::size_t slot = 0; slot < edits_.size(); ++slot)
            if (edits_[slot] && edits_[slot] != original_[slot])
                actions.apply(slot, *edits_[slot]);
        actions.close();
    });
    hit(host, ids::ai_cancel, cancel_rect, cancel_sound, actions.close);
}

} // namespace rich4::native::screens
