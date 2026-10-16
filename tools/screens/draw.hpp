// SPDX-License-Identifier: GPL-3.0-or-later
// Drawing helpers shared by the original-style menu and finance screens: the original
// rich4_create_font styles (shadow / bold / outline), rich4_draw_text alignments,
// brightness-table darkening and raw 640x480 pictures (docs/ui/finance-menus.md §0).
#pragma once

#include "../native_frame.hpp"
#include "rich4/world.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace rich4::native::screens {

// 0xRRGGBB (as written in the original font calls) to the canvas' RGB555.
constexpr std::uint16_t rgb(std::uint32_t value) {
    return ui::color((value >> 16) & 255U, (value >> 8) & 255U, value & 255U);
}

enum : int { shadow = 1, bold = 2, outline = 4 };

// Mirrors rich4_create_font(size, colour, colour2, flags, spacing).
struct Style {
    int size = 16;
    std::uint32_t colour = 0xf0f0f0;
    std::uint32_t colour2 = 0x101010;
    int flags = shadow | bold;
};

inline constexpr Style white_text{16, 0xf0f0f0, 0x101010, shadow | bold}; // the workhorse (16, f0f0f0, 101010, 3)
inline constexpr Style black_text{16, 0x101010, 0, bold};

// Things a screen needs from the application besides the frame.
struct Host {
    Frame &frame;
    std::function<void(int)> sound;               // Effect.mkf entry; respects the sound toggle
    std::function<void(std::string_view)> toast;  // short notice
    bool instant = false;                         // headless replay: skip waits and animations
    std::function<void(std::size_t)> speech{};    // AUDIO: Speaking.mkf clip (clerk lines); respects the sound toggle
};

// Text width of one line in the style.
int measure(Frame &frame, std::string_view text, const Style &style);
// rich4_draw_text alignments 0..7 (finance-menus.md §0 "Text placement").
void text(Frame &frame, std::string_view value, int x, int y, int align, const Style &style);

// fcn_004552e7: darken through the brightness table (level -16 is about 50 %).
void darken(graphics::Image &image, graphics::Rect rect, int level);
// fcn_004553da: greyscale a rectangle.
void greyscale(graphics::Image &image, graphics::Rect rect);
// Raw headerless RGB555 picture (jump.mkf 0..7, Panel 22 hit map uses bytes instead).
graphics::Image raw_picture(Artwork &artwork, char archive, std::size_t entry, int width, int height);
// Festival picture for the world's date (Data 4..127, raw 200x200, holidays::festival_picture),
// drawn instead of the Panel 2 season picture; nullptr on ordinary days. Cached per entry.
const graphics::Image *festival_picture(Artwork &artwork, const World &world);
// Copies the sub-rectangle `source` of image (frame-local) to (x, y), honouring transparency.
void blit_part(graphics::Image &canvas, const graphics::Image &image, graphics::Rect source, int x, int y);
// fcn_0045620f style 1-pixel rectangle outline.
void outline_rect(graphics::Image &canvas, graphics::Rect rect, std::uint16_t colour);
// Generic "pressed" bevel of fcn_00451b9e: content moves 1 px down-right, top/left darken.
void pressed_bevel(graphics::Image &canvas, graphics::Rect rect);

// Nearest-neighbour integer upscale of an artwork frame (cached), for finger-sized controls.
const graphics::Image &scaled(Artwork &artwork, char archive, std::size_t entry, std::size_t index, int scale);
// Full-screen picture drawn opaque at (0,0), like fcn_004563f5 (colour 0 stays black).
void backdrop(Frame &frame, char archive, std::size_t entry, std::size_t index);
// Panel 73:5 prompt box (184x88) with centred black text, top-left (228, y).
void popup(Host &host, std::string_view message, int y = 196);

// "12,345" (num_to_currency without the dollar sign).
std::string grouped(std::int64_t amount);
// Compact non-dollar financial figures (not share counts or input amounts).
inline std::string financial(std::int64_t amount) { return ui::compact_money(amount, false); }

// Original UI sounds (Effect.mkf entries, finance-menus.md §0 "UI sound table").
enum Effect : int { hover_sound = 0, click_sound = 1, exit_sound = 2, error_sound = 3, cancel_sound = 4,
                    key_sound = 7, gauge_sound = 9, no_sound = -1 };

// Registers a hit area whose press plays `effect` instead of the application's generic click.
void hit(Host &host, int id, graphics::Rect rect, int effect, std::function<void()> action, bool enabled = true);

} // namespace rich4::native::screens
