// SPDX-License-Identifier: GPL-3.0-or-later
#include "finance.hpp"

#include "../native_stock_names.hpp"
#include "voices.hpp" // AUDIO: clerk voice ids

#include "rich4/calendar.hpp"
#include "rich4/mod_rules.hpp"
#include "rich4/stocks.hpp"
#include "rich4/world.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

namespace rich4::native::screens {
namespace {

// ref_0047e725: lit gauge width for trunc(value / max * 33).
constexpr std::array<std::uint8_t, 34> pad_gauge{3,  6,  9,  12, 15, 19, 22, 25, 28, 31, 34, 38,
                                                  41, 44, 47, 50, 53, 57, 60, 63, 66, 69, 73, 76,
                                                  79, 82, 85, 88, 92, 95, 98, 101, 104, 107};
// ref_0047e6d8: pad-local top-left of the pressed image for key ids 2..15.
struct PadKey {
    int id, x, y;
};
constexpr std::array<PadKey, 14> pad_keys{{{2, 8, 63},
                                           {3, 64, 63},
                                           {4, 8, 95},
                                           {5, 48, 95},
                                           {6, 88, 95},
                                           {7, 8, 119},
                                           {8, 48, 119},
                                           {9, 88, 119},
                                           {10, 8, 143},
                                           {11, 48, 143},
                                           {12, 88, 143},
                                           {13, 8, 167},
                                           {14, 48, 167},
                                           {15, 88, 167}}};
// Key id -> character ('M' MAX, 'E' Enter, 'C' clear, 'B' backspace).
constexpr char pad_character(int id) {
    constexpr const char *map = "??MEC0B789456123";
    return id >= 2 && id <= 15 ? map[id] : '?';
}
// Touch-friendly hit rectangles: the visible keys are 33x17, the enlarged areas fill the gaps.
graphics::Rect pad_key_rect(const PadKey &key) {
    if (key.id == 2)
        return {4, 59, 60, 91};
    if (key.id == 3)
        return {61, 59, 124, 91};
    return {key.x - 4, key.y - 3, key.x + 37, key.y + 20};
}

std::int64_t parse(const std::string &text) {
    std::int64_t value = 0;
    for (const char digit : text)
        if (digit >= '0' && digit <= '9')
            value = value > (INT64_MAX - (digit - '0')) / 10
                        ? INT64_MAX : value * 10 + (digit - '0');
    return value;
}

// Appends a digit like fcn_00452c02 / BK:4770: "0" is replaced, the length is bounded and
// the value is clamped to the maximum.
void append_digit(std::string &text, char digit, std::size_t limit, std::int64_t maximum) {
    if (text == "0")
        text.clear();
    if (text.size() < limit)
        text += digit;
    if (text.empty())
        text = "0";
    if (parse(text) > maximum)
        text = std::to_string(std::max(0, maximum));
}

std::optional<std::size_t> option_index(const GameDecision &decision, std::int32_t value) {
    for (std::size_t index = 0; index < decision.options.size(); ++index)
        if (decision.options[index].value == value)
            return index;
    return std::nullopt;
}
bool has_option(const GameDecision &decision, std::int32_t value) {
    return option_index(decision, value).has_value();
}

std::string with_number(const char *format, long long value) {
    char buffer[128];
    std::snprintf(buffer, sizeof buffer, format, value);
    return buffer;
}

} // namespace

int ids::pad_digit(int digit) {
    return digit == 0 ? 905 : digit >= 7 ? 900 + digit : digit >= 4 ? 906 + digit : 912 + digit;
}
int ids::atm_digit(int digit) {
    // ATM key order: 7 8 9 / 4 5 6 / 1 2 3 / C 0 <- (indices 4..15).
    return digit == 0 ? 934 : digit >= 7 ? 920 + digit - 3 : digit >= 4 ? 920 + digit + 3 : 920 + digit + 9;
}

// ---------------------------------------------------------------------------------------
// Number pad (Panel 21 artwork, Panel 22 hit map)

void NumberPad::open(std::int64_t maximum, Done done) {
    active_ = true;
    dragging_ = false;
    maximum_ = std::max(0, maximum);
    text_ = "0";
    done_ = std::move(done);
    // The original opens at (256,144). The doubled touch pad sits lower so a prompt box fits above it.
    x_ = scale_ > 1 ? 320 - 64 * scale_ : 256;
    y_ = scale_ > 1 ? 92 : 144;
}

std::int64_t NumberPad::value() const {
    return std::min(parse(text_), maximum_);
}

void NumberPad::press(int key) {
    const char character = pad_character(key);
    if (character == 'M')
        text_ = std::to_string(maximum_);
    else if (character == 'C')
        text_ = "0";
    else if (character == 'B') {
        text_.pop_back();
        if (text_.empty())
            text_ = "0";
    } else if (character >= '0' && character <= '9')
        append_digit(text_, character, 19, maximum_);
    else if (character == 'E') {
        const auto result = value();
        active_ = false;
        const auto done = std::move(done_);
        done_ = {};
        if (done)
            done(result);
    }
}

void NumberPad::set_from_gauge(int x) {
    const int position = std::clamp((x - x_) / scale_ - 10, 0, 107);
    const int step = std::clamp((position * 33 + 53) / 107, 0, 33);
    text_ = std::to_string(static_cast<std::int64_t>((__int128(maximum_) * step) / 33));
}

void NumberPad::cancel(Host &host) {
    if (!active_)
        return;
    if (host.sound)
        host.sound(cancel_sound);
    active_ = false;
    const auto done = std::move(done_);
    done_ = {};
    if (done)
        done(0); // The original cannot tell a cancel from an entered 0.
}

void NumberPad::key(Host &host, char key) {
    if (!active_)
        return;
    int id = 0;
    if (key >= '0' && key <= '9')
        id = ids::pad_digit(key - '0') - 900;
    else if (key == 'M' || key == 'm')
        id = 2;
    else if (key == '\r')
        id = 3;
    else if (key == 'C' || key == 'c')
        id = 4;
    else if (key == '\b')
        id = 6;
    else if (key == 'H' || key == 'h') {
        set_from_gauge(x_ + 64 * scale_);
        if (host.sound)
            host.sound(gauge_sound);
        return;
    }
    if (!id)
        return;
    if (host.sound)
        host.sound(key_sound);
    press(id);
}

int NumberPad::next_key_towards(std::int64_t target) const {
    const auto wanted = std::to_string(std::clamp(target, 0, maximum_));
    if (text_ == wanted)
        return ids::pad_enter;
    if (text_ == "0")
        return ids::pad_digit(wanted.front() - '0');
    if (wanted.starts_with(text_))
        return ids::pad_digit(wanted[text_.size()] - '0');
    return ids::pad_clear;
}

void NumberPad::draw(Host &host) {
    if (!active_)
        return;
    auto &frame = host.frame;
    const int k = scale_;
    const auto picture = [&](std::size_t index) -> const graphics::Image & {
        return scaled(frame.artwork, 'p', 21, index, k);
    };
    const auto &body = picture(0);
    if (dragging_) {
        if (frame.pointer_down)
            set_from_gauge(frame.pointer_x);
        else
            dragging_ = false;
    }
    frame.picture(body, x_, y_);
    for (std::size_t n = 0; n < std::min<std::size_t>(9, text_.size()); ++n) {
        const char digit = text_[text_.size() - 1 - n];
        frame.picture(picture(std::size_t(16 + digit - '0')), x_ + k * (107 - 12 * int(n)), y_ + k * 11);
    }
    // Original nine sprites only fit the trailing digits. Show a separate
    // complete, labelled edit value on wide transactions.
    if (text_.size() > 9) {
        const graphics::Rect display{x_, y_ - 28, x_ + 128 * k, y_ - 2};
        ui::fill(frame.canvas, display, ui::color(19, 35, 54));
        ui::border(frame.canvas, display, ui::color(212, 169, 78));
        frame.font.draw(frame.canvas, ui::digits(value()), x_ + 5, y_ - 23,
                        text_.size() >= 16 ? 13 : 16, ui::color(255, 233, 173));
    }
    frame.picture(picture(1), x_ + k * 10, y_ + k * 42);
    if (const auto current = value(); current > 0 && maximum_ > 0) {
        const auto step = std::clamp(int(double(current) / maximum_ * 33), 0, 33);
        blit_part(frame.canvas, body, {k * 10, k * 42, k * (10 + pad_gauge[std::size_t(step)]), k * 54},
                  x_ + k * 10, y_ + k * 42);
    }
    for (const auto &key : pad_keys) {
        const auto local = pad_key_rect(key);
        const graphics::Rect rect{x_ + k * local.left, y_ + k * local.top, x_ + k * local.right,
                                  y_ + k * local.bottom};
        if (frame.pointer_down && frame.pointer_in(rect))
            frame.picture(picture(std::size_t(key.id)), x_ + k * key.x, y_ + k * key.y);
        hit(host, 900 + key.id, rect, key_sound, [this, id = key.id] { press(id); });
    }
    hit(host, ids::pad_gauge, {x_ + k * 6, y_ + k * 34, x_ + k * 122, y_ + k * 58}, gauge_sound, [this, &frame] {
        dragging_ = true;
        set_from_gauge(frame.pointer_x);
    });
    hit(host, ids::pad_body, {x_, y_, x_ + k * 128, y_ + k * 192}, no_sound, [] {});
    // Touch has no right button: tapping outside the pad cancels, like the original right-click.
    hit(host, ids::pad_cancel, {0, 0, 640, 480}, cancel_sound, [this] {
        active_ = false;
        const auto done = std::move(done_);
        done_ = {};
        if (done)
            done(0);
    });
}

// ---------------------------------------------------------------------------------------
// Bank: ATM (Panel 24), loan desk and director's office (Panel 23)

namespace {
// ref_00475888, ATM-local, inclusive; the ATM body is drawn at (60,71).
constexpr std::array<graphics::Rect, 18> atm_rects{{{57, 49, 137, 90},
                                                    {139, 49, 219, 90},
                                                    {221, 49, 264, 90},
                                                    {53, 137, 268, 166},
                                                    {58, 211, 91, 228},
                                                    {97, 211, 130, 228},
                                                    {136, 211, 169, 228},
                                                    {58, 230, 91, 247},
                                                    {97, 230, 130, 247},
                                                    {136, 230, 169, 247},
                                                    {58, 249, 91, 266},
                                                    {97, 249, 130, 266},
                                                    {136, 249, 169, 266},
                                                    {58, 268, 91, 285},
                                                    {97, 268, 130, 285},
                                                    {136, 268, 169, 285},
                                                    {183, 233, 232, 258},
                                                    {175, 260, 232, 285}}};
constexpr int atm_x = 60, atm_y = 71;
// Screen rectangle (exclusive) for ATM control index, keys enlarged into their gaps for touch.
graphics::Rect atm_rect(std::size_t index) {
    const auto &r = atm_rects[index];
    graphics::Rect rect{r.left + atm_x, r.top + atm_y, r.right + atm_x + 1, r.bottom + atm_y + 1};
    if (index >= 4 && index <= 15)
        rect = {rect.left - 3, rect.top - 1, rect.right + 3, rect.bottom + 1};
    return rect;
}
// Index 4..15 -> character.
constexpr char atm_character(std::size_t index) {
    constexpr const char *map = "????789456123C0B";
    return index < 16 ? map[index] : '?';
}

bool owns_bank(const World &world, std::size_t slot) {
    const auto &player = world.players.at(slot).state;
    if (player.node_id >= world.nodes.size())
        return false;
    const auto code = world.nodes[player.node_id].type_and_index;
    return code > 6000 && code < 8000 && std::size_t(code - 6000) < world.commercials.size() &&
           world.commercials[code - 6000].owner_code == slot + 1;
}
std::int64_t others_deposits(const World &world, std::size_t slot) {
    std::int64_t total = 0;
    for (std::size_t i = 0; i < world.players.size(); ++i)
        if (i != slot)
            total += world.players[i].state.bank_balance;
    return total; // MOD: keep the full bank balance rather than clipping to INT32_MAX.
}
bool desk_available(const GameDecision &decision) {
    return has_option(decision, 3) || has_option(decision, 4) || has_option(decision, 5) || has_option(decision, 6);
}
} // namespace

void BankScreen::say(Host &host, std::vector<std::string> lines) {
    lines_ = std::move(lines);
    line_started_ = host.frame.ticks;
    ++said_; // AUDIO
}

std::int64_t BankScreen::atm_maximum(const World &world, std::size_t slot) const {
    const auto &player = world.players.at(slot).state;
    return std::max<std::int64_t>(0, mode_ == 0 ? player.bank_balance : player.cash);
}

void BankScreen::draw(Host &host, const World &world, const GameDecision &decision, NumberPad &pad,
                      const BankActions &actions) {
    const auto &status = world.players.at(decision.player).status;
    if (!started_) {
        started_ = true;
        mode_ = has_option(decision, 2) ? 0 : 1;
        atm_text_ = "0";
        if (status.days_bank_no_loans && !host.instant) {
            popup_ = with_number("銀行暫停放款\n\n還剩%lld天！", status.days_bank_no_loans & 0x7f);
            popup_until_ = host.frame.ticks + 1500;
        }
    }
    if (stage_ == Stage::atm)
        draw_atm(host, world, decision, pad, actions);
    else if (stage_ == Stage::desk)
        draw_desk(host, world, decision, pad, actions);
    else
        draw_director(host, world, decision, pad, actions);
    if (pad.active() && pad.scale() > 1 && !lines_.empty()) // the enlarged pad hides the balloon
        popup(host, lines_[std::min(std::size_t((host.frame.ticks - line_started_) / 2000), lines_.size() - 1)],
              pad.prompt_top());
    if (!popup_.empty()) {
        if (host.frame.ticks < popup_until_)
            popup(host, popup_, 196);
        else
            popup_.clear();
    }
    // AUDIO: the clerk / director voices each balloon line as it appears (#0075..#0091).
    if (!lines_.empty() && host.speech) {
        const auto index = std::min(std::size_t((host.frame.ticks - line_started_) / 2000), lines_.size() - 1);
        if (said_ != voiced_said_ || index != voiced_index_) {
            voiced_said_ = said_;
            voiced_index_ = index;
            if (const auto voice = voices::clerk_voice(lines_[index]))
                host.speech(*voice);
        }
    }
}

void BankScreen::leave_atm(Host &host, const GameDecision &decision, const BankActions &actions) {
    if (desk_available(decision)) {
        stage_ = Stage::desk;
        say(host, {"歡迎光臨\n大富翁銀行！", "需要我為您\n服務嗎？"});
    } else
        actions.respond(0, 0);
}

void BankScreen::cancel(Host &host, const GameDecision &decision, const BankActions &actions) {
    if (host.sound)
        host.sound(cancel_sound);
    if (stage_ == Stage::atm)
        leave_atm(host, decision, actions);
    else if (stage_ == Stage::director) {
        stage_ = Stage::desk;
        say(host, {"董事長慢走！"});
    } else
        actions.respond(0, 0);
}

void BankScreen::atm_key(Host &host, int index, const World &world, const GameDecision &decision,
                         const BankActions &actions) {
    const auto maximum = atm_maximum(world, decision.player);
    if (index == 0 || index == 1) {
        if (index == 0 && !has_option(decision, 2))
            return;
        mode_ = index;
        atm_text_ = "0";
    } else if (index == 2)
        leave_atm(host, decision, actions);
    else if (index == 16)
        atm_text_ = std::to_string(maximum);
    else if (index == 17) {
        const auto amount = std::min(parse(atm_text_), maximum);
        if (amount <= 0) {
            leave_atm(host, decision, actions);
            return;
        }
        if (actions.respond(mode_ == 0 ? 2 : 1, amount)) {
            atm_text_ = "0";
            leave_atm(host, decision, actions); // The ATM closes after one transaction.
        } else if (host.sound)
            host.sound(error_sound);
    } else if (const char character = atm_character(std::size_t(index)); character == 'C')
        atm_text_ = "0";
    else if (character == 'B') {
        atm_text_.pop_back();
        if (atm_text_.empty())
            atm_text_ = "0";
    } else if (character >= '0' && character <= '9')
        append_digit(atm_text_, character, 19, maximum);
}

void BankScreen::draw_atm(Host &host, const World &world, const GameDecision &decision, NumberPad &pad,
                          const BankActions &actions) {
    auto &frame = host.frame;
    const auto &status = world.players.at(decision.player).status;
    const auto maximum = atm_maximum(world, decision.player);
    if (parse(atm_text_) > maximum)
        atm_text_ = std::to_string(maximum);
    if (atm_dragging_) {
        if (frame.pointer_down) {
            const int x = frame.pointer_x - 118;
            const auto value = x < 0 ? 0
                               : x >= 204
                                   ? maximum
                                   : static_cast<std::int64_t>((__int128(maximum) * (x / 6 + 1)) / 34);
            atm_text_ = std::to_string(value);
        } else
            atm_dragging_ = false;
    }
    frame.picture('p', 24, 0, atm_x, atm_y);
    frame.picture('p', 24, std::size_t(1 + mode_), mode_ == 0 ? 117 : 199, 120);
    if (status.days_bank_no_loans)
        graphics::blit(frame.canvas, frame.artwork.frame('p', 24, 29), 157, 141);
    for (std::size_t k = 0; k < std::min<std::size_t>(10, atm_text_.size()); ++k) {
        const char digit = atm_text_[atm_text_.size() - 1 - k];
        frame.picture('p', 24, std::size_t(19 + digit - '0'), 304 - 20 * int(k), 172);
    }
    if (atm_text_.size() > 10) {
        const graphics::Rect display{111, 142, 332, 167};
        ui::fill(frame.canvas, display, ui::color(19, 35, 54));
        ui::border(frame.canvas, display, ui::color(212, 169, 78));
        frame.font.draw(frame.canvas, ui::digits(parse(atm_text_)), 114, 147,
                        atm_text_.size() >= 16 ? 13 : 15, ui::color(255, 233, 173));
    }
    if (maximum > 0) {
        const int lit = std::clamp(int(double(parse(atm_text_)) / maximum * 34), 0, 34) * 6;
        blit_part(frame.canvas, frame.artwork.frame('p', 24, 4), {0, 0, lit, 26}, 118, 210);
    }
    const auto &world_ref = world;
    if (pad.scale() > 1) {
        // Touch: the ATM's 33x17 keys are too small for a finger, so its display and keypad open
        // the enlarged number pad instead; Enter on the pad completes the ATM transaction.
        hit(host, ids::atm_keypad, {113, 165, 330, 362}, key_sound,
            [this, &pad, &world_ref, decision, actions, maximum, host_frame = &frame, sound = host.sound] {
                pad.open(maximum, [this, &world_ref, decision, actions, host_frame, sound](std::int64_t amount) {
                    atm_text_ = std::to_string(amount);
                    if (amount <= 0)
                        return; // cancelled or 0: stay at the ATM
                    Host callback{*host_frame, sound, {}};
                    atm_key(callback, 17, world_ref, decision, actions);
                });
            });
    }
    for (std::size_t index = 0; index < atm_rects.size(); ++index) {
        const auto rect = atm_rect(index);
        const auto &r = atm_rects[index];
        if (index >= 2 && index != 3 && frame.pointer_down && frame.pointer_in(rect))
            frame.picture('p', 24, index + 1, r.left + atm_x, r.top + atm_y);
        const int effect = index <= 1 ? click_sound : index == 2 ? exit_sound : index == 3 ? gauge_sound : key_sound;
        if (index == 3) {
            hit(host, ids::atm_gauge, rect, effect, [this] { atm_dragging_ = true; });
            continue;
        }
        hit(host, ids::atm_withdraw + int(index), rect, effect,
            [this, index, &world_ref, decision, actions, host_frame = &frame, sound = host.sound] {
                Host callback{*host_frame, sound, {}};
                atm_key(callback, int(index), world_ref, decision, actions);
            },
            index != 0 || has_option(decision, 2));
    }
}

void BankScreen::draw_side_panel(Host &host, const World &world, std::size_t slot) {
    auto &frame = host.frame;
    const auto elapsed = frame.ticks - side_started_;
    const int x = host.instant ? 440 : std::max(440, 640 - int(elapsed / 50) * 40);
    const auto &player = world.players.at(slot);
    frame.picture('p', 23, 15, x, 0);
    graphics::blit(frame.canvas, frame.artwork.frame('m', 27 + player.state.character, 0), x + 42, 40);
    const Style label{12, 0xffffff, 0x101010, outline};
    text(frame, "現  金", x + 10, 80, 0, label);
    text(frame, "存  款", x + 10, 145, 0, label);
    text(frame, "貸  款", x + 10, 208, 0, label);
    text(frame, names.at(player.state.character), x + 82, 28, 5, {22, 0xffffff, 0x101010, outline | bold});
    const Style money{18, 0x101010, 0, bold};
    text(frame, ui::money(player.state.cash), x + 180, 100, 6, money);
    text(frame, ui::money(player.state.bank_balance), x + 180, 164, 6, money);
    text(frame, ui::money(player.state.loan), x + 180, 228, 6, money);
    // Calendar (fcn_00433f24).
    const auto &date = world.header.date;
    constexpr std::array<int, 12> season{3, 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3};
    if (const auto *festival = festival_picture(frame.artwork, world)) // fcn_004521f0 row: Data 4..127
        graphics::blit(frame.canvas, *festival, x, 280, graphics::Transparency::opaque);
    else
        frame.picture('p', 2, std::size_t(season[std::clamp(int(date.month), 1, 12) - 1]), x, 280);
    const Style calendar_style{60, 0x101010, 0xffffff, outline | bold};
    text(frame, std::to_string(date.day), x + 60, 376, 2, calendar_style);
    try {
        constexpr std::array<const char *, 7> weekdays{"星期日", "星期一", "星期二", "星期三",
                                                       "星期四", "星期五", "星期六"};
        const calendar::Date today{date.year, date.month, date.day};
        text(frame, weekdays[calendar::weekday(today)], x + 14, 352, 3, {16, 0x101010, 0xffffff, outline | bold});
        if (player.state.loan > 0 && player.status.loan_due_date) {
            const auto due = calendar::unpack(player.status.loan_due_date);
            if (calendar::is_valid(due))
                text(frame, with_number("距還款日%lld天", calendar::days_between(today, due)), x + 20, 456, 5,
                     {20, 0x101010, 0xffffff, outline | bold});
        }
    } catch (const std::exception &) {
    }
    text(frame, std::to_string(date.year), x + 140, 288, 0, {24, 0x101010, 0xffffff, outline | bold});
    text(frame, with_number("%lld月", date.month), x + 60, 328, 2, {28, 0x101010, 0xffffff, outline | bold});
}

void BankScreen::draw_desk(Host &host, const World &world, const GameDecision &decision, NumberPad &pad,
                           const BankActions &actions) {
    auto &frame = host.frame;
    const auto slot = decision.player;
    const auto &player = world.players.at(slot);
    const bool director = owns_bank(world, slot);
    backdrop(frame, 'p', 23, 0);
    if (!director)
        graphics::blit(frame.canvas, frame.artwork.frame('p', 23, 1), 320, 240);
    else
        text(frame, "特別融資", 492, 245, 2, {14, 0x808080, 0, bold});
    // Current balloon line (each line about two seconds, fcn_0044ee18).
    std::string line;
    if (!lines_.empty()) {
        auto index = std::size_t((frame.ticks - line_started_) / 2000);
        if (pad.active()) // the request stays up while the amount is typed
            index = std::min(index, lines_.size() - 1);
        if (index < lines_.size())
            line = lines_[index];
        else {
            lines_.clear();
            if (!pad.active())
                side_panel_ = false;
        }
    }
    // Clerk: blink every few seconds (frames 7,6,7,5) and move the mouth while talking.
    const auto blink = (frame.ticks / 100) % 40;
    if (blink < 4) {
        constexpr std::array<std::size_t, 4> eyes{7, 6, 7, 5};
        frame.picture('p', 23, eyes[blink], 140, 114);
    }
    if (!line.empty())
        frame.picture('p', 23, (frame.ticks / 150) % 2 ? 9 : 10, 140, 152);
    const Style desk{26, 0x101010, 0, bold};
    text(frame, "申請貸款", 345, 345, 2, desk);
    text(frame, "償還貸款", 530, 345, 2, desk);
    if (player.status.days_bank_no_loans)
        graphics::blit(frame.canvas, frame.artwork.frame('p', 23, 23), 345, 345);
    if (frame.pointer_down && frame.pointer_in({548, 431, 629, 472}))
        frame.picture('p', 23, 19, 548, 431);
    if (!line.empty()) {
        frame.picture('p', 23, 21, 240, 80);
        text(frame, line, 357, 151, 4, {20, 0x101010, 0, bold});
    }
    if (side_panel_ || pad.active())
        draw_side_panel(host, world, slot);

    const auto wealth = stocks::player_wealth(world, slot);
    const auto loan = player.state.loan;
    const auto available = player.state.cash + std::int64_t(player.state.bank_balance);
    const auto sound = host.sound;
    auto *frame_pointer = &frame;
    hit(host, ids::desk_exit, {548, 431, 629, 472}, exit_sound, [actions] { actions.respond(0, 0); });
    hit(host, ids::desk_loan, {282, 324, 409, 367}, click_sound,
        [this, &pad, actions, wealth, loan, frame_pointer, sound] {
            Host callback{*frame_pointer, sound, {}};
            if (loan >= wealth) {
                say(callback, {"很抱歉！\n金額已超過\n許可額度。"});
                return;
            }
            say(callback, {"請輸入您要貸款\n的金額！"});
            side_panel_ = true;
            side_started_ = frame_pointer->ticks;
            pad.open(static_cast<std::int64_t>(std::clamp<__int128>(__int128(wealth) - loan, 0, INT32_MAX)), [this, actions, frame_pointer, sound](std::int64_t amount) {
                Host done{*frame_pointer, sound, {}};
                if (amount <= 0) {
                    lines_.clear();
                    side_panel_ = false;
                    return;
                }
                if (actions.respond(3, amount))
                    say(done, {"您的貸款手續\n已經完成。", "請於三個月內\n還清貸款。"});
                else
                    say(done, {"很抱歉！\n金額已超過\n許可額度。"});
            });
        },
        has_option(decision, 3));
    std::function<void()> repay;
    repay = [this, &pad, actions, loan, available, frame_pointer, sound] {
        Host callback{*frame_pointer, sound, {}};
        say(callback, {"請輸入您預備\n還款的金額！"});
        side_panel_ = true;
        if (!pad.active())
            side_started_ = frame_pointer->ticks;
        pad.open(std::max<std::int64_t>(0, loan), [this, &pad, actions, loan, available, frame_pointer, sound](std::int64_t amount) {
            Host done{*frame_pointer, sound, {}};
            if (amount <= 0) {
                lines_.clear();
                side_panel_ = false;
                return;
            }
            if (amount > available) {
                say(done, {"很抱歉！\n您的現金不足。"});
                // Re-prompt, as BK:2839 does.
                pad.open(std::max<std::int64_t>(0, loan), [this, actions, available, frame_pointer, sound](std::int64_t retry) {
                    Host again{*frame_pointer, sound, {}};
                    if (retry > 0 && retry <= available && actions.respond(4, retry))
                        say(again, {"您的還款手續\n已完成。"});
                    else {
                        lines_.clear();
                        side_panel_ = false;
                    }
                });
                return;
            }
            if (actions.respond(4, amount))
                say(done, {"您的還款手續\n已完成。"});
        });
    };
    hit(host, ids::desk_repay, {470, 326, 591, 367}, click_sound, repay, loan > 0 && has_option(decision, 4));
    if (director)
        hit(host, ids::desk_window, {268, 51, 592, 274}, click_sound, [this, frame_pointer, sound] {
            Host callback{*frame_pointer, sound, {}};
            stage_ = Stage::director;
            say(callback, {"董事長親自蒞臨\n不知有何指教？"});
        });
}

void BankScreen::draw_director(Host &host, const World &world, const GameDecision &decision, NumberPad &pad,
                               const BankActions &actions) {
    auto &frame = host.frame;
    const auto slot = decision.player;
    const auto &player = world.players.at(slot);
    const auto total = others_deposits(world, slot);
    const auto special = player.state.special_finance;
    backdrop(frame, 'p', 23, 2);
    frame.picture('p', 23, 20, 10, 125);
    const std::array<graphics::Rect, 3> rects{{{11, 305, 126, 346}, {11, 362, 126, 403}, {11, 419, 92, 460}}};
    for (std::size_t i = 0; i < rects.size(); ++i) {
        const bool lit = frame.pointer_in(rects[i]);
        frame.picture('p', 23, i < 2 ? (lit ? 17 : 16) : (lit ? 19 : 18), rects[i].left, rects[i].top);
    }
    const Style button{20, 0xf0f0f0, 0x101010, bold};
    text(frame, "週轉現金", 67, 324, 2, button);
    text(frame, "歸還款項", 67, 382, 2, button);
    if (player.status.days_bank_no_loans)
        graphics::blit(frame.canvas, frame.artwork.frame('p', 23, 23), 67, 324);
    const Style label{16, 0x202020, 0, bold};
    text(frame, "客戶存款總額", 78, 147, 2, label);
    text(frame, "目前融資金額", 78, 195, 2, label);
    text(frame, "尚可融資金額", 78, 243, 2, label);
    text(frame, financial(total), 128, 163, 6, label);
    text(frame, financial(special), 128, 211, 6, label);
    text(frame, financial(std::int64_t(total) - special), 128, 259, 6, label);
    if (!lines_.empty()) {
        auto index = std::size_t((frame.ticks - line_started_) / 2000);
        if (pad.active())
            index = std::min(index, lines_.size() - 1);
        if (index < lines_.size()) {
            frame.picture('p', 23, 22, 214, 50);
            text(frame, lines_[index], 339, 95, 4, {20, 0x101010, 0, bold});
        } else
            lines_.clear();
    }
    const auto available = player.state.cash + std::int64_t(player.state.bank_balance);
    const auto sound = host.sound;
    auto *frame_pointer = &frame;
    hit(host, ids::director_finance, rects[0], click_sound,
        [this, &pad, actions, total, special, frame_pointer, sound] {
            Host callback{*frame_pointer, sound, {}};
            if (special >= total) {
                say(callback, {"行裡現在沒有\n這麼多現金。"});
                return;
            }
            say(callback, {"請輸入您要\n週轉的金額～"});
            pad.open(static_cast<std::int64_t>(std::clamp<__int128>(__int128(total) - special, 0, INT32_MAX)), [this, actions](std::int64_t amount) {
                if (amount > 0 && actions.respond(5, amount)) {
                    stage_ = Stage::desk; // A transaction closes the office and the desk.
                    actions.respond(0, 0);
                } else
                    lines_.clear();
            });
        },
        !player.status.days_bank_no_loans && has_option(decision, 5));
    hit(host, ids::director_return, rects[1], click_sound,
        [this, &pad, actions, special, available, frame_pointer, sound] {
            Host callback{*frame_pointer, sound, {}};
            if (special == 0) {
                say(callback, {"董事長您別開玩笑了～"});
                return;
            }
            say(callback, {"請輸入您預備\n還款的金額～"});
            pad.open(std::min<std::int64_t>(std::max<std::int64_t>(special, 0), INT32_MAX)), [this, actions, available, frame_pointer, sound](std::int64_t amount) {
                Host done{*frame_pointer, sound, {}};
                if (amount <= 0) {
                    lines_.clear();
                    return;
                }
                if (amount > available) {
                    say(done, {"董事長您別開玩笑了～"});
                    return;
                }
                if (actions.respond(6, amount)) {
                    stage_ = Stage::desk;
                    actions.respond(0, 0);
                }
            });
        });
    hit(host, ids::director_exit, rects[2], exit_sound, [this, frame_pointer, sound] {
        Host callback{*frame_pointer, sound, {}};
        stage_ = Stage::desk;
        say(callback, {"董事長慢走！"});
    });
}

// ---------------------------------------------------------------------------------------

void draw_primary_stock(Host &host, const World &world, const GameDecision &decision, NumberPad &pad,
                        const BankActions &actions) {
    auto &frame = host.frame;
    std::string company;
    if (decision.land_id < world.commercials.size()) {
        const auto stock = world.commercials[decision.land_id].stock_index;
        const auto index = std::size_t(world.header.stage_id * 4 + world.header.map_id) * 12 + stock;
        if (stock < 12 && index < native_stock_names.size())
            company = std::string(native_stock_names[index]) + "\n";
    }
    const int top = pad.active() ? pad.prompt_top() : pad.scale() > 1 ? 2 : 42;
    frame.picture('p', 73, 5, 227, top);
    text(frame,
         "認購原始股\n" + company + "每股" + std::to_string(decision.amount) + "元\n最多" +
             std::to_string(decision.max_amount) + "股",
         319, top + 44, 4, black_text);
    if (!pad.active())
        pad.open(decision.max_amount, [actions](std::int64_t amount) {
            if (amount > 0)
                actions.respond(1, amount);
            else
                actions.respond(0, 0);
        });
}

} // namespace rich4::native::screens
