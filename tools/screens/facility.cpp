// SPDX-License-Identifier: GPL-3.0-or-later
// Facility-type picker (fcn_00440aac / fcn_0043fae4), see facility.hpp and docs/ui/facility-build.md.
#include "facility.hpp"

#include "messages.hpp"

#include <algorithm>
#include <array>
#include <string_view>

namespace rich4::native::facility {
namespace {

constexpr std::string_view prompt = "請選擇設施類別"; // 0x465289
// 0x475150: facility type names, drawn in the box for the hovered slot.
constexpr std::array<std::string_view, 5> names{"公  園", "旅  館", "購物中心", "加油站", "研究所"};
// create_font(0x10, 0xf0f0f0, 0x101010, 3, 1) in fcn_00440aac: bold 16 px white, drop shadow.
constexpr ui::TextStyle box_style{16, ui::color(240, 240, 240), true, true, false, ui::color(16, 16, 16)};

constexpr int strip_x = 43, strip_y = 279; // Data 517:4 anchor (0x2b, 0x117)
constexpr int slot_x = 50, slot_y = 286, slot_size = 68; // hit zone x 0x32..0x186, y 0x11e..0x162

std::optional<std::size_t> option_with_value(const GameDecision &decision, std::int32_t value) {
    for (std::size_t i = 0; i < decision.options.size(); ++i)
        if (decision.options[i].value == value)
            return i;
    return std::nullopt;
}

std::optional<std::size_t> cancel_option(const GameDecision &decision) {
    return decision.kind == DecisionKind::action_target ? option_with_value(decision, -1) : std::nullopt;
}

} // namespace

bool handles(const GameDecision &decision) {
    if (decision.kind == DecisionKind::research_tool || decision.kind == DecisionKind::construct_facility)
        return true;
    return decision.kind == DecisionKind::action_target && decision.prompt == prompt &&
           std::all_of(decision.options.begin(), decision.options.end(),
                       [](const auto &option) { return !option.location && option.value >= -1 && option.value <= 4; });
}

bool Picker::cancellable(const GameDecision &decision) {
    return cancel_option(decision).has_value();
}

void Picker::cancel(screens::Host &host, const GameDecision &decision,
                    const std::function<void(std::size_t)> &respond) {
    if (const auto index = cancel_option(decision)) {
        host.sound(screens::cancel_sound); // right-click: Effect 4 (0x482332), result -1
        respond(*index);
    }
}

void Picker::draw(screens::Host &host, const GameDecision &decision, const std::string &key,
                  const std::function<void(std::size_t)> &respond) {
    auto &frame = host.frame;
    if (key != key_) {
        key_ = key;
        selected_.reset();
    }
    if (decision.kind == DecisionKind::research_tool) {
        // Same original-art selection flow as building on a large lot: Data 517:5 message
        // board, a row of five icon slots, yellow triple-outline selection and Effect 0/1.
        // The original tool inventory is Panel 11:1 with icons at Panel 11:(tool_id+1).
        // This deliberately avoids a separate native-styled modal on mobile.
        graphics::blit(frame.canvas, frame.artwork.frame('d', 517, 5), 220, 140);
        messages::draw_text(frame, "請選擇研發道具", 220, 122, 2, box_style);
        constexpr int tray_x = 14, tray_y = 279;
        graphics::blit(frame.canvas, frame.artwork.frame('p', 11, 1), tray_x, tray_y,
                       graphics::Transparency::opaque);
        for (int slot = 0; slot < 5; ++slot) {
            const auto index = option_with_value(decision, 9 + slot);
            const int x = tray_x + 6 + slot * 80, y = tray_y + 6;
            const graphics::Rect cell{x, y, x + 78, y + 54};
            graphics::blit(frame.canvas, frame.artwork.frame('p', 11, std::size_t(10 + slot)),
                           tray_x + 29 + slot * 80, tray_y + 33);
            if (!index) {
                screens::darken(frame.canvas, cell, -16); // higher-level tools are visible but locked
                continue;
            }
            if (selected_ == slot)
                for (int inset = 0; inset < 3; ++inset)
                    screens::outline_rect(frame.canvas,
                                          {x + inset, y + inset, cell.right - inset, cell.bottom - inset},
                                          screens::rgb(0xffff00));
            const bool chosen = selected_ == slot;
            screens::hit(host, 210 + int(*index), cell, chosen ? screens::click_sound : screens::hover_sound,
                         [this, chosen, slot, index = *index, respond] {
                             if (chosen)
                                 respond(index);
                             else
                                 selected_ = slot;
                         });
        }
        const auto selected_index = selected_ ? option_with_value(decision, 9 + *selected_) : std::nullopt;
        if (selected_index)
            messages::draw_text(frame, decision.options[*selected_index].label, 220, 154, 4, box_style);
        // The Panel 11 tool tray occupies x=14..426, y=279..459. The
        // previous confirmation buttons were painted *inside* its second and
        // third rows, covering original textures and labels on Android.
        // Put the touch-only controls in the unused right-hand sidebar.
        const graphics::Rect confirm_rect{458, 305, 604, 350};
        frame.button(confirm_button, confirm_rect, "確定", [] {}, selected_index.has_value());
        frame.buttons.pop_back();
        screens::hit(host, confirm_button, confirm_rect, screens::click_sound,
                     [respond, selected_index] {
                         if (selected_index)
                             respond(*selected_index);
                     },
                     selected_index.has_value());
        if (const auto postpone = option_with_value(decision, 0)) {
            const graphics::Rect postpone_rect{458, 367, 604, 412};
            frame.button(210 + int(*postpone), postpone_rect, "稍後研究", [] {});
            frame.buttons.pop_back();
            screens::hit(host, 210 + int(*postpone), postpone_rect, screens::cancel_sound,
                         [respond, index = *postpone] { respond(index); });
        }
        return;
    }
    // fcn_00440aac: box Data 517:5 (transparent) at (220,140), prompt mode 2 at (220,122); the strip
    // Data 517:4 opaque at (43,279). Slot i (facility type i) covers x 50+68i, y 286, 68x68.
    graphics::blit(frame.canvas, frame.artwork.frame('d', 517, 5), 220, 140);
    messages::draw_text(frame, prompt, 220, 122, 2, box_style);
    graphics::blit(frame.canvas, frame.artwork.frame('d', 517, 4), strip_x, strip_y, graphics::Transparency::opaque);
    for (int slot = 0; slot < 5; ++slot) {
        const int x = slot_x + slot * slot_size;
        const auto index = option_with_value(decision, slot);
        // Finger targets: the slot's column over the strip's full height (edges to the frame).
        const graphics::Rect cell{slot == 0 ? strip_x : x, strip_y, slot == 4 ? x + slot_size + 7 : x + slot_size,
                                  strip_y + 83};
        if (!index) { // a type the core does not offer here is dimmed and inert
            screens::darken(frame.canvas, {x, slot_y, x + slot_size, slot_y + slot_size}, -16);
            continue;
        }
        if (selected_ == slot) {
            // Hover (fcn_0043fae4 WM_MOUSEMOVE): three nested 0xffff00 frames 68/66/64 px.
            for (int inset = 0; inset < 3; ++inset)
                screens::outline_rect(frame.canvas,
                                      {x + inset, slot_y + inset, x + slot_size - inset, slot_y + slot_size - inset},
                                      screens::rgb(0xffff00));
        }
        const bool chosen = selected_ == slot;
        screens::hit(host, 210 + int(*index), cell, chosen ? screens::click_sound : screens::hover_sound,
                     [this, slot, chosen, index = *index, respond] {
                         if (chosen)
                             respond(index); // WM_LBUTTONUP: Effect 1 (0x482322), returns the slot
                         else
                             selected_ = slot; // entering a slot: Effect 0 (0x48231a)
                     });
    }
    if (selected_) // the hovered type's name, mode 4 at (220,154)
        messages::draw_text(frame, names[std::size_t(*selected_)], 220, 154, 4, box_style);
    // NATIVE: touch has no hover/right-click: 確定 commits the selection; 取消 replaces the
    // right-click cancel (改建卡 only; building on a lot cannot be cancelled in the original).
    const auto cancel = cancel_option(decision);
    const int left = cancel ? 128 : 180;
    const auto selected_index = selected_ ? option_with_value(decision, *selected_) : std::nullopt;
    frame.button(confirm_button, {left, 372, left + 80, 412}, "確定", [] {}, selected_index.has_value());
    frame.buttons.pop_back(); // re-register with the original click sound instead of the generic one
    screens::hit(host, confirm_button, {left, 372, left + 80, 412}, screens::click_sound,
                 [respond, selected_index] {
                     if (selected_index)
                         respond(*selected_index);
                 },
                 selected_index.has_value());
    if (cancel) {
        frame.button(210 + int(*cancel), {232, 372, 312, 412}, "取消", [] {});
        frame.buttons.pop_back();
        screens::hit(host, 210 + int(*cancel), {232, 372, 312, 412}, screens::cancel_sound,
                     [respond, index = *cancel] { respond(index); });
    }
}

} // namespace rich4::native::facility
