// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rich4/graphics.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H
#include <iconv.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace rich4::ui {

inline std::filesystem::path asset_path(const std::filesystem::path &directory, std::string name) {
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        auto candidate = entry.path().filename().string();
        std::transform(candidate.begin(), candidate.end(), candidate.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        if (candidate == name && entry.is_regular_file())
            return entry.path();
    }
    throw std::runtime_error("Cannot find " + name + " in " + directory.string());
}

inline std::vector<std::uint8_t> read_file(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot read " + path.string());
    return {std::istreambuf_iterator<char>(input), {}};
}

inline void write_file(const std::filesystem::path &path, const std::vector<std::uint8_t> &data) {
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path());
    const auto temporary = std::filesystem::path(path.string() + ".tmp");
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
    output.close();
    if (!output)
        throw std::runtime_error("Cannot write " + path.string());
    std::filesystem::rename(temporary, path);
}

// The original data is Windows Big5 (code page 950): e.g. 張恒市 on the China map uses the ETEN
// extension 0xF9DA, which strict "BIG5" (GNU libiconv on Android) rejects. Convert as CP950, falling
// back to BIG5 where CP950 is unavailable, and replace only an undecodable character with "?".
inline std::string big5(std::string_view input) {
    iconv_t converter = iconv_open("UTF-8", "CP950");
    if (converter == (iconv_t)-1)
        converter = iconv_open("UTF-8", "BIG5");
    if (converter == (iconv_t)-1)
        throw std::runtime_error("Big5 conversion is unavailable");
    std::string result(input.size() * 4 + 4, '\0');
    char *source = const_cast<char *>(input.data());
    char *destination = result.data();
    std::size_t source_left = input.size(), destination_left = result.size();
    while (source_left > 0) {
        if (iconv(converter, &source, &source_left, &destination, &destination_left) != std::size_t(-1))
            break;
        if (destination_left < 1)
            break;
        // Skip the offending character (a lead byte takes its trail byte with it) and mark it.
        const auto skip = static_cast<unsigned char>(*source) >= 0x81 && source_left >= 2 ? 2U : 1U;
        source += skip;
        source_left -= skip;
        *destination++ = '?';
        --destination_left;
        iconv(converter, nullptr, nullptr, nullptr, nullptr); // reset the shift state
    }
    iconv_close(converter);
    result.resize(result.size() - destination_left);
    return result;
}

inline std::u32string codepoints(std::string_view text) {
    std::u32string result;
    for (std::size_t offset = 0; offset < text.size();) {
        const auto first = static_cast<unsigned char>(text[offset++]);
        char32_t value = first;
        int remaining = 0;
        if (first >= 0xf0) {
            value &= 7;
            remaining = 3;
        } else if (first >= 0xe0) {
            value &= 15;
            remaining = 2;
        } else if (first >= 0xc0) {
            value &= 31;
            remaining = 1;
        } else if (first >= 0x80) {
            result.push_back(U'?');
            continue;
        }
        bool valid = true;
        for (int i = 0; i < remaining; ++i) {
            if (offset >= text.size() || (static_cast<unsigned char>(text[offset]) & 0xc0) != 0x80) {
                valid = false;
                break;
            }
            value = (value << 6) | (static_cast<unsigned char>(text[offset++]) & 63);
        }
        result.push_back(valid && value <= 0x10ffff ? value : U'?');
    }
    return result;
}

inline constexpr std::uint16_t color(unsigned r, unsigned g, unsigned b) {
    return static_cast<std::uint16_t>((r >> 3) << 10 | (g >> 3) << 5 | (b >> 3));
}
// A player's colour (PlayerState::color, player+4) is 0x00RRGGBB from the character table. The
// original converts it with _rich4_convert_color (RGB555: fcn_0045523e keeps the top 5 bits of each
// byte) for the board's SPR palette slot 255 (rich4.asm:2599), the info-panel bars (8020, 8508)
// and the minimap dots (3752). It is never a ready RGB555 value.
inline constexpr std::uint16_t player_color(std::uint32_t rgb) {
    return color((rgb >> 16) & 255U, (rgb >> 8) & 255U, rgb & 255U);
}
inline constexpr auto ink = color(38, 30, 20);
inline constexpr auto paper = color(247, 232, 185);
inline constexpr auto blue = color(20, 65, 135);

