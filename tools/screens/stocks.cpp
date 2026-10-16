// SPDX-License-Identifier: GPL-3.0-or-later
// Panel 75 stock market (docs/ui/finance-menus.md §4, rich4_ui_stock.asm).
#include "finance.hpp"

#include "../native_stock_names.hpp"
#include "rich4/holidays.hpp"
#include "rich4/stocks.hpp"
#include "rich4/world.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

namespace rich4::native::screens {
namespace {

float price(const World &world, std::size_t stock) {
    return std::bit_cast<float>(world.stocks[stock].float_bits[2]);
}
float previous_price(const World &world, std::size_t stock) {
    return std::bit_cast<float>(world.stocks[stock].float_bits[1]);
}
// ST:154-175: precision follows the quote's magnitude.
std::string format_price(float value, float magnitude, bool sign = false) {
    char buffer[32];
    const int precision = magnitude < 15 ? 2 : magnitude < 150 ? 1 : 0;
    std::snprintf(buffer, sizeof buffer, sign ? "%+.*f" : "%.*f", precision, double(value));
    return buffer;
}
std::string_view stock_name(const World &world, std::size_t stock) {
    return native_stock_names.at(std::size_t(world.header.stage_id * 4 + world.header.map_id) * 12 + stock);
}
const Commercial *company(const World &world, std::size_t stock) {
    for (const auto &commercial : world.commercials)
        if (commercial.id && commercial.stock_index == stock)
            return &commercial;
    return nullptr;
}
// Price history in chronological order (ST:1450: ring walk from the cursor, stops at a zero).
std::vector<float> history(const World &world, std::size_t stock) {
    const auto &ring = world.stock_history_bits.at(stock);
    const auto cursor = std::size_t(std::clamp(world.stock_history_cursor, 0, 143));
    const std::size_t start = ring[cursor] ? cursor : 0;
    std::vector<float> samples;
    for (std::size_t k = 0; k < ring.size(); ++k) {
        const auto bits = ring[(start + k) % ring.size()];
        if (!bits)
            break;
        samples.push_back(std::bit_cast<float>(bits));
    }
    return samples;
}
void line(graphics::Image &canvas, int x0, int y0, int x1, int y1, std::uint16_t colour) {
    const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        if (x0 >= 0 && x0 < canvas.width && y0 >= 0 && y0 < canvas.height)
            canvas.pixels[std::size_t(y0 * canvas.width + x0)] = colour;
        if (x0 == x1 && y0 == y1)
            return;
        const int twice = 2 * error;
        if (twice >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

constexpr graphics::Rect page_button{16, 9, 125, 40}, buy_button{128, 9, 199, 40}, sell_button{202, 9, 273, 40},
    info_button{276, 9, 411, 40}, exit_button{552, 8, 624, 41};
// The top bar is 31 px tall; its hit areas extend to the screen edge and the header row for fingers.
graphics::Rect tall(graphics::Rect rect) {
    return {rect.left, 0, rect.right, 47};
}

} // namespace

bool market_closed(const World &world) {
    return world.news_effect_days != 0 ||
           holidays::is_closed({world.header.date.year, world.header.date.month, world.header.date.day},
                               world.header.map_id, world.header.stage_id);
}

void StockMarket::cancel(Host &host, const StockActions &actions) {
    if (host.sound)
        host.sound(cancel_sound);
    if (info_open)
        info_open = false;
    else if (page == 1)
        page = 0;
    else
        actions.close();
}

void StockMarket::draw(Host &host, const World &world, std::size_t player, NumberPad &pad,
                       const StockActions &actions) {
    auto &frame = host.frame;
    ui::fill(frame.canvas, {0, 0, 640, 480}, 0);
    if (market_closed(world)) {
        // ST:2722: 本日休市, any click closes.
        backdrop(frame, 'p', 75, 0);
        text(frame, "本日休市", 324, 244, 2, {72, 0x000000, 0, bold});
        text(frame, "本日休市", 320, 240, 2, {72, 0xf0f0f0, 0, bold});
        hit(host, ids::stock_exit, {0, 0, 640, 480}, exit_sound, [actions] { actions.close(); });
        return;
    }
    backdrop(frame, 'p', 75, std::size_t(page));
    const auto &holder = world.players.at(player);
    const bool row = selected.has_value() && page == 0;
    const Style on{16, 0xf0f0f0, 0x101010, shadow | bold}, off{16, 0xa0a0a0, 0x101010, shadow | bold};
    text(frame, page == 0 ? "持有股數表" : "股價表", 70, 24, 2, on);
    text(frame, "買進", 163, 24, 2, row ? on : off);
    text(frame, "賣出", 237, 24, 2, row ? on : off);
    text(frame, "上市公司資訊", 343, 24, 2, row ? on : off);
    text(frame, financial(holder.state.bank_balance), 540, 24, 6, white_text);
    for (const auto rect : {page_button, buy_button, sell_button, info_button, exit_button})
        if (frame.pointer_down && frame.pointer_in(rect) && !info_open && !pad.active())
            pressed_bevel(frame.canvas, rect);
    if (page == 0)
        draw_prices(host, world, player);
    else
        draw_holdings(host, world);
    if (info_open && selected) {
        draw_info(host, world, *selected);
        hit(host, ids::stock_info_close, {0, 0, 640, 480}, cancel_sound, [this] { info_open = false; });
        return;
    }
    if (!popup_.empty() && frame.ticks < popup_until_)
        popup(host, popup_, 196);
    auto *frame_pointer = &frame;
    const auto refuse = [this, frame_pointer](std::string message) {
        popup_ = std::move(message);
        popup_until_ = frame_pointer->ticks + 1500;
    };
    hit(host, ids::stock_page, tall(page_button), click_sound, [this] { page ^= 1; });
    hit(host, ids::stock_exit, {exit_button.left, 0, 640, 47}, exit_sound, [this, actions] {
        if (page == 1)
            page = 0;
        else
            actions.close();
    });
    if (page == 0) {
        for (std::size_t i = 0; i < world.stocks.size(); ++i)
            hit(host, ids::stock_row + int(i), {16, 80 + 32 * int(i), 625, 112 + 32 * int(i)}, click_sound,
                [this, i] {
                    if (selected == i)
                        info_open = true; // ST:2207: clicking the selected row again opens the info popup
                    selected = i;
                });
    }
    if (!row)
        return;
    const auto stock = *selected;
    const auto &quote = world.stocks[stock];
    const auto trend = stocks::trend(world, stock);
    const auto current = price(world, stock);
    const auto buy_maximum =
        current > 0 ? std::min<std::int32_t>(quote.flags_10, static_cast<std::int32_t>(std::min<double>(
                                                                 INT32_MAX, holder.state.bank_balance / double(current))))
                    : 0;
    const auto holding = holder.stocks[stock].amount;
    const bool suspended = quote.suspension_days != 0;
    hit(host, ids::stock_buy, tall(buy_button), click_sound, [suspended, buy_maximum, trend, refuse, actions, stock, &pad] {
        if (suspended || buy_maximum <= 0)
            return;
        if (trend == stocks::PriceTrend::limit_up) {
            refuse("漲停無法買進！");
            return;
        }
        pad.open(buy_maximum, [actions, stock, refuse](std::int32_t amount) {
            if (amount > 0 && !actions.buy(stock, amount))
                refuse("無法買進！");
        });
    });
    hit(host, ids::stock_sell, tall(sell_button), click_sound, [suspended, holding, trend, refuse, actions, stock, &pad] {
        if (suspended || holding <= 0)
            return;
        if (trend == stocks::PriceTrend::limit_down) {
            refuse("跌停無法賣出！");
            return;
        }
        pad.open(holding, [actions, stock, refuse](std::int32_t amount) {
            if (amount > 0 && !actions.sell(stock, amount))
                refuse("無法賣出！");
        });
    });
    hit(host, ids::stock_info, tall(info_button), click_sound, [this] { info_open = true; });
}

void StockMarket::draw_card_target(Host &host, const World &world, std::size_t player,
                                   const GameDecision *decision, int option_base,
                                   const std::function<void(std::size_t)> &respond) {
    auto &frame = host.frame;
    ui::fill(frame.canvas, {0, 0, 640, 480}, 0);
    backdrop(frame, 'p', 75, 0);
    page = 0;
    const Style off{16, 0xa0a0a0, 0x101010, shadow | bold};
    text(frame, "持有股數表", 70, 24, 2, off);
    text(frame, "買進", 163, 24, 2, off);
    text(frame, "賣出", 237, 24, 2, off);
    text(frame, "上市公司資訊", 343, 24, 2, off);
    text(frame, financial(world.players.at(player).state.bank_balance), 540, 24, 6, white_text);
    draw_prices(host, world, player);
    if (!decision)
        return;
    for (std::size_t index = 0; index < decision->options.size(); ++index) {
        const auto &option = decision->options[index];
        if (option.value < 0) { // 取消: EXIT stands in for the original right-click
            if (frame.pointer_down && frame.pointer_in(exit_button))
                pressed_bevel(frame.canvas, exit_button);
            hit(host, option_base + int(index), {exit_button.left, 0, 640, 47}, exit_sound,
                [this, respond, index] {
                    selected.reset();
                    respond(index);
                });
            continue;
        }
        if (!option.subject || option.subject->kind != CardTargetKind::stock ||
            option.subject->value >= world.stocks.size())
            continue;
        const auto stock = std::size_t(option.subject->value);
        hit(host, option_base + int(index), {16, 80 + 32 * int(stock), 625, 112 + 32 * int(stock)}, click_sound,
            [this, respond, stock, index] {
                if (selected == stock) // touch: a second tap on the outlined row uses the card
                    respond(index);
                else
                    selected = stock; // the original outlines the row under the mouse
            });
    }
}

void StockMarket::draw_prices(Host &host, const World &world, std::size_t player) {
    auto &frame = host.frame;
    const auto &holder = world.players.at(player);
    constexpr std::array<std::pair<const char *, int>, 6> headers{
        {{"股票名稱", 76}, {"成交價", 188}, {"漲跌", 280}, {"交易量", 368}, {"持有股數", 468}, {"平均成本", 572}}};
    for (const auto &[label, x] : headers)
        text(frame, label, x, 64, 2, white_text);
    for (std::size_t i = 0; i < world.stocks.size(); ++i) {
        const int y = 96 + 32 * int(i);
        const auto &quote = world.stocks[i];
        text(frame, stock_name(world, i), 76, y, 2,
             {16, quote.flags_4 ? 0x00f0f0U : 0xf0f0f0U, 0x101010, shadow | bold});
        const auto current = price(world, i), change = current - previous_price(world, i);
        Style quote_style = white_text, change_style = white_text;
        switch (stocks::trend(world, i)) {
        case stocks::PriceTrend::rising:
            quote_style.colour = change_style.colour = 0xff0000;
            break;
        case stocks::PriceTrend::limit_up:
            ui::fill(frame.canvas, {144, y - 10, 144 + 89, y + 10}, rgb(0xd00000));
            quote_style = {16, 0xf0f0f0, 0, bold};
            change_style.colour = 0xff0000;
            break;
        case stocks::PriceTrend::falling:
            quote_style.colour = change_style.colour = 0x00ff00;
            break;
        case stocks::PriceTrend::limit_down:
            ui::fill(frame.canvas, {144, y - 10, 144 + 89, y + 10}, rgb(0x00d000));
            quote_style = {16, 0x101010, 0, bold};
            change_style.colour = 0x00ff00;
            break;
        case stocks::PriceTrend::unchanged:
            break;
        }
        text(frame, format_price(current, current), 225, y, 6, quote_style);
        text(frame, format_price(change, current, true), 305, y, 6, change_style);
        if (quote.suspension_days)
            text(frame, "暫停交易", 368, y, 2, white_text);
        else
            text(frame, std::to_string(quote.flags_10), 401, y, 6, white_text);
        const auto &holding = holder.stocks[i];
        if (holding.amount > 0) {
            text(frame, grouped(holding.amount), 504, y, 6, white_text);
            char cost[32];
            std::snprintf(cost, sizeof cost, "%.2f", double(std::bit_cast<float>(holding.raw_cost_basis)));
            text(frame, cost, 609, y, 6, white_text);
        }
    }
    if (selected)
        outline_rect(frame.canvas, {15, 80 + 32 * int(*selected), 625, 112 + 32 * int(*selected)},
                     rgb(0xffffff));
}

void StockMarket::draw_holdings(Host &host, const World &world) {
    auto &frame = host.frame;
    text(frame, "股票名稱", 71, 64, 2, white_text);
    for (std::size_t p = 0; p < world.players.size() && p < 4; ++p)
        text(frame, names.at(world.players[p].state.character), 168 + 80 * int(p), 64, 2, white_text);
    text(frame, "保留股份", 492, 64, 2, white_text);
    text(frame, "累積盈餘", 580, 64, 2, white_text);
    for (std::size_t i = 0; i < world.stocks.size(); ++i) {
        const int y = 96 + 32 * int(i);
        const bool listed = world.stocks[i].flags_4 != 0;
        const auto *commercial = company(world, i);
        text(frame, stock_name(world, i), 71, y, 2, white_text);
        for (std::size_t p = 0; p < world.players.size() && p < 4; ++p) {
            const int x = 192 + 80 * int(p);
            const auto amount = world.players[p].stocks[i].amount;
            const bool manager = listed && commercial && commercial->owner_code == p + 1;
            if (manager) {
                ui::fill(frame.canvas, {x - 56, y - 10, x + 8, y + 10}, rgb(0x0000ff));
                text(frame, grouped(amount), x, y, 6, {16, 0xf0f000, 0x101010, shadow | bold});
            } else if (amount)
                text(frame, grouped(amount), x, y, 6, white_text);
        }
        if (listed && commercial) {
            text(frame, grouped(commercial->shares_remaining), 520, y, 6, white_text);
            text(frame, financial(commercial->funds), 609, y, 6, white_text);
        }
    }
}

void StockMarket::draw_info(Host &host, const World &world, std::size_t stock) {
    auto &frame = host.frame;
    frame.picture('p', 75, 2, 26, 52);
    text(frame, stock_name(world, stock), 320, 82, 2, {20, 0xf0f0f0, 0x101010, shadow | bold});
    const auto *commercial = world.stocks[stock].flags_4 ? company(world, stock) : nullptr;
    if (commercial && commercial->commerce_type >= 1 && commercial->commerce_type <= 12) {
        // ref_00475530 per map (ST:3746); building levels are not modelled natively.
        constexpr std::array<std::array<std::uint8_t, 12>, 8> pictures{{{11, 0, 7, 5, 6, 9, 3, 0, 0, 4, 10, 0},
                                                                        {11, 0, 7, 5, 6, 9, 3, 0, 0, 4, 10, 0},
                                                                        {11, 0, 8, 5, 6, 9, 3, 0, 0, 4, 10, 0},
                                                                        {11, 0, 7, 5, 6, 9, 3, 0, 0, 4, 10, 0},
                                                                        {11, 0, 7, 12, 6, 9, 3, 0, 0, 4, 10, 0},
                                                                        {11, 23, 7, 14, 6, 9, 3, 0, 0, 4, 10, 15},
                                                                        {11, 0, 7, 13, 6, 9, 3, 0, 0, 4, 10, 0},
                                                                        {11, 24, 7, 5, 6, 9, 3, 0, 0, 4, 10, 0}}};
        const auto map = std::size_t(std::clamp(world.header.stage_id * 4 + world.header.map_id, 0, 7));
        const auto picture = pictures[map][commercial->commerce_type - 1];
        frame.picture('p', 75, std::size_t(3 + picture), 50, 107);
    }
    const Style label{16, 0xf0f0f0, 0x101010, shadow | bold};
    text(frame, "本月盈餘", 212, 123, 6, label);
    text(frame, "平均盈餘", 212, 162, 6, label);
    text(frame, "經 營 者", 212, 203, 6, label);
    text(frame, "成交價", 395, 123, 6, label);
    text(frame, "漲  跌", 395, 162, 6, label);
    text(frame, "週均價", 395, 203, 6, label);
    text(frame, "交易量", 531, 123, 6, label);
    text(frame, "漲跌幅", 531, 162, 6, label);
    text(frame, "月均價", 531, 203, 6, label);
    text(frame, "歷史高價", 531, 243, 6, label);
    text(frame, "歷史低價", 531, 283, 6, label);
    const Style small{12, 0xf0f0f0, 0x101010, shadow};
    text(frame, "半年內股價走勢線圖", 236, 248, 2, small);
    text(frame, "持股比例", 502, 318, 2, small);

    if (commercial) {
        text(frame, financial(commercial->funds), 309, 123, 6, label);
        const auto average = world.month_counter ? commercial->funds_change / std::int64_t(world.month_counter)
                                                 : commercial->funds_change;
        text(frame, financial(average), 309, 162, 6, label);
        if (commercial->owner_code >= 1 && commercial->owner_code <= world.players.size())
            text(frame, names.at(world.players[commercial->owner_code - 1].state.character), 269, 203, 2, label);
    }
    const auto current = price(world, stock), previous = previous_price(world, stock);
    text(frame, format_price(current, current), 453, 123, 6, label);
    text(frame, format_price(current - previous, current, true), 453, 162, 6, label);
    text(frame, std::to_string(world.stocks[stock].flags_10), 589, 123, 6, label);
    char percent[32];
    std::snprintf(percent, sizeof percent, "%.2f", previous ? double((current - previous) / previous * 100) : 0.0);
    text(frame, percent, 589, 162, 6, label);
    const auto samples = history(world, stock);
    const auto mean_of_last = [&samples](std::size_t count) {
        const auto n = std::min(count, samples.size());
        double sum = 0;
        for (std::size_t i = samples.size() - n; i < samples.size(); ++i)
            sum += samples[i];
        return n ? float(sum / double(n)) : 0.0F;
    };
    const auto week = mean_of_last(6), month = mean_of_last(24);
    text(frame, format_price(week, week), 453, 203, 6, label);
    text(frame, format_price(month, month), 589, 203, 6, label);
    float high = 0, low = 10000;
    for (const auto sample : samples) {
        high = std::max(high, sample);
        low = std::min(low, sample);
    }
    if (!samples.empty()) {
        text(frame, format_price(high, high), 589, 243, 6, label);
        text(frame, format_price(low, low), 589, 283, 6, label);
        // ST:1450-1635 price chart.
        const float middle = (high + low) / 2, range = high - low;
        const float scale = middle > 0 && range / middle > 0.3F ? 109 / range : middle > 0 ? 109 / (0.6F * middle) : 0;
        const auto y_of = [&](float value) { return int(324 - (value - middle) * scale); };
        for (std::size_t k = 1; k < samples.size(); ++k)
            line(frame.canvas, 92 + 2 * int(k - 1), y_of(samples[k - 1]), 92 + 2 * int(k), y_of(samples[k]),
                 rgb(0xffffff));
        text(frame, format_price(high, high), 88, y_of(high) - 8, 6, small);
        text(frame, format_price(low, low), 88, y_of(low) + 8, 6, small);
    }
    // ST:1262-1449 shareholding pie: the active player's share of 10000, clockwise from 12 o'clock.
    const auto holding = world.players.at(world.header.current_player).stocks[stock].amount;
    const double fraction = std::clamp(holding / 10000.0, 0.0, 1.0);
    const auto owned = [fraction](double dx, double dy) {
        double angle = std::atan2(dx, -dy); // 0 at 12 o'clock, clockwise
        if (angle < 0)
            angle += 2 * std::numbers::pi;
        return angle < 2 * std::numbers::pi * fraction;
    };
    for (int y = 336; y <= 401; ++y)
        for (int x = 414; x <= 591; ++x) {
            const double tx = (x - 502.5) / 88.5, ty = (y - 365) / 29.0;
            const double bx = (x - 502.5) / 88.5, by = (y - 372) / 29.0;
            const bool top = tx * tx + ty * ty <= 1, base = bx * bx + by * by <= 1;
            std::uint16_t colour = 0;
            if (top) {
                const bool edge = tx * tx + ty * ty > 0.94;
                colour = edge ? rgb(0x000000) : owned(tx, ty) ? rgb(0xd00000) : rgb(0x0000d0);
            } else if (base || (std::abs(tx) <= 1 && y > 365 && y < 372)) {
                const bool edge = base && bx * bx + by * by > 0.94;
                colour = edge ? rgb(0x000000) : owned(tx, 0.0001) && tx > 0 && fraction > 0.25 ? rgb(0x900000)
                                                                                                : rgb(0x000090);
            } else
                continue;
            frame.canvas.pixels[std::size_t(y * frame.canvas.width + x)] = colour;
        }
}

} // namespace rich4::native::screens
