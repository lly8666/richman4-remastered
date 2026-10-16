// SPDX-License-Identifier: GPL-3.0-or-later
#include "draw.hpp"

#include "rich4/holidays.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <tuple>
#include <vector>

namespace rich4::native::screens {
namespace {

std::vector<std::string_view> lines(std::string_view text) {
    std::vector<std::string_view> result;
    for (std::size_t start = 0;;) {
        const auto end = text.find('\n', start);
        result.push_back(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (end == std::string_view::npos)
            return result;
        start = end + 1;
    }
}

void draw_run(Frame &frame, std::string_view value, int x, int top, const Style &style) {
    const auto main = rgb(style.colour), second = rgb(style.colour2);
    // GDI puts the baseline about 0.8 em below the cell top; ui::Font puts it a full em below.
    const int y = top - style.size / 5;
    const auto put = [&](int dx, int dy, std::uint16_t colour) {
        frame.font.draw(frame.canvas, value, x + dx, y + dy, style.size, colour);
        if (style.flags & bold)
            frame.font.draw(frame.canvas, value, x + dx + 1, y + dy, style.size, colour);
    };
    if (style.flags & outline) {
        put(-1, 0, second);
        put(1, 0, second);
        put(0, -1, second);
        put(0, 1, second);
    } else if (style.flags & shadow)
        put(1, 1, second);
    put(0, 0, main);
}

// Top of a GDI text cell (height = size) vertically centred on centre_y.
int top_for_centre(int centre_y, int size) {
    return centre_y - size / 2;
}

} // namespace

int measure(Frame &frame, std::string_view text, const Style &style) {
    int width = 0;
    for (const auto line : lines(text))
        width = std::max(width, frame.font.measure(line, style.size) + ((style.flags & bold) ? 1 : 0));
    return width;
}

void text(Frame &frame, std::string_view value, int x, int y, int align, const Style &style) {
    const int line_height = style.size + 3;
    const auto rows = lines(value);
    switch (align) {
    case 3: { // vertical text centred at (x, y)
        const auto glyphs = ui::codepoints(value);
        int top = y - int(glyphs.size()) * style.size / 2;
        for (std::size_t offset = 0; offset < value.size();) {
            std::size_t length = 1;
            const auto first = static_cast<unsigned char>(value[offset]);
            if (first >= 0xf0)
                length = 4;
            else if (first >= 0xe0)
                length = 3;
            else if (first >= 0xc0)
                length = 2;
            const auto glyph = value.substr(offset, length);
            draw_run(frame, glyph, x - measure(frame, glyph, style) / 2, top, style);
            top += style.size;
            offset += length;
        }
        return;
    }
    case 4:
    case 7: {
        const int height = int(rows.size()) * line_height - 3;
        int top = align == 4 ? y - height / 2 : y - height;
        for (const auto line : rows) {
            draw_run(frame, line, x - measure(frame, line, style) / 2, top, style);
            top += line_height;
        }
        return;
    }
    default:
        break;
    }
    int top = y;
    if (align == 2 || align == 5 || align == 6)
        top = top_for_centre(y, style.size) - (int(rows.size()) - 1) * line_height / 2;
    for (const auto line : rows) {
        const int width = measure(frame, line, style);
        const int left = align == 1 || align == 6 ? x - width : align == 2 ? x - width / 2 : x;
        draw_run(frame, line, left, top, style);
        top += line_height;
    }
}

void darken(graphics::Image &image, graphics::Rect rect, int level) {
    rect.left = std::clamp(rect.left, 0, image.width);
    rect.right = std::clamp(rect.right, 0, image.width);
    rect.top = std::clamp(rect.top, 0, image.height);
    rect.bottom = std::clamp(rect.bottom, 0, image.height);
    const unsigned factor = unsigned(std::clamp(32 + level, 0, 32));
    for (int y = rect.top; y < rect.bottom; ++y)
        for (int x = rect.left; x < rect.right; ++x) {
            auto &pixel = image.pixels[std::size_t(y * image.width + x)];
            unsigned result = 0;
            for (unsigned shift : {0U, 5U, 10U})
                result |= ((((pixel >> shift) & 31U) * factor) / 32U) << shift;
            pixel = static_cast<std::uint16_t>(result);
        }
}

void greyscale(graphics::Image &image, graphics::Rect rect) {
    rect.left = std::clamp(rect.left, 0, image.width);
    rect.right = std::clamp(rect.right, 0, image.width);
    rect.top = std::clamp(rect.top, 0, image.height);
    rect.bottom = std::clamp(rect.bottom, 0, image.height);
    for (int y = rect.top; y < rect.bottom; ++y)
        for (int x = rect.left; x < rect.right; ++x) {
            auto &pixel = image.pixels[std::size_t(y * image.width + x)];
            const unsigned r = (pixel >> 10) & 31U, g = (pixel >> 5) & 31U, b = pixel & 31U;
            const unsigned grey = (r * 30 + g * 59 + b * 11) / 100;
            pixel = static_cast<std::uint16_t>(grey << 10 | grey << 5 | grey);
        }
}

graphics::Image raw_picture(Artwork &artwork, char archive, std::size_t entry, int width, int height) {
    const auto bytes = artwork.archive(archive).decode(entry);
    auto image = graphics::make_canvas(width, height, 0);
    const auto count = std::min<std::size_t>(image.pixels.size(), bytes.size() / 2);
    for (std::size_t i = 0; i < count; ++i)
        image.pixels[i] = static_cast<std::uint16_t>((bytes[2 * i] | bytes[2 * i + 1] << 8) & 0x7fff);
    return image;
}

const graphics::Image *festival_picture(Artwork &artwork, const World &world) {
    const auto &date = world.header.date;
    std::optional<std::size_t> entry;
    try {
        entry = holidays::festival_picture({date.year, date.month, date.day}, world.header.map_id,
                                           world.header.stage_id);
    } catch (const std::exception &) { // invalid date or map: keep the season picture
        return nullptr;
    }
    if (!entry)
        return nullptr;
    static std::map<std::pair<const Artwork *, std::size_t>, graphics::Image> cache;
    auto &image = cache[{&artwork, *entry}];
    if (image.pixels.empty())
        image = raw_picture(artwork, 'd', *entry, 200, 200);
    return &image;
}

void blit_part(graphics::Image &canvas, const graphics::Image &image, graphics::Rect source, int x, int y) {
    graphics::blit(canvas, image, x - source.left + image.origin_x, y - source.top + image.origin_y,
                   {x, y, x + (source.right - source.left), y + (source.bottom - source.top)});
}

void outline_rect(graphics::Image &canvas, graphics::Rect rect, std::uint16_t colour) {
    ui::border(canvas, rect, colour);
}

void pressed_bevel(graphics::Image &canvas, graphics::Rect rect) {
    rect.left = std::clamp(rect.left, 0, canvas.width);
    rect.right = std::clamp(rect.right, 0, canvas.width);
    rect.top = std::clamp(rect.top, 0, canvas.height);
    rect.bottom = std::clamp(rect.bottom, 0, canvas.height);
    if (rect.right - rect.left < 2 || rect.bottom - rect.top < 2)
        return;
    for (int y = rect.bottom - 1; y > rect.top; --y)
        for (int x = rect.right - 1; x > rect.left; --x)
            canvas.pixels[std::size_t(y * canvas.width + x)] = canvas.pixels[std::size_t((y - 1) * canvas.width + x - 1)];
    darken(canvas, {rect.left, rect.top, rect.right, rect.top + 1}, -16);
    darken(canvas, {rect.left, rect.top + 1, rect.left + 1, rect.bottom}, -16);
}

const graphics::Image &scaled(Artwork &artwork, char archive, std::size_t entry, std::size_t index, int scale) {
    const auto &source = artwork.frame(archive, entry, index);
    if (scale <= 1)
        return source;
    static std::map<std::tuple<const graphics::Image *, int>, graphics::Image> cache;
    auto &result = cache[{&source, scale}];
    if (result.pixels.empty()) {
        result.width = source.width * scale;
        result.height = source.height * scale;
        result.origin_x = source.origin_x * scale;
        result.origin_y = source.origin_y * scale;
        result.pixels.resize(std::size_t(result.width * result.height));
        if (!source.opaque_pixels.empty())
            result.opaque_pixels.resize(result.pixels.size());
        for (int y = 0; y < result.height; ++y)
            for (int x = 0; x < result.width; ++x) {
                const auto from = std::size_t((y / scale) * source.width + x / scale);
                const auto to = std::size_t(y * result.width + x);
                result.pixels[to] = source.pixels[from];
                if (!source.opaque_pixels.empty())
                    result.opaque_pixels[to] = source.opaque_pixels[from];
            }
    }
    return result;
}

void backdrop(Frame &frame, char archive, std::size_t entry, std::size_t index) {
    const auto &image = frame.artwork.frame(archive, entry, index);
    graphics::blit(frame.canvas, image, image.origin_x, image.origin_y, graphics::Transparency::opaque);
}

void popup(Host &host, std::string_view message, int y) {
    auto &frame = host.frame;
    frame.picture('p', 73, 5, 228, y);
    text(frame, message, 320, y + 44, 4, black_text);
}

std::string grouped(std::int64_t amount) {
    return ui::digits(amount);
}

void hit(Host &host, int id, graphics::Rect rect, int effect, std::function<void()> action, bool enabled) {
    host.frame.hit(
        id, rect,
        [sound = host.sound, effect, action = std::move(action)] {
            if (effect >= 0 && sound)
                sound(effect);
            action();
        },
        enabled);
    host.frame.buttons.back().click_sound = false;
}

} // namespace rich4::native::screens
