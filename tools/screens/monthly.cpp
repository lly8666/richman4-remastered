// SPDX-License-Identifier: GPL-3.0-or-later
// Layout, timing, texts and sounds from the original asm; see monthly.hpp and
// docs/ui/finance-menus.md §4 (dividend), §7.2 (lottery draw) and §14 (month-end report).
//
// The original screens restore back-buffer rectangles between states; here each frame is redrawn
// from the state's scene description (Playing::layout, lady, spotlight, ...), which shows the same
// picture.
#include "monthly.hpp"

#include "../native_stock_names.hpp"
#include "draw.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <utility>
#include <vector>
#include <string>
#include <string_view>

namespace rich4::native::monthly {
namespace {

using graphics::Transparency;
using screens::bold;
using screens::shadow;
using screens::Style;
using screens::text;

constexpr Style black16{16, 0x101010, 0, bold};
constexpr Style bubble_style{20, 0x101010, 0, bold}; // fcn_0044ecb6: font(20, colour, 0, 2, 1)

constexpr unsigned bubble_min_ms = 2000; // fcn_0044ee18: a bubble stays at least 2 s, and until its voice ends

std::string money(std::int64_t amount) {
    return screens::financial(amount);
}
bool listed(const MonthlyFigures &figures, std::size_t slot) {
    return slot < 4 && ((figures.active >> slot) & 1U);
}
std::vector<std::size_t> listed_slots(const MonthlyFigures &figures) {
    std::vector<std::size_t> slots;
    for (std::size_t slot = 0; slot < 4; ++slot)
        if (listed(figures, slot))
            slots.push_back(slot);
    return slots;
}
std::uint8_t character(const MonthlyFigures &figures, std::size_t slot) {
    return static_cast<std::uint8_t>(figures.characters.at(slot) % 12);
}
std::string_view name(const MonthlyFigures &figures, std::size_t slot) {
    return names.at(character(figures, slot));
}
// libc rand() stand-in for the cosmetic blinks (Watcom LCG, 15-bit result).
unsigned cosmetic_rand(std::uint32_t &seed) {
    seed = seed * 1103515245U + 12345U;
    return (seed >> 16) & 0x7fffU;
}
// Places an archive frame like fcn_00456418: top-left at (x, y) − origin.
void put(Frame &frame, char archive, std::size_t entry, std::size_t index, int x, int y,
         Transparency transparency = Transparency::source_key) {
    graphics::blit(frame.canvas, frame.artwork.frame(archive, entry, index), x, y, transparency);
}
std::unique_ptr<flc::Decoder> open_movie(Frame &frame, char archive, std::size_t entry) {
    try {
        auto bytes = frame.artwork.archive(archive).decode(entry);
        if (!flc::is_flc(bytes))
            return nullptr;
        return std::make_unique<flc::Decoder>(std::move(bytes));
    } catch (const std::exception &) {
        return nullptr;
    }
}

// ---- Lottery draw (rich4_ui_letou.asm) ----
enum LotteryLayout : int { hosts = 0, rolling, rolling_back, winner, sorry };
constexpr unsigned lottery_tick_ms = 50; // SetTimer 0x32 (LT:1335)
constexpr int cage_x = 183, cage_y = 75, winner_movie_x = 205, winner_movie_y = 0;
// fcn_0042f417 ticket panels, column-major (LT:105).
constexpr std::array<std::array<int, 2>, 4> ticket_origin{{{16, 340}, {16, 410}, {328, 340}, {328, 410}}};

// ---- Month-end report (rich4.asm fcn_00437e61) ----
constexpr unsigned report_tick_ms = 100;
// 0x475918: card column y, 0x475930: lineup x, 0x475948/0x475960: spotlight x / frame; [n-2][i].
constexpr std::array<std::array<int, 4>, 3> column_y{{{120, 360, 0, 0}, {100, 240, 380, 0}, {60, 180, 300, 420}}};
constexpr std::array<std::array<int, 4>, 3> lineup_x{{{407, 490, 0, 0}, {324, 407, 490, 0}, {324, 407, 490, 573}}};
constexpr std::array<std::array<int, 4>, 3> spot_x{{{334, 414, 0, 0}, {259, 334, 414, 0}, {259, 334, 414, 494}}};
constexpr std::array<std::array<int, 4>, 3> spot_frame{{{16, 17, 0, 0}, {15, 16, 17, 0}, {15, 16, 17, 18}}};
// 0x4759f7: {dx416, dy416, flags416, dx417, dy417, flags417} per character; flags bits 8..15 = plays.
constexpr std::array<std::array<int, 6>, 12> reaction{{{-50, -94, 0x603, -77, -148, 0x003},
                                                        {-48, -97, 0x403, -48, -90, 0x603},
                                                        {-38, -97, 0x603, -50, -87, 0x603},
                                                        {-31, -61, 0x603, -64, -109, 0x303},
                                                        {-41, -87, 0x403, -42, -77, 0x603},
                                                        {-44, -76, 0x403, -31, -60, 0x203},
                                                        {-36, -100, 0x603, -36, -103, 0x603},
                                                        {-50, -89, 0x403, -50, -87, 0x003},
                                                        {-51, -90, 0x403, -51, -92, 0x003},
                                                        {-31, -60, 0x403, -51, -95, 0x303},
                                                        {-103, -188, 0x403, -39, -87, 0x603},
                                                        {-40, -75, 0x403, -40, -75, 0x403}}};
struct LadyPose {
    int frame, x, y;
    std::array<int, 3> blink; // 0x4759e8 eye frames, -1 = no blink
    int eye_x, eye_y;
    int mouth_open, mouth_end, mouth_x, mouth_y; // -1 = no mouth animation
};
// c42d lady modes 0..6 (fcn_00437e61, rich4.asm:18259-18943).
constexpr std::array<LadyPose, 7> poses{{{19, 24, 70, {21, 20, 21}, 76, 120, 22, 23, 76, 160},
                                         {24, 28, 70, {28, 27, 28}, 103, 120, 30, 29, 103, 155},
                                         {32, 35, 67, {34, 33, 34}, 87, 118, 35, 36, 87, 161},
                                         {37, 0, 89, {-1, -1, -1}, 0, 0, -1, -1, 0, 0},
                                         {38, 17, 65, {39, 40, 39}, 83, 123, -1, -1, 0, 0},
                                         {42, 27, 64, {-1, -1, -1}, 0, 0, 44, 43, 90, 134},
                                         {45, 6, 60, {-1, -1, -1}, 0, 0, -1, -1, 0, 0}}};

} // namespace

bool handles(const EffectEvent &effect) {
    return (effect.kind == EffectKind::dividend || effect.kind == EffectKind::lottery_draw ||
            effect.kind == EffectKind::month_report) &&
           effect.monthly.has_value();
}

void MonthlyScreens::enqueue(EffectEvent effect) {
    if (!handles(effect))
        return;
    queue_.push_back(std::move(effect));
}

void MonthlyScreens::clear() {
    queue_.clear();
    current_.reset();
}

const char *MonthlyScreens::shot() const noexcept {
    if (!current_)
        return "";
    switch (current_->event.kind) {
    case EffectKind::dividend:
        return "dividend";
    case EffectKind::lottery_draw:
        return "lottery";
    default:
        return "report";
    }
}

void MonthlyScreens::say(Playing &playing, std::size_t entry, int x, int y, int dx, int dy, std::size_t voice,
                         std::string_view value) {
    auto &bubble = playing.bubble;
    bubble = {entry, x, y, dx, dy, std::string(value), bubble_min_ms, true};
    if (sound_.speech)
        bubble.remaining_ms = std::max<unsigned>(bubble_min_ms, sound_.speech(voice));
}

void MonthlyScreens::start(Playing &playing) {
    playing.figures = *playing.event.monthly;
    playing.seed = 0x2545f491U ^ playing.figures.number ^ (unsigned(playing.event.kind) << 8);
    switch (playing.event.kind) {
    case EffectKind::dividend:
        playing.tick_ms = 1000; // fcn_0042b3eb: SetTimer(1000 ms)
        if (sound_.play)
            sound_.play(61); // preset ref_004755a8, on open
        break;
    case EffectKind::lottery_draw:
        // Music 09 (fcn_004549cf(8)) is selected by the app's music controller while this screen is up.
        playing.tick_ms = lottery_tick_ms;
        playing.layout = hosts;
        playing.state = 1; // 0x405 with the dialogue option on
        say(playing, 22, 300, 47, -10, 0, 17, "嗨！\n又到了每月\n十五號樂透\n開獎時間～");
        break;
    default:
        // Music 10 (fcn_004549cf(9)) is selected by the app's music controller while this screen is up.
        playing.tick_ms = report_tick_ms;
        playing.state = 1; // 0x405 with the dialogue option on
        say(playing, 1, 190, 10, 0, -30, 92, "各位客戶辛苦了！\n又到了每月銀行\n結算的日子。");
        break;
    }
}

void MonthlyScreens::draw(Frame &frame, const World &world, unsigned elapsed_ms) {
    if (!current_) {
        if (queue_.empty())
            return;
        current_.emplace();
        current_->event = std::move(queue_.front());
        queue_.pop_front();
        start(*current_);
    }
    auto &playing = *current_;
    if (skip_ && playing.shown) { // headless replay: one frame per screen
        current_.reset();
        return;
    }
    bool open = true;
    switch (playing.event.kind) {
    case EffectKind::dividend:
        open = draw_dividend(frame, world, playing, elapsed_ms);
        break;
    case EffectKind::lottery_draw:
        open = draw_lottery(frame, playing, elapsed_ms);
        break;
    default:
        open = draw_report(frame, playing, elapsed_ms);
        break;
    }
    playing.shown = true;
    playing.tapped = false;
    frame.buttons.clear(); // the screen is modal: only its own tap target is live
    frame.hit(continue_button, {0, 0, 640, 480}, [this] {
        if (current_)
            current_->tapped = true;
    });
    if (!open)
        current_.reset();
}

// ---- §4 上市公司分紅 (fcn_0042ba97 / window proc fcn_0042b3eb, rich4_ui_stock.asm) ----
// Black screen, the table composed into Panel 76:0 drawn at (24,24) (frame-local text + 24).
// Effect 61 on open; closes on a left/right click or by itself on the third 1-second tick.
bool MonthlyScreens::draw_dividend(Frame &frame, const World &world, Playing &playing, unsigned elapsed_ms) {
    const auto &figures = playing.figures;
    playing.elapsed += elapsed_ms;
    while (playing.elapsed >= playing.tick_ms) {
        playing.elapsed -= playing.tick_ms;
        ++playing.counter;
    }
    ui::fill(frame.canvas, {0, 0, 640, 480}, ui::color(0, 0, 0));
    constexpr int ox = 24, oy = 24;
    put(frame, 'p', 76, 0, ox, oy, Transparency::opaque);
    text(frame, "上市公司分紅", ox + 296, oy + 25, 2, {28, 0xf0f0f0, 0x101010, shadow | bold});
    text(frame, "人名", ox + 104, oy + 82, 6, {12, 0x101010, 0, bold});
    text(frame, "公司", ox + 18, oy + 94, 5, {12, 0x101010, 0, bold});
    std::array<int, 4> column{};
    int next = 0;
    for (const auto slot : listed_slots(figures)) {
        column[slot] = next++;
        text(frame, name(figures, slot), ox + 160 + 98 * column[slot], oy + 88, 2, black16);
    }
    text(frame, "本月盈餘", ox + 542, oy + 88, 2, black16);
    text(frame, "紅  利", ox + 62, oy + 404, 2, black16);
    std::array<std::int64_t, 4> totals{};
    const auto map = std::size_t(world.header.stage_id * 4 + world.header.map_id);
    int y = 116;
    for (const auto &row : figures.dividends) {
        if (map * 12 + row.stock < native_stock_names.size())
            text(frame, native_stock_names[map * 12 + row.stock], ox + 62, oy + y, 2, black16);
        for (const auto slot : listed_slots(figures)) {
            text(frame, money(row.payout[slot]), ox + 198 + 98 * column[slot], oy + y, 6, black16);
            totals[slot] += row.payout[slot];
        }
        text(frame, money(row.earnings), ox + 572, oy + y, 6, black16);
        y += 24;
    }
    for (const auto slot : listed_slots(figures))
        text(frame, money(totals[slot]), ox + 198 + 98 * column[slot], oy + 404, 6, black16);
    return !playing.tapped && playing.counter < 3;
}

// ---- §7.2 樂透開獎 (_rich4_ui_letou_kaijiang_entry / fcn_0043010c, rich4_ui_letou.asm) ----
void MonthlyScreens::lottery_tick(Frame &frame, Playing &playing) {
    auto &bubble = playing.bubble;
    const auto &figures = playing.figures;
    // fcn_0044ee18: a state handler only runs on a tick with no bubble up.
    const bool idle = !bubble.up;
    if (idle) {
        switch (playing.state) {
        case 1:
            playing.state = 2;
            say(playing, 22, 300, 47, -10, 0, 18, "現在馬上為您\n開出這一期的\n號碼．．。");
            break;
        case 2: // LT:1482: the cage starts; Effect 57; the host works the cage
            playing.state = 3;
            playing.counter = 1;
            playing.layout = rolling;
            playing.eye_patch = playing.mouth_patch = -1;
            playing.movie = {};
            playing.movie.decoder = open_movie(frame, 'p', 16);
            playing.movie.x = cage_x;
            playing.movie.y = cage_y;
            playing.movie.overlay = false; // flags 8: decoded into the back buffer
            if (sound_.play)
                sound_.play(57);
            break;
        case 3: { // one cage frame per tick from tick 20; the result when it ends
            ++playing.counter;
            if (playing.counter == 20) {
                playing.layout = rolling_back;
                playing.eye_patch = playing.mouth_patch = -1;
            }
            if (playing.counter < 20)
                break;
            auto &cage = playing.movie;
            if (cage.decoder && cage.decoder->next()) {
                cage.still = cage.decoder->image();
                break;
            }
            playing.balls = playing.green = true; // LT:2087: balls 15:37+d, green Data 517:8+d
            const auto number = figures.number;
            if (number >= 1 && number <= 36 && figures.tickets[number - 1] && figures.winner >= 0) {
                playing.state = 4;
                say(playing, 22, 300, 47, -10, 0, 19, "本月份的得主\n是．．．。");
            } else {
                playing.state = 70; // LT:2266: blocking 500 ms wait before the SORRY host
                playing.counter = 0;
            }
            break;
        }
        case 70:
            if (++playing.counter * lottery_tick_ms < 500)
                break;
            playing.state = 7;
            playing.layout = sorry;
            playing.eye_patch = playing.mouth_patch = playing.left_patch = -1;
            say(playing, 23, 320, 200, 0, 0, 33, "SORRY！\n本月份沒有人\n得獎～");
            break;
        case 4: { // LT:1567: winner layout, name plate, Effect 58, the winner's name voice
            playing.state = 5;
            playing.counter = 1;
            playing.layout = winner;
            playing.plate = true;
            playing.eye_patch = playing.mouth_patch = playing.left_patch = -1;
            playing.second = {};
            playing.second.decoder = open_movie(frame, 'p', 17);
            playing.second.x = winner_movie_x;
            playing.second.y = winner_movie_y;
            if (sound_.play)
                sound_.play(58);
            if (sound_.speech && figures.winner >= 0)
                sound_.speech(20 + character(figures, std::size_t(figures.winner))); // #0020.. (0x475630)
            break;
        }
        case 5: { // static for 30 ticks, then one Panel 17 frame per tick
            ++playing.counter;
            if (playing.counter < 31)
                break;
            auto &movie = playing.second;
            if (movie.decoder && movie.decoder->next()) {
                movie.still = movie.decoder->image();
                break;
            }
            // Restores (150,0, 330×360): the movie, plate, name and green digits are gone.
            movie = {};
            playing.movie.still = {};
            playing.plate = playing.green = false;
            playing.state = 6;
            say(playing, 22, 300, 47, -10, 0, 32, "恭喜您獨得\n所有獎金！");
            break;
        }
        case 6:
            playing.layout = hosts;
            playing.state = 8;
            break;
        case 7:
            playing.state = 8;
            say(playing, 23, 320, 200, 0, 0, 34, "獎金將累積\n到下個月．\n．．．。");
            break;
        case 8:
            playing.layout = hosts;
            playing.eye_patch = playing.mouth_patch = playing.left_patch = -1;
            playing.state = 9;
            say(playing, 22, 300, 47, -10, 0, 35, "希望下次\n得獎者就\n是您！");
            break;
        case 9:
            playing.state = 10;
            say(playing, 22, 300, 47, -10, 0, 36, "行動要快喔！");
            break;
        case 10:
            playing.closing = true; // KillTimer, Post_0402: the show ends by itself
            break;
        default:
            break;
        }
    }
    // Host faces (LT:2516..): states 1-3 and 9-10 only, every tick, even under a bubble.
    if (!(playing.state <= 3 || playing.state == 9 || playing.state == 10))
        return;
    if (playing.blink >= 0) { // a blink: one frame per tick
        static constexpr std::array<int, 4> right{8, 7, 8, 10}, left{16, 17, 16, 15};
        if (playing.blink == 1)
            playing.eye_patch = right[std::size_t(playing.blink_step)];
        else
            playing.left_patch = left[std::size_t(playing.blink_step)];
        if (++playing.blink_step == 4)
            playing.blink = -1;
    } else {
        const unsigned r = cosmetic_rand(playing.seed) >> 9;
        if (r == 0 || r == 1) {
            playing.blink = r == 0 ? 1 : 2;
            playing.blink_step = 0;
        } else if (r <= 3)
            playing.eye_patch = 9 + int(cosmetic_rand(playing.seed) & 1U);
        else if (r <= 5)
            playing.left_patch = 14 + int(cosmetic_rand(playing.seed) & 1U);
    }
    if (bubble.up) { // LT:2770: the right host talks while a bubble is up
        if (playing.mouth_hold > 0) {
            if (--playing.mouth_hold == 0)
                playing.mouth_patch = 11;
        } else if ((cosmetic_rand(playing.seed) >> 11) < 4) {
            playing.mouth_patch = 12 + int(cosmetic_rand(playing.seed) & 1U);
            playing.mouth_hold = 1 + int(cosmetic_rand(playing.seed) % 15);
        }
    } else if (playing.mouth_patch >= 0)
        playing.mouth_patch = 11;
}

bool MonthlyScreens::draw_lottery(Frame &frame, Playing &playing, unsigned elapsed_ms) {
    const auto &figures = playing.figures;
    auto &bubble = playing.bubble;
    // Phone: a tap ends the current bubble early (the original has no input here and waits).
    if (playing.tapped && bubble.up)
        bubble.remaining_ms = 0;
    if (bubble.up)
        bubble.remaining_ms -= std::min(bubble.remaining_ms, elapsed_ms);
    playing.elapsed += elapsed_ms;
    while (playing.elapsed >= playing.tick_ms && !playing.closing) {
        playing.elapsed -= playing.tick_ms;
        if (bubble.up && bubble.remaining_ms == 0)
            bubble.up = false; // fcn_0044ee18 removes it; this tick's handler runs
        lottery_tick(frame, playing);
    }
    if (playing.closing)
        return false;

    auto &canvas = frame.canvas;
    put(frame, 'p', 15, 0, 0, 0, Transparency::opaque);
    if (!playing.movie.still.pixels.empty())
        graphics::blit(canvas, playing.movie.still, playing.movie.x, playing.movie.y,
                       playing.movie.overlay ? Transparency::source_key : Transparency::opaque);
    const int tens = figures.number / 10, units = figures.number % 10;
    if (playing.green) {
        put(frame, 'd', 517, std::size_t(8 + tens), 300, 220);
        put(frame, 'd', 517, std::size_t(8 + units), 340, 220);
    }
    const auto &background = frame.artwork.frame('p', 15, 0);
    switch (playing.layout) {
    case hosts:
        put(frame, 'p', 15, 1, 472, 66);
        put(frame, 'p', 15, 3, 7, 66);
        break;
    case rolling:
    case rolling_back:
        put(frame, 'p', 15, 2, 418, 66);
        put(frame, 'p', 15, 3, 7, 66);
        if (playing.layout == rolling_back) { // LT:1407 cnt 20: the host's arm goes back up
            screens::blit_part(canvas, background, {418, 171, 498, 251}, 418, 171);
            screens::blit_part(canvas, frame.artwork.frame('p', 15, 1), {0, 51, 38, 141}, 472, 117);
        }
        break;
    case winner:
        put(frame, 'p', 15, 6, 0, 0);
        put(frame, 'p', 15, 5, 505, 66);
        break;
    case sorry:
        put(frame, 'p', 15, 3, 7, 66);
        put(frame, 'p', 15, 21, 52, 89, Transparency::opaque);
        put(frame, 'p', 15, 4, 489, 116);
        break;
    default:
        break;
    }
    if (playing.layout == hosts || playing.layout == rolling || playing.layout == rolling_back) {
        if (playing.eye_patch >= 0)
            put(frame, 'p', 15, std::size_t(playing.eye_patch), 512, 102, Transparency::opaque);
        if (playing.mouth_patch >= 0)
            put(frame, 'p', 15, std::size_t(playing.mouth_patch), 512, 119, Transparency::opaque);
        if (playing.left_patch >= 0)
            put(frame, 'p', 15, std::size_t(playing.left_patch), 52, 89, Transparency::opaque);
    }
    // 累積獎金 board (font 20, 0x4f35b1 / red), moved up on the winner layout.
    const bool raised = playing.layout == winner;
    const Style board{20, 0x4f35b1, 0, bold}, amount{20, 0xff0000, 0, bold};
    text(frame, "累積獎金", raised ? 91 : 77, raised ? 19 : 193, 2, board);
    text(frame, "$" + money(figures.pool), raised ? 91 : 77, raised ? 56 : 228, 2, amount);
    if (playing.balls) {
        put(frame, 'p', 15, std::size_t(37 + tens), 286, 405);
        put(frame, 'p', 15, std::size_t(37 + units), 358, 405);
    }
    // fcn_0042f417: one darkened panel per listed player: head, then the numbers in Panel 13 digits.
    std::size_t panel = 0;
    for (const auto slot : listed_slots(figures)) {
        const int px = ticket_origin[panel][0], py = ticket_origin[panel][1];
        ++panel;
        screens::darken(canvas, {px, py, px + 296, py + 60}, -16);
        put(frame, 'p', 15, std::size_t(25 + character(figures, slot)), px + 20, py + 30);
        std::vector<int> numbers;
        for (std::size_t n = 0; n < 36; ++n)
            if (figures.tickets[n] == slot + 1)
                numbers.push_back(int(n + 1));
        for (std::size_t i = 0; i < numbers.size() && i < 12; ++i) {
            const int row_y = numbers.size() <= 6 ? py + 30 : i < 6 ? py + 15 : py + 45;
            const int x = px + 54 + 40 * int(i % 6);
            put(frame, 'p', 13, std::size_t(numbers[i] / 10), x, row_y);
            put(frame, 'p', 13, std::size_t(numbers[i] % 10), x + 16, row_y);
        }
    }
    if (playing.plate) { // 15:24 name plate; winner name font(28, red, 0x400000, 3, 4) at (320,180)
        put(frame, 'p', 15, 24, 320, 200);
        if (figures.winner >= 0)
            text(frame, name(figures, std::size_t(figures.winner)), 320, 180, 2,
                 {28, 0xff0000, 0x400000, shadow | bold});
    }
    if (!playing.second.still.pixels.empty())
        graphics::blit(canvas, playing.second.still, playing.second.x, playing.second.y);
    if (bubble.up) {
        const auto &image = frame.artwork.frame('p', 15, bubble.frame);
        graphics::blit(canvas, image, bubble.x, bubble.y);
        const int left = bubble.x - image.origin_x, top = bubble.y - image.origin_y;
        text(frame, bubble.text, left + image.width / 2 + bubble.dx, top + image.height / 2 + bubble.dy, 4,
             bubble_style);
    }
    return true;
}

// ---- §14 月底結算 (fcn_00439bfa / fcn_00437e61, rich4.asm) ----
void MonthlyScreens::report_tick(Frame &frame, Playing &playing) {
    auto &bubble = playing.bubble;
    const auto &figures = playing.figures;
    const auto slots = listed_slots(figures);
    const auto set_lady = [&](int mode) {
        playing.lady_mode = mode;
        playing.eye_patch = playing.mouth_patch = -1;
        playing.blink = -1;
        playing.mouth_hold = 0;
    };
    const auto index_of = [&](int slot) {
        const auto found = std::find(slots.begin(), slots.end(), std::size_t(slot));
        return found == slots.end() ? 0 : int(found - slots.begin());
    };
    const auto lineup = [&] { // fcn_00437c25: wallpaper, lady 19 and the row of figures
        set_lady(0);
        playing.lineup = true;
        playing.cards = false;
        playing.spotlight = playing.hidden = -1;
        playing.award = 0;
        playing.happy = false;
        playing.movie.still = {};
    };
    const auto start_reaction = [&](int slot, bool champion) {
        const auto c = character(figures, std::size_t(slot));
        const auto &row = reaction[c];
        const int n = std::clamp(int(slots.size()), 2, 4) - 2;
        playing.movie = {};
        playing.movie.decoder = open_movie(frame, 'd', std::size_t((champion ? 416 : 417) + 2 * c));
        playing.movie.x = lineup_x[std::size_t(n)][std::size_t(index_of(slot))] + row[champion ? 0 : 3];
        playing.movie.y = 330 + row[champion ? 1 : 4];
        playing.movie.plays = std::max(1, (row[champion ? 2 : 5] >> 8) & 0xff);
        playing.movie.running = playing.movie.decoder != nullptr;
        if (playing.movie.running && playing.movie.decoder->next())
            playing.movie.still = playing.movie.decoder->image();
    };
    if (!bubble.up && !playing.movie.running) {
        switch (playing.state) {
        case 1: // the lady opens her folder
            set_lady(1);
            playing.state = 2;
            say(playing, 1, 190, 10, 0, -30, 93, "大富翁銀行將\n根據您的存款，\n加發１０％的儲金利息。");
            break;
        case 2: // the cards: 存款 / 利息 (interest is credited here in the original)
            playing.happy = true;
            playing.cards = true;
            playing.skip_wait = false;
            playing.counter = 0;
            playing.lined = false;
            playing.state = figures.unlucky < 0 || figures.unlucky == figures.champion ? 0xF : 5;
            break;
        case 5:
        case 0xF: {
            ++playing.counter;
            if (playing.skip_wait)
                playing.counter = std::max(playing.counter, 30);
            if (playing.counter >= 20 && !playing.lined) { // c425 == 20: back to the lineup
                lineup();
                playing.lined = true;
            }
            if (playing.counter < 30)
                break;
            playing.skip_wait = false;
            if (playing.state == 5) {
                set_lady(2);
                playing.state = 6;
                say(playing, 1, 190, 10, 0, -30, 95, "本月悲情人物是．．。");
            } else {
                set_lady(4);
                playing.state = 0x10;
                say(playing, 1, 190, 10, 0, -30, 109, "本月冠軍是．．。");
            }
            if (sound_.play)
                sound_.play(27);
            break;
        }
        case 6: // spotlight on the 悲情人物, name voice #0096+c
            playing.spotlight = figures.unlucky;
            set_lady(3);
            playing.state = 7;
            say(playing, 1, 190, 10, 0, -30, 96 + character(figures, std::size_t(figures.unlucky)),
                name(figures, std::size_t(figures.unlucky)));
            if (sound_.play)
                sound_.play(60);
            break;
        case 7: // award panel, then the 417+2c "keep trying" reaction where the figure stood
            set_lady(2);
            playing.award = 1;
            playing.hidden = figures.unlucky;
            playing.state = 8;
            start_reaction(figures.unlucky, false);
            break;
        case 8:
            set_lady(4);
            playing.state = 9;
            break;
        case 9:
            playing.state = 0xF;
            playing.counter = 19;
            playing.lined = false;
            say(playing, 3, 190, 10, 20, 0, 108, "別灰心，再加油喔！");
            break;
        case 0x10: // spotlight on the champion, name voice #0110+c
            playing.spotlight = figures.champion;
            set_lady(5);
            playing.state = 0x11;
            say(playing, 1, 190, 10, 0, -30, 110 + character(figures, std::size_t(figures.champion)),
                name(figures, std::size_t(figures.champion)));
            if (sound_.play)
                sound_.play(28);
            break;
        case 0x11: // award panel (the lady is not redrawn), then the 416+2c celebration
            playing.lady_mode = -1;
            playing.award = 2;
            playing.hidden = figures.champion;
            playing.state = 0x12;
            start_reaction(figures.champion, true);
            break;
        case 0x12:
            set_lady(6);
            playing.state = 0x13;
            break;
        case 0x13:
            playing.state = 0x16;
            playing.counter = 10;
            say(playing, 4, 213, 37, 20, 0, 122, "其他人還要\n更努力喔！");
            break;
        case 0x16: // closes by itself after 1 s, or on a click
            if (--playing.counter <= 0 || playing.skip_wait)
                playing.closing = true;
            break;
        default:
            break;
        }
    }
    // The lady's blinks and mouth (rich4.asm:18259-18943), every tick.
    if (playing.lady_mode < 0)
        return;
    const auto &pose = poses[std::size_t(playing.lady_mode)];
    if (playing.blink >= 0) {
        if (playing.blink_step < 3)
            playing.eye_patch = pose.blink[std::size_t(playing.blink_step++)];
        else {
            playing.eye_patch = playing.lady_mode == 1 ? 25 : -1;
            playing.blink = -1;
        }
    } else {
        const unsigned r = cosmetic_rand(playing.seed) >> 10;
        if (r == 0 && pose.blink[0] >= 0) {
            playing.blink = 1;
            playing.blink_step = 0;
        } else if (r == 1 && playing.lady_mode == 1)
            playing.eye_patch = 25 + int(cosmetic_rand(playing.seed) & 1U);
    }
    if (pose.mouth_open < 0)
        return;
    if (playing.mouth_hold > 0) {
        if (--playing.mouth_hold == 0)
            playing.mouth_patch = pose.mouth_end;
    } else if (bubble.up && (cosmetic_rand(playing.seed) & 3U) == 0) {
        playing.mouth_patch = playing.lady_mode == 0 && (cosmetic_rand(playing.seed) & 1U) ? -1 : pose.mouth_open;
        playing.mouth_hold = 1 + int(cosmetic_rand(playing.seed) & 7U);
    }
}

bool MonthlyScreens::draw_report(Frame &frame, Playing &playing, unsigned elapsed_ms) {
    const auto &figures = playing.figures;
    auto &bubble = playing.bubble;
    auto &movie = playing.movie;
    // 0x202/0x205: a click removes the bubble at once, skips a playing reaction and the next wait.
    if (playing.tapped) {
        if (bubble.up)
            bubble.up = false;
        if (movie.running) {
            movie.running = false;
            movie.decoder.reset();
        }
        playing.skip_wait = true;
    }
    if (bubble.up)
        bubble.remaining_ms -= std::min(bubble.remaining_ms, elapsed_ms);
    if (movie.running) { // the blocking fcn_0045144f playback
        movie.elapsed += elapsed_ms;
        const unsigned delay = std::max(20U, movie.decoder->delay_ms());
        while (movie.running && movie.elapsed >= delay) {
            movie.elapsed -= delay;
            if (movie.decoder->next())
                movie.still = movie.decoder->image();
            else if (--movie.plays > 0) {
                movie.decoder->rewind();
                if (movie.decoder->next())
                    movie.still = movie.decoder->image();
            } else
                movie.running = false;
        }
    }
    playing.elapsed += elapsed_ms;
    while (playing.elapsed >= playing.tick_ms && !playing.closing) {
        playing.elapsed -= playing.tick_ms;
        if (bubble.up && bubble.remaining_ms == 0)
            bubble.up = false;
        report_tick(frame, playing);
    }
    if (playing.closing)
        return false;

    auto &canvas = frame.canvas;
    const auto slots = listed_slots(figures);
    const int n = std::clamp(int(slots.size()), 2, 4) - 2;
    put(frame, 'p', 25, 0, 0, 0, Transparency::opaque);
    for (std::size_t i = 0; i < slots.size(); ++i)
        if (int(slots[i]) == playing.spotlight)
            put(frame, 'p', 25, std::size_t(spot_frame[std::size_t(n)][i]), spot_x[std::size_t(n)][i], 0,
                Transparency::opaque);
    if (playing.lady_mode >= 0) {
        const auto &pose = poses[std::size_t(playing.lady_mode)];
        put(frame, 'p', 25, std::size_t(pose.frame), pose.x, pose.y);
        if (playing.eye_patch >= 0)
            put(frame, 'p', 25, std::size_t(playing.eye_patch), pose.eye_x, pose.eye_y);
        if (playing.mouth_patch >= 0)
            put(frame, 'p', 25, std::size_t(playing.mouth_patch), pose.mouth_x, pose.mouth_y);
    }
    const Style card_style{18, 0x101010, 0, bold}, loan_style{18, 0xff0000, 0, bold};
    for (std::size_t i = 0; i < slots.size(); ++i) {
        const auto slot = slots[i];
        const auto figure = std::size_t(3 * character(figures, slot) + (playing.happy && !playing.lineup ? 49 : 47));
        if (playing.lineup) {
            if (int(slot) == playing.hidden)
                continue;
            const auto &image = frame.artwork.frame('p', 25, figure);
            graphics::blit(canvas, image, lineup_x[std::size_t(n)][i], 330 - image.height + image.origin_y);
            continue;
        }
        const int y = column_y[std::size_t(n)][i];
        put(frame, 'p', 25, figure, 600, y);
        if (!playing.cards)
            continue;
        const int cx = 360, cy = y - 36; // card frame 11+i with its pre-rendered text (font 18 bold)
        put(frame, 'p', 25, 11 + i, cx, cy);
        text(frame, "存款：", cx + 4, cy + 6, 0, card_style);
        text(frame, money(figures.deposit[slot]), cx + 154, cy + 6, 1, card_style);
        text(frame, "利息：", cx + 4, cy + 46, 0, card_style);
        if ((figures.loans >> slot) & 1U)
            text(frame, "貸款中", cx + 154, cy + 46, 1, loan_style);
        else
            text(frame, money(figures.interest[slot]), cx + 154, cy + 46, 1, card_style);
    }
    if (playing.award) { // frame 2 at (440,405); labels x=320, values right-aligned at x=560
        put(frame, 'p', 25, 2, 440, 405, Transparency::opaque);
        const Style award{16, 0x101010, 0, bold};
        const auto slot = std::size_t(playing.award == 1 ? figures.unlucky : figures.champion);
        text(frame, "獲獎原因：", 320, 370, 0, award);
        if (playing.award == 1) {
            text(frame, "本月意外損失：", 320, 388, 0, award);
            text(frame, money(std::int64_t(std::int32_t(figures.spending[slot]))), 560, 388, 1, award);
            text(frame, "本月意外之財：", 320, 406, 0, award);
            text(frame, money(std::int64_t(std::int32_t(figures.income[slot]))), 560, 406, 1, award);
            text(frame, "本月倒楣天數：", 320, 424, 0, award);
            text(frame, std::to_string(figures.unlucky_days[slot]) + "天", 560, 424, 1, award);
        } else {
            text(frame, "現金：", 320, 388, 0, award);
            text(frame, money(figures.cash[slot]), 560, 388, 1, award);
            text(frame, "存款：", 320, 406, 0, award);
            text(frame, money(figures.balance[slot]), 560, 406, 1, award);
            text(frame, "總資產：", 320, 424, 0, award);
            text(frame, money(figures.total[slot]), 560, 424, 1, award);
        }
    }
    if (!movie.still.pixels.empty())
        graphics::blit(canvas, movie.still, movie.x, movie.y);
    if (bubble.up) {
        const auto &image = frame.artwork.frame('p', 25, bubble.frame);
        graphics::blit(canvas, image, bubble.x, bubble.y);
        const int left = bubble.x - image.origin_x, top = bubble.y - image.origin_y;
        text(frame, bubble.text, left + image.width / 2 + bubble.dx, top + image.height / 2 + bubble.dy, 4,
             bubble_style);
    }
    return true;
}

void MonthlyScreens::gallery(Frame &frame, const World &world, const std::function<void()> &background,
                             const std::function<void(const std::string &)> &write) {
    MonthlyFigures base;
    for (std::size_t slot = 0; slot < world.players.size() && slot < 4; ++slot) {
        if (world.players[slot].state.controller_flags)
            base.active |= static_cast<std::uint8_t>(1U << slot);
        base.characters[slot] = world.players[slot].state.character;
    }
    std::vector<std::pair<std::string, EffectEvent>> samples;
    const auto sample = [&](const std::string &name, EffectKind kind, MonthlyFigures figures) {
        EffectEvent event{kind};
        event.monthly = std::move(figures);
        samples.emplace_back(name, std::move(event));
    };
    auto dividend = base;
    for (std::uint8_t stock = 0; stock < 9; ++stock) {
        DividendRow row{stock, 12000 - 2500 * stock, {}};
        row.payout = {row.earnings / 2, row.earnings / 4, 0, row.earnings / 4};
        dividend.dividends.push_back(row);
    }
    sample("dividend", EffectKind::dividend, dividend);
    auto won = base;
    for (std::size_t n = 0; n < 36; n += 3)
        won.tickets[n] = static_cast<std::uint8_t>(1 + (n / 3) % 4);
    won.pool = 27000;
    won.number = 7;
    won.winner = won.tickets[6] - 1;
    sample("lottery-won", EffectKind::lottery_draw, won);
    auto lost = won;
    lost.number = 29;
    lost.winner = -1;
    sample("lottery-lost", EffectKind::lottery_draw, lost);
    auto report = base;
    for (std::size_t slot = 0; slot < 4; ++slot) {
        report.cash[slot] = 52000 + 9000 * std::int32_t(slot);
        report.deposit[slot] = 10000 + 15000 * std::int32_t(slot);
        report.interest[slot] = report.deposit[slot] / 10;
        report.balance[slot] = report.deposit[slot] + report.interest[slot];
        report.total[slot] = report.cash[slot] + report.balance[slot] + 40000;
        report.spending[slot] = 1500 * std::uint32_t(slot);
        report.income[slot] = 800;
    }
    report.loans = 0x4;
    report.balance[2] = report.deposit[2];
    report.unlucky_days[1] = 4;
    report.spending[1] = 36000;
    report.unlucky = 1;
    report.champion = 3;
    sample("report", EffectKind::month_report, report);

    const bool skip = skip_;
    skip_ = false;
    for (auto &[name, event] : samples) {
        clear();
        enqueue(event);
        unsigned shot = 0;
        for (unsigned ms = 0; active() && ms < 120000; ms += 50) {
            background();
            draw(frame, world, ms ? 50 : 0);
            if (ms % 500 == 0) {
                char suffix[16];
                std::snprintf(suffix, sizeof suffix, "-%03u", shot++);
                write("monthly-" + name + suffix);
            }
        }
    }
    clear();
    skip_ = skip;
}

} // namespace rich4::native::monthly