inline void fill(graphics::Image &image, graphics::Rect rect, std::uint16_t value) {
    rect.left = std::clamp(rect.left, 0, image.width);
    rect.right = std::clamp(rect.right, 0, image.width);
    rect.top = std::clamp(rect.top, 0, image.height);
    rect.bottom = std::clamp(rect.bottom, 0, image.height);
    if (rect.left >= rect.right || rect.top >= rect.bottom)
        return;
    for (int y = rect.top; y < rect.bottom; ++y)
        std::fill(image.pixels.begin() + y * image.width + rect.left,
                  image.pixels.begin() + y * image.width + rect.right, value);
}

inline void border(graphics::Image &image, graphics::Rect rect, std::uint16_t value) {
    fill(image, {rect.left, rect.top, rect.right, rect.top + 1}, value);
    fill(image, {rect.left, rect.bottom - 1, rect.right, rect.bottom}, value);
    fill(image, {rect.left, rect.top, rect.left + 1, rect.bottom}, value);
    fill(image, {rect.right - 1, rect.top, rect.right, rect.bottom}, value);
}

inline bool contains(graphics::Rect rect, int x, int y) {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

// Original _rich4_create_font styles: weight 700 (bold) unless flags==2, flags&1 drop shadow
// in `effect` at (+1,+1), flags&4 four-direction outline in `effect` with the text at (+1,+1).
struct TextStyle {
    int size = 16;
    std::uint16_t color = 0;
    bool bold = false;
    bool shadow = false;
    bool outline = false;
    std::uint16_t effect = 0;
    int line_gap = 2; // GDI lines are `size` apart; a small gap keeps CJK lines legible.
};

class Font {
  public:
    explicit Font(const std::filesystem::path &path) {
        if (FT_Init_FreeType(&library_))
            throw std::runtime_error("Cannot initialize FreeType");
        if (FT_New_Face(library_, path.string().c_str(), 0, &face_)) {
            FT_Done_FreeType(library_);
            throw std::runtime_error("Cannot load Chinese font: " + path.string());
        }
    }
    ~Font() {
        FT_Done_Face(face_);
        FT_Done_FreeType(library_);
    }
    Font(const Font &) = delete;
    Font &operator=(const Font &) = delete;

    int measure(std::string_view text, int size = 16, bool monochrome = false) {
        int width = 0;
        for (const auto character : codepoints(text))
            width += glyph(character, size, monochrome).advance;
        return width;
    }

    // Ink bounding box of a single line drawn with draw(text, 0, 0, size), plus `spacing` extra
    // pixels after each glyph. Empty text yields an empty rectangle at the origin.
    graphics::Rect ink_bounds(std::string_view text, int size = 16, int spacing = 0, bool monochrome = false) {
        graphics::Rect bounds{0, 0, 0, 0};
        bool any = false;
        int x = 0;
        for (const auto character : codepoints(text)) {
            const auto &bitmap = glyph(character, size, monochrome);
            if (bitmap.width && bitmap.height) {
                const graphics::Rect ink{x + bitmap.left, size - bitmap.top, x + bitmap.left + bitmap.width,
                                         size - bitmap.top + bitmap.height};
                if (!any)
                    bounds = ink;
                else
                    bounds = {std::min(bounds.left, ink.left), std::min(bounds.top, ink.top),
                              std::max(bounds.right, ink.right), std::max(bounds.bottom, ink.bottom)};
                any = true;
            }
            x += bitmap.advance + spacing;
        }
        return bounds;
    }

    void draw(graphics::Image &image, std::string_view text, int x, int y, int size = 16,
              std::uint16_t value = ink, int wrap_width = 0, bool monochrome = false) {
        const int start_x = x;
        for (const auto character : codepoints(text)) {
            if (character == U'\n') {
                x = start_x;
                y += size + 5;
                continue;
            }
            const auto &bitmap = glyph(character, size, monochrome);
            if (wrap_width > 0 && x > start_x && x + bitmap.advance > start_x + wrap_width) {
                x = start_x;
                y += size + 5;
            }
            for (int row = 0; row < bitmap.height; ++row) {
                const int destination_y = y + size - bitmap.top + row;
                if (destination_y < 0 || destination_y >= image.height)
                    continue;
                for (int column = 0; column < bitmap.width; ++column) {
                    const int destination_x = x + bitmap.left + column;
                    if (destination_x < 0 || destination_x >= image.width)
                        continue;
                    const unsigned coverage = bitmap.mask[row * bitmap.width + column];
                    if (!coverage)
                        continue;
                    auto &destination = image.pixels[destination_y * image.width + destination_x];
                    unsigned blended = 0;
                    for (unsigned shift : {0U, 5U, 10U}) {
                        const unsigned foreground = (value >> shift) & 31U;
                        const unsigned background = (destination >> shift) & 31U;
                        blended |= ((foreground * coverage + background * (255U - coverage) + 127U) / 255U)
                                   << shift;
                    }
                    destination = static_cast<std::uint16_t>(blended);
                }
            }
            x += bitmap.advance;
        }
    }

    // Multi-line text block: explicit '\n' breaks only (no wrapping), like DrawTextA
    // without DT_WORDBREAK. Width/height include the bold and shadow/outline pixels.
    struct Extent {
        int width = 0, height = 0;
    };
    Extent measure_block(std::string_view text, const TextStyle &style) {
        Extent extent;
        const int extra = (style.bold ? 1 : 0) + (style.shadow ? 1 : 0) + (style.outline ? 2 : 0);
        std::size_t start = 0;
        int lines = 0;
        while (true) {
            const auto end = text.find('\n', start);
            const auto line = text.substr(start, end == std::string_view::npos ? end : end - start);
            extent.width = std::max(extent.width, measure(line, style.size) + extra);
            ++lines;
            if (end == std::string_view::npos)
                break;
            start = end + 1;
        }
        extent.height = lines * (style.size + style.line_gap) - style.line_gap + (style.shadow || style.outline ? 2 : 0);
        return extent;
    }
    // Draws a block with its top-left at (x,y); centred=true centres each line in the block
    // (DT_CENTER), otherwise lines are left-aligned.
    void draw_block(graphics::Image &image, std::string_view text, int x, int y, const TextStyle &style,
                    bool centred = false) {
        const int block_width = centred ? measure_block(text, style).width : 0;
        const int bold_extra = style.bold ? 1 : 0;
        std::size_t start = 0;
        for (int line_y = y;; line_y += style.size + style.line_gap) {
            const auto end = text.find('\n', start);
            const auto line = text.substr(start, end == std::string_view::npos ? end : end - start);
            int line_x = x;
            if (centred)
                line_x += (block_width - measure(line, style.size) - bold_extra -
                           (style.shadow ? 1 : 0) - (style.outline ? 2 : 0)) /
                          2;
            const auto stroke = [&](int dx, int dy, std::uint16_t colour) {
                draw(image, line, line_x + dx, line_y + dy, style.size, colour);
                if (style.bold)
                    draw(image, line, line_x + dx + 1, line_y + dy, style.size, colour);
            };
            if (style.outline) {
                for (const auto [dx, dy] : {std::pair{0, 1}, std::pair{2, 1}, std::pair{1, 0}, std::pair{1, 2}})
                    stroke(dx, dy, style.effect);
                stroke(1, 1, style.color);
            } else {
                if (style.shadow)
                    stroke(1, 1, style.effect);
                stroke(0, 0, style.color);
            }
            if (end == std::string_view::npos)
                break;
            start = end + 1;
        }
    }

  private:
    struct Glyph {
        int width, height, left, top, advance;
        std::vector<std::uint8_t> mask;
    };
    // monochrome yields aliased glyphs like the original unsmoothed GDI text: the smoothed coverage
    // is thresholded (FreeType's 1-bit rasterizer drops thin MingLiU strokes).
    const Glyph &glyph(char32_t character, int size, bool monochrome = false) {
        const auto key = std::tuple(size, character, monochrome);
        if (const auto found = glyphs_.find(key); found != glyphs_.end())
            return found->second;
        if (FT_Set_Pixel_Sizes(face_, 0, static_cast<FT_UInt>(size)) ||
            FT_Load_Char(face_, character, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL))
            throw std::runtime_error("Cannot rasterize font glyph");
        const auto &bitmap = face_->glyph->bitmap;
        Glyph result{int(bitmap.width),
                     int(bitmap.rows),
                     face_->glyph->bitmap_left,
                     face_->glyph->bitmap_top,
                     int(face_->glyph->advance.x >> 6),
                     {}};
        result.mask.resize(static_cast<std::size_t>(result.width * result.height));
        for (int row = 0; row < result.height; ++row) {
            const auto *line =
                bitmap.buffer + (bitmap.pitch >= 0 ? row : result.height - 1 - row) * std::abs(bitmap.pitch);
            for (int column = 0; column < result.width; ++column)
                result.mask[row * result.width + column] =
                    bitmap.pixel_mode == FT_PIXEL_MODE_MONO
                        ? static_cast<std::uint8_t>(((line[column / 8] >> (7 - column % 8)) & 1) * 255)
                    : monochrome ? static_cast<std::uint8_t>(line[column] >= 56 ? 255 : 0)
                                 : line[column];
        }
        return glyphs_.emplace(key, std::move(result)).first->second;
    }
    FT_Library library_{};
    FT_Face face_{};
    std::map<std::tuple<int, char32_t, bool>, Glyph> glyphs_;
};

// Exact grouped digits for edit fields and non-monetary share quantities.
inline std::string digits(std::int64_t amount) {
    std::string text = std::to_string(amount);
    const std::size_t begin = amount < 0 ? 1 : 0;
    for (std::ptrdiff_t position = static_cast<std::ptrdiff_t>(text.size()) - 3;
         position > static_cast<std::ptrdiff_t>(begin); position -= 3)
        text.insert(static_cast<std::size_t>(position), ",");
    return text;
}

// Display only: compact Chinese accounting units for the original 640px UI.
// Calculations and the banking keypad always use exact 64-bit values.
inline std::string compact_money(std::int64_t amount, bool dollar = true) {
    const auto magnitude = amount < 0 ? std::uint64_t(-(amount + 1)) + 1
                                      : std::uint64_t(amount);
    constexpr std::uint64_t wan = 10'000, yi = 100'000'000,
                            wan_yi = 1'000'000'000'000ULL, jing = 10'000'000'000'000'000ULL;
    std::uint64_t unit = 1;
    std::string_view suffix;
    if (magnitude >= jing) { unit = jing; suffix = "京"; }
    else if (magnitude >= wan_yi) { unit = wan_yi; suffix = "万亿"; }
    else if (magnitude >= yi) { unit = yi; suffix = "亿"; }
    else if (magnitude >= wan * 1000) { unit = wan; suffix = "万"; }
    if (unit == 1)
        return (dollar ? "$" : "") + digits(amount);
    const auto major = magnitude / unit;
    const auto minor = static_cast<unsigned>((magnitude % unit) * 100 / unit);
    std::string result = (dollar ? "$" : "") + std::string(amount < 0 ? "-" : "") +
                         std::to_string(major);
    if (minor) {
        result += ".";
        result += char('0' + minor / 10);
        if (minor % 10)
            result += char('0' + minor % 10);
    }
    return result + std::string(suffix);
}
inline std::string money(std::int64_t amount) {
    return compact_money(amount);
}

} // namespace rich4::ui
