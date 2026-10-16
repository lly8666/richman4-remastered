// SPDX-License-Identifier: GPL-3.0-or-later
// Player trading board (Panel 73, §9), lottery bar (Panel 12, §7.1) and auction room
// (Panel 26, §8) of docs/ui/finance-menus.md.
#include "finance.hpp"

#include "../native_stock_names.hpp"
#include "rich4/calendar.hpp"
#include "rich4/economy.hpp"
#include "rich4/flc.hpp"
#include "rich4/services.hpp"
#include "rich4/world.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <iostream>
#include <map>

namespace rich4::native::screens {
namespace {

std::string number(const char *pattern, long long value) {
    char buffer[128];
    std::snprintf(buffer, sizeof buffer, pattern, value);
    return buffer;
}

constexpr Style font_a{16, 0xf0f0f0, 0x101010, shadow | bold};  // general text
constexpr Style font_b{16, 0xffffff, 0x800000, shadow | bold};  // red buttons
constexpr Style font_c{20, 0x101010, 0, bold};                  // tab labels
constexpr Style font_d{20, 0xffffff, 0, bold};                  // column headers
constexpr Style small_black{12, 0x101010, 0, 0};

std::string item_name(const World &world, OfferKind kind, std::uint16_t item) {
    if (kind == OfferKind::stock)
        return std::string(
            native_stock_names.at(std::size_t(world.header.stage_id * 4 + world.header.map_id) * 12 + item));
    if (kind == OfferKind::tool && item >= 1 && item <= services::tool_names.size())
        return std::string(services::tool_names[item - 1]);
    if (kind == OfferKind::card && item >= 1 && item <= services::card_names.size())
        return std::string(services::card_names[item - 1]);
    if (kind == OfferKind::property && item > 4000 && std::size_t(item - 4000) < world.facilities.size())
        return ui::big5(world.facilities[item - 4000].name);
    if (kind == OfferKind::property && item > 2000 && std::size_t(item - 2000) < world.lands.size())
        return ui::big5(world.lands[item - 2000].economy.street_name);
    return "?";
}

struct PropertyRow {
    std::string place, development;
    std::int64_t price = 0, toll = 0;
    std::uint32_t expiry = 0;
    bool chain = false, facility = false, built = false;
};
PropertyRow property_row(const World &world, std::uint16_t code) {
    PropertyRow row;
    const auto index = world.header.price_index;
    if (code > 4000 && std::size_t(code - 4000) < world.facilities.size()) {
        const auto &facility = world.facilities[code - 4000];
        row = {ui::big5(facility.name), number("%lld級", facility.level),
               (std::int64_t(facility.land_price) + std::int64_t(facility.price_per_level()) * facility.level) * index,
               std::int64_t(facility.toll_by_level[std::min<std::size_t>(facility.level, 5)]) * index,
               facility.expired_date, false, true, facility.level > 0};
    } else if (code > 2000 && std::size_t(code - 2000) < world.lands.size()) {
        const auto &land = world.lands[code - 2000];
        std::vector<LandEconomyRecord> records;
        for (std::size_t i = 1; i < world.lands.size(); ++i)
            records.push_back(world.lands[i].economy);
        const bool chain = land.economy.chain_store != 0;
        row = {ui::big5(land.economy.street_name),
               chain ? "連鎖店" : land.economy.level ? number("%lld層", land.economy.level) : "空地",
               (std::int64_t(land.land_price) + std::int64_t(land.price_per_level) * land.economy.level) * index,
               chain ? land_toll(records, land.economy.owner_code, index)
                     : land_toll(records, land.economy.owner_code, index, std::string_view(land.economy.street_name)),
               land.expired_date, chain, false, land.economy.level > 0};
    }
    return row;
}
std::string lease(std::uint32_t expiry) {
    if (!expiry)
        return "無限期";
    const auto date = calendar::unpack(expiry);
    return number("%02lld/", date.month) + std::to_string(date.day) + "/" + std::to_string(date.year);
}
bool in_tab(const PropertyRow &row, int tab) {
    switch (tab) {
    case 1:
        return !row.facility && !row.chain;
    case 2:
        return row.facility;
    case 3:
        return !row.facility && !row.chain && row.built;
    case 4:
        return row.chain;
    default:
        return true;
    }
}

} // namespace

// ---------------------------------------------------------------------------------------
// Trading board

void TradingBoard::notice(std::string message) {
    notice_ = std::move(message);
    notice_until_ = now_ + 1500;
}

void TradingBoard::cancel(Host &host, const TradingActions &actions) {
    if (host.sound)
        host.sound(cancel_sound);
    if (view_ == View::board)
        actions.close();
    else
        view_ = View::board;
}

void TradingBoard::draw(Host &host, const World &world, std::size_t actor, NumberPad &pad,
                        const TradingActions &actions) {
    auto &frame = host.frame;
    now_ = frame.ticks;
    draw_board(host, world, actor, actions);
    if (view_ == View::detail && detail_)
        draw_detail(host, world, actor, actions);
    else if (view_ != View::board)
        draw_picker(host, world, actor, pad, actions);
    if (pad.active() && !prompt_.empty()) {
        frame.picture('p', 73, 5, 227, pad.prompt_top());
        text(frame, prompt_, 319, pad.prompt_top() + 44, 4, black_text);
    }
    if (!notice_.empty() && now_ < notice_until_)
        popup(host, notice_, 196);
}

void TradingBoard::draw_board(Host &host, const World &world, std::size_t actor, const TradingActions &actions) {
    auto &frame = host.frame;
    const bool active = view_ == View::board;
    frame.picture('p', 73, 0, 22, 66);
    for (std::size_t p = 0; p < world.players.size() && p < 4; ++p)
        if (world.players[p].state.controller_flags)
            graphics::blit(frame.canvas, frame.artwork.frame('m', 27 + world.players[p].state.character, 0), 66,
                           150 + 72 * int(p));
    const auto quotes = trading::quotes(world);
    std::array<int, 4> columns{};
    int own = 0;
    for (std::size_t i = 0; i < quotes.size(); ++i) {
        const auto &quote = quotes[i];
        if (quote.seller >= 4)
            continue;
        own += quote.seller == actor;
        const int column = columns[quote.seller]++;
        const int x = 104 + 72 * column, y = 114 + 72 * int(quote.seller);
        const auto sex = world.players[quote.seller].state.sex ? 1 : 0;
        frame.picture('p', 73, std::size_t(8 + int(quote.offer.kind) + 4 * sex), x, y);
        text(frame, financial(quote.offer.price), x + 36, y + 63, 2, small_black);
        const graphics::Rect cell{x, y, x + 72, y + 72};
        if (active && frame.pointer_down && frame.pointer_in(cell))
            darken(frame.canvas, cell, -12);
        if (active)
            hit(host, ids::trade_offer + int(i), cell, click_sound, [this, quote] {
                detail_ = quote;
                view_ = View::detail;
            });
    }
    const graphics::Rect sale{465, 75, 536, 114}, exit{537, 75, 608, 114};
    if (active && frame.pointer_down && frame.pointer_in(sale))
        pressed_bevel(frame.canvas, sale);
    if (active && frame.pointer_down && frame.pointer_in(exit))
        pressed_bevel(frame.canvas, exit);
    if (!active)
        return;
    hit(host, ids::trade_sale, sale, click_sound, [this, own] {
        if (own >= 7)
            notice("公佈欄已滿\n\n請先撤件！");
        else
            view_ = View::menu;
    });
    hit(host, ids::trade_exit, exit, exit_sound, actions.close);
}

void TradingBoard::ask_price(const World &world, std::size_t actor, NumberPad &pad, const TradingActions &actions,
                             trading::Item item, std::uint16_t quantity) {
    const auto limits = trading::listing_limits(world, actor, item.kind, item.id, quantity);
    if (!limits || limits->maximum_price <= 0) {
        notice("無法出售！");
        view_ = View::board;
        return;
    }
    prompt_ = "請輸入欲拍賣的價格\n\n（市價：" + std::to_string(limits->reference_price) + "元）";
    suggestion_ = std::max(1, limits->reference_price);
    pad.open(limits->maximum_price, [this, actions, item, quantity](std::int32_t price) {
        prompt_.clear();
        if (price <= 0)
            return;
        if (actions.post({item.kind, item.id, price, quantity}))
            view_ = View::board;
        else
            notice("無法刊登！");
    });
}

void TradingBoard::draw_picker(Host &host, const World &world, std::size_t actor, NumberPad &pad,
                               const TradingActions &actions) {
    auto &frame = host.frame;
    const auto items = trading::listable_items(world, actor);
    if (view_ == View::menu) {
        frame.picture('p', 73, 17, 464, 116);
        for (int k = 0; k < 4; ++k) {
            const graphics::Rect cell{473 + 63 * (k / 2), 125 + 39 * (k % 2), 536 + 63 * (k / 2), 164 + 39 * (k % 2)};
            if (frame.pointer_in(cell))
                darken(frame.canvas, cell, -12);
            const auto kind = static_cast<OfferKind>(k + 1);
            const bool any = std::any_of(items.begin(), items.end(), [kind](const auto &item) { return item.kind == kind; });
            hit(host, ids::trade_category + k, cell, click_sound, [this, k, any] {
                if (!any) {
                    notice("沒有可出售的物品！");
                    view_ = View::board;
                    return;
                }
                category_ = k;
                scroll_ = 0;
                view_ = k == 0 ? View::stocks : k == 1 ? View::properties : View::items;
            });
        }
        hit(host, ids::trade_outside, {0, 0, 640, 480}, cancel_sound, [this] { view_ = View::board; });
        return;
    }
    if (pad.active())
        return; // The pad is modal; the picker stays visible underneath.
    const auto choose = [this, &world, actor, &pad, actions](const trading::Item &item) {
        if (item.kind == OfferKind::stock) {
            prompt_ = "請輸入欲賣出的張數";
            suggestion_ = 1;
            pad.open(item.maximum_quantity, [this, &world, actor, &pad, actions, item](std::int32_t quantity) {
                prompt_.clear();
                if (quantity > 0)
                    ask_price(world, actor, pad, actions, item, static_cast<std::uint16_t>(quantity));
            });
        } else
            ask_price(world, actor, pad, actions, item, 1);
    };
    graphics::Rect window{};
    if (view_ == View::stocks) {
        window = {152, 32, 488, 448};
        frame.picture('p', 73, 1, 152, 32);
        text(frame, "股票名稱", 200, 48, 2, font_d);
        text(frame, "持有張數", 300, 48, 2, font_d);
        text(frame, "總 市 價", 420, 48, 2, font_d);
        int row = 0;
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (items[i].kind != OfferKind::stock || row >= 12)
                continue;
            const int y = 80 + 32 * row++;
            const auto value = std::int64_t(items[i].maximum_quantity *
                                            double(std::bit_cast<float>(world.stocks.at(items[i].id).float_bits[2])));
            text(frame, item_name(world, OfferKind::stock, items[i].id), 200, y, 2, font_a);
            text(frame, grouped(items[i].maximum_quantity), 332, y, 6, font_a);
            text(frame, grouped(value), 478, y, 6, font_a);
            const graphics::Rect line{152, y - 16, 488, y + 16};
            if (frame.pointer_in(line))
                outline_rect(frame.canvas, line, rgb(0xffffff));
            hit(host, ids::trade_item + int(i), line, click_sound, [choose, item = items[i]] { choose(item); });
        }
        hit(host, ids::trade_picker_close, {448, 26, 500, 64}, exit_sound, [this] { view_ = View::board; });
    } else if (view_ == View::properties) {
        window = {112, 32, 528, 448};
        frame.picture('p', 73, 2, 112, 32);
        constexpr std::array<const char *, 5> tabs{"全  部", "住宅區", "商業區", "房  屋", "連鎖店"};
        for (int i = 0; i < 5; ++i) {
            if (i == tab_)
                frame.picture('p', 73, 19, 112 + 80 * i, 32);
            text(frame, tabs[std::size_t(i)], 152 + 80 * i, 47, 2, font_c);
            hit(host, ids::trade_tab + i, {112 + 80 * i, 33, 192 + 80 * i, 62}, click_sound, [this, i] {
                tab_ = i;
                scroll_ = 0;
            });
        }
        const std::array<std::pair<const char *, int>, 5> headers{
            {{"地  點", 147}, {"開發狀況", 231}, {"價  格", 293}, {"收  費", 369}, {"租  期", 471}}};
        for (const auto &[label, x] : headers)
            text(frame, label, x, 80, 2, font_d);
        std::vector<std::size_t> visible;
        for (std::size_t i = 0; i < items.size(); ++i)
            if (items[i].kind == OfferKind::property && in_tab(property_row(world, items[i].id), tab_))
                visible.push_back(i);
        if (scroll_ >= visible.size())
            scroll_ = 0;
        for (std::size_t r = 0; r < 11 && scroll_ + r < visible.size(); ++r) {
            const auto index = visible[scroll_ + r];
            const auto row = property_row(world, items[index].id);
            const int y = 112 + 32 * int(r);
            text(frame, row.place, 147, y, 2, font_a);
            text(frame, row.development, 231, y, 2, font_a);
            text(frame, grouped(row.price), 319, y, 6, font_a);
            text(frame, grouped(row.toll), 395, y, 6, font_a);
            text(frame, lease(row.expiry), 471, y, 2, font_a);
            const graphics::Rect line{112, y - 16, 512, y + 16};
            if (frame.pointer_in(line))
                outline_rect(frame.canvas, line, rgb(0xffffff));
            hit(host, ids::trade_item + int(index), line, click_sound, [choose, item = items[index]] { choose(item); });
        }
        hit(host, ids::trade_picker_close, {512, 26, 548, 62}, exit_sound, [this] { view_ = View::board; });
        hit(host, ids::trade_up, {512, 62, 548, 98}, click_sound, [this] { scroll_ = scroll_ >= 11 ? scroll_ - 11 : 0; });
        hit(host, ids::trade_down, {512, 98, 548, 134}, click_sound, [this, total = visible.size()] {
            if (scroll_ + 11 < total)
                scroll_ += 11;
        });
    } else {
        const bool cards = category_ == 3;
        window = {140, 160, 500, 288};
        frame.picture('p', 73, cards ? 4 : 3, 140, 160);
        int cell = 0;
        for (std::size_t i = 0; i < items.size() && cell < 15; ++i) {
            if (items[i].kind != (cards ? OfferKind::card : OfferKind::tool))
                continue;
            const int x = 140 + 72 * (cell % 5), y = 192 + 32 * (cell / 5);
            ++cell;
            if (cards)
                text(frame, item_name(world, OfferKind::card, items[i].id), x + 36, y + 16, 2,
                     {14, 0xffffff, 0x101010, shadow | bold});
            else {
                if (items[i].id >= 1 && items[i].id <= 13)
                    graphics::blit(frame.canvas, frame.artwork.frame('p', 74, items[i].id - 1u), x + 20, y + 16);
                text(frame, "×" + std::to_string(items[i].maximum_quantity), x + 66, y + 16, 6, font_a);
            }
            const graphics::Rect box{x, y, x + 72, y + 32};
            if (frame.pointer_in(box))
                darken(frame.canvas, box, -12);
            hit(host, ids::trade_item + int(i), box, click_sound, [choose, item = items[i]] { choose(item); });
        }
        hit(host, ids::trade_picker_close, {429, 161, 500, 192}, exit_sound, [this] { view_ = View::board; });
    }
    hit(host, ids::trade_outside - 100, window, no_sound, [] {});
    hit(host, ids::trade_outside, {0, 0, 640, 480}, cancel_sound, [this] { view_ = View::board; });
}

void TradingBoard::draw_detail(Host &host, const World &world, std::size_t actor, const TradingActions &actions) {
    auto &frame = host.frame;
    const auto quote = *detail_;
    const auto &offer = quote.offer;
    const std::size_t art = offer.kind == OfferKind::stock ? 7 : offer.kind == OfferKind::property ? 8 : 6;
    const int height = art == 6 ? 224 : art == 7 ? 256 : 288;
    const int left = 224, top = 240 - height / 2;
    frame.picture('p', 73, art, left, top);
    text(frame, names.at(world.players.at(quote.seller).state.character), left + 57, top + 37, 2, font_a);
    std::vector<std::pair<std::string, std::string>> rows;
    const auto reference = trading::listing_limits(world, quote.seller, offer.kind, offer.item,
                                                   offer.kind == OfferKind::stock ? offer.quantity : 0);
    if (offer.kind == OfferKind::stock) {
        rows = {{"股票", item_name(world, offer.kind, offer.item)},
                {"張數", grouped(offer.quantity)},
                {"市價", reference ? financial(reference->reference_price) : "-"},
                {"售價", financial(offer.price)}};
    } else if (offer.kind == OfferKind::property) {
        const auto row = property_row(world, offer.item);
        rows = {{"地點", row.place},
                {"開發", row.development},
                {"收費", financial(row.toll)},
                {"市價", reference ? financial(reference->reference_price) : financial(row.price)},
                {"售價", financial(offer.price)}};
    } else
        rows = {{offer.kind == OfferKind::tool ? "道具" : "卡片", item_name(world, offer.kind, offer.item)},
                {"市價", reference ? financial(reference->reference_price) : "-"},
                {"售價", financial(offer.price)}};
    for (std::size_t k = 0; k < rows.size(); ++k) {
        const int y = top + 91 + 30 * int(k);
        text(frame, rows[k].first, left + 29, y, 2, {14, 0xffffff, 0x101010, shadow | bold});
        text(frame, rows[k].second, left + 178, y, 6, font_a);
    }
    const bool own = quote.seller == actor;
    const graphics::Rect act{left + 14, top + height - 34, left + 85, top + height - 11},
        close{left + 102, top + height - 34, left + 173, top + height - 11};
    for (const auto rect : {act, close})
        if (frame.pointer_down && frame.pointer_in(rect))
            pressed_bevel(frame.canvas, rect);
    const auto finger = [](graphics::Rect rect) { // the red buttons are only 23 px tall
        return graphics::Rect{rect.left - 6, rect.top - 9, rect.right + 6, rect.bottom + 9};
    };
    text(frame, own ? "撤 件" : "購 買", (act.left + act.right) / 2, (act.top + act.bottom) / 2, 2, font_b);
    text(frame, "離 開", (close.left + close.right) / 2, (close.top + close.bottom) / 2, 2, font_b);
    const auto cash = world.players.at(actor).state.cash;
    hit(host, ids::trade_act, finger(act), click_sound, [this, quote, own, actions, cash] {
        if (own) {
            if (actions.cancel(quote.index))
                view_ = View::board;
            return;
        }
        if (actions.buy(quote.seller, quote.index))
            view_ = View::board;
        else
            notice(cash < quote.offer.price ? "您的現金不足！" : "無法購買！");
    });
    hit(host, ids::trade_detail_exit, finger(close), exit_sound, [this] { view_ = View::board; });
    hit(host, ids::trade_outside - 100, {left, top, left + 192, top + height}, no_sound, [] {});
    hit(host, ids::trade_outside, {0, 0, 640, 480}, cancel_sound, [this] { view_ = View::board; });
}

// ---------------------------------------------------------------------------------------
// Lottery bar (_rich4_ui_letou_bar_entry)

namespace {
// Panel 14: the bar's 5-frame neon sign FLC (213x68), looped at (8,8) and advanced on every 100 ms
// tick (LT:2932). Index 0 is transparent over the bar. Decoded once per asset set.
const std::vector<graphics::Image> &neon_frames(Artwork &artwork) {
    static std::map<const Artwork *, std::vector<graphics::Image>> cache;
    auto &frames = cache[&artwork];
    if (frames.empty()) {
        try {
            flc::Decoder movie(artwork.archive('p').decode(14));
            while (frames.size() < movie.frame_count() && movie.next())
                frames.push_back(movie.image());
        } catch (const std::exception &error) {
            std::cerr << "Lottery neon skipped: " << error.what() << '\n';
        }
        if (frames.empty())
            frames.emplace_back(); // never retry a broken asset every frame
    }
    return frames;
}
} // namespace

void draw_lottery(Host &host, const World &world, const GameDecision &decision,
                  const std::function<void(std::size_t)> &respond, LotteryFarewell *farewell,
                  const std::function<std::uint32_t(bool picked)> &goodbye) {
    auto &frame = host.frame;
    if (farewell && farewell->option && *farewell->option >= decision.options.size())
        *farewell = {};
    const bool leaving = farewell && farewell->option.has_value();
    backdrop(frame, 'p', 12, 0);
    if (const auto &neon = neon_frames(frame.artwork); !neon.front().pixels.empty())
        graphics::blit(frame.canvas, neon[(frame.ticks / 100) % neon.size()], 8, 8);
    if (leaving)
        frame.picture('p', 12, 2, 154, -8); // the wink (LT:1227)
    else
        frame.picture('p', 12, 1, 210, -5);
    if (!leaving && (frame.ticks / 100) % 35 < 3) {
        constexpr std::array<std::size_t, 3> blink{3, 4, 3};
        frame.picture('p', 12, blink[(frame.ticks / 100) % 35], 273, 63);
    }
    // Jackpot: Panel 13 digits right-aligned ending at x=184 (18 px per digit, 12 per comma).
    frame.picture('p', 12, 9, 28, 27);
    const auto pool = ui::money(world.public_fund);
    int right = 184;
    for (auto character = pool.rbegin(); character != pool.rend(); ++character) {
        const bool comma = *character == ',';
        const int width = comma ? 12 : 18;
        const std::size_t glyph = comma ? 10 : *character == '$' ? 11 : std::size_t(*character - '0');
        graphics::blit(frame.canvas, frame.artwork.frame('p', 13, glyph), right - width / 2, 41);
        right -= width;
    }
    const auto slot = decision.player;
    const bool can_buy = std::any_of(decision.options.begin(), decision.options.end(),
                                     [](const auto &option) { return option.value > 0; });
    std::string line = can_buy ? "請圈選您的\n幸運號碼～" : "太可惜了！\n您的現金不足～";
    if (leaving)
        line = farewell->number >= 0 ? "拜拜！\n祝您中獎！" : "下次再來吧！";
    frame.picture('p', 12, 8, 360, 20);
    text(frame, line, 498, 116, 4, {20, 0x101010, 0, bold});
    if ((frame.ticks / 150) % 2)
        frame.picture('p', 12, 5, 273, 105);
    for (int n = 0; n < 36; ++n) {
        const int column = n % 9, row = n / 9;
        const int x = 30 + 64 * column, y = 271 + 48 * row;
        const auto owner = world.lottery_tickets[std::size_t(n)];
        if (owner)
            darken(frame.canvas, {31 + 64 * column, 272 + 48 * row, 93 + 64 * column, 318 + 48 * row}, -10);
        if (owner == slot + 1 || (leaving && farewell->number == n))
            graphics::blit(frame.canvas, frame.artwork.frame('p', 12, 7), x + 32, y + 24);
    }
    if (leaving) { // hold until the line is said (at least 2 s, fcn_0044ee18); a tap ends it early
        const auto finish = [respond, farewell] {
            const auto index = *farewell->option;
            *farewell = {};
            respond(index);
        };
        if (frame.ticks >= farewell->started + std::max<std::uint32_t>(2000, farewell->voice_ms)) {
            finish();
            return;
        }
        hit(host, 210 + int(*farewell->option), {0, 0, 640, 480}, click_sound, finish);
        return;
    }
    const auto choose = [respond, farewell, goodbye, instant = host.instant, &frame](std::size_t index, int number) {
        if (!farewell || instant) {
            respond(index);
            return;
        }
        *farewell = {index, number, frame.ticks, goodbye ? goodbye(number >= 0) : 0U};
    };
    if (frame.pointer_in({30, 271, 606, 463})) {
        const int n = (frame.pointer_y - 271) / 48 * 9 + (frame.pointer_x - 30) / 64;
        if (n >= 0 && n < 36 && !world.lottery_tickets[std::size_t(n)] && can_buy)
            graphics::blit(frame.canvas, frame.artwork.frame('p', 12, 7), 30 + 64 * (n % 9) + 32,
                           271 + 48 * (n / 9) + 24);
    }
    std::optional<std::size_t> leave;
    for (std::size_t index = 0; index < decision.options.size(); ++index) {
        const auto value = decision.options[index].value;
        if (value <= 0 || value > 36) {
            leave = index;
            continue;
        }
        const int n = value - 1;
        hit(host, 210 + int(index), {30 + 64 * (n % 9), 271 + 48 * (n / 9), 94 + 64 * (n % 9), 319 + 48 * (n / 9)},
            click_sound, [choose, index, n] { choose(index, n); });
    }
    if (leave) // Tapping anywhere else leaves, like the original right-click (下次再來吧！).
        hit(host, 210 + int(*leave), {0, 0, 640, 480}, cancel_sound,
            [choose, respond, index = *leave, can_buy] {
                if (can_buy)
                    choose(index, -1);
                else
                    respond(index); // 太可惜了！ is already on screen
            });
}

// ---------------------------------------------------------------------------------------
// Auction room (_rich4_ui_auction_entry)

namespace {
constexpr Style auction_setup{18, 0xffffff, 0x101010, shadow | bold};
constexpr unsigned bid_frame_ms = 200; // bid animation: sprite frames 1..4,0 in about 1 s (AU:1776)
constexpr std::uint64_t sold_ms = 3500;

// Room, auctioneer (idle 26:25 or gavel 26:21), the lot on the table and the price; returns the seller.
int draw_room(Frame &frame, const World &world, std::uint16_t code, std::int32_t price, bool gavel) {
    backdrop(frame, 'p', 26, 0);
    frame.picture('p', 26, 17, 67, 63);
    if (gavel)
        frame.picture('p', 26, 21, 124, 27);
    else
        frame.picture('p', 26, 25, 123, 24);
    // Property on the table (AU:2484-2620; building types are not modelled, type 0 is used).
    std::size_t picture = 90;
    int seller = -1;
    if (code > 2000 && code < 4000 && std::size_t(code - 2000) < world.lands.size()) {
        const auto &land = world.lands[code - 2000].economy;
        seller = land.owner_code ? int(land.owner_code) - 1 : -1;
        if (land.level)
            picture = std::size_t((world.header.stage_id ? 114 : 29) + land.level);
        else if (land.owner_code && land.owner_code <= world.players.size())
            picture = 91 + world.players[land.owner_code - 1].state.character;
    } else if (code > 4000 && std::size_t(code - 4000) < world.facilities.size()) {
        const auto owner = world.facilities[code - 4000].owner_code;
        seller = owner ? int(owner) - 1 : -1;
    }
    if (picture < frame.artwork.frame_count('p', 26))
        graphics::blit(frame.canvas, frame.artwork.frame('p', 26, picture), 232, 180);
    text(frame, "標價：", 182, 242, 0, auction_setup);
    text(frame, std::to_string(price) + "元", 272, 262, 6, auction_setup);
    return seller;
}

void announce(Frame &frame, std::string_view line) {
    graphics::blit(frame.canvas, frame.artwork.frame('p', 26, 2), 410, 60);
    text(frame, line, 410, 60, 4, {16, 0x101010, 0, bold});
}

// Bidder column at (590, 80+120k): Panel 27+3c bidding (5 frames), 28+3c cannot bid (1 frame),
// 29+3c winner (5 frames); `pose(k)` returns {entry offset 0/1/2, frame}.
template <typename Pose>
void draw_bidders(Frame &frame, const World &world, std::optional<std::size_t> current, int seller,
                  Pose pose) {
    for (std::size_t k = 0; k < world.players.size() && k < 4; ++k) {
        const auto &player = world.players[k].state;
        if (!player.controller_flags)
            continue;
        const int y = 80 + 120 * int(k);
        const auto [offset, index] = pose(k);
        const auto entry = std::size_t(27 + 3 * player.character + offset);
        const auto count = std::max<std::size_t>(frame.artwork.frame_count('p', entry), 1);
        graphics::blit(frame.canvas, frame.artwork.frame('p', entry, std::size_t(index) % count), 590, y);
        if (current == k)
            graphics::blit(frame.canvas, frame.artwork.frame('p', 26, std::size_t(78 + player.character)), 590, y);
        if (int(k) == seller)
            text(frame, "賣方", 590, y + 14, 2, auction_setup);
        else
            text(frame, financial(player.cash) + "元", 620, y + 14, 6, auction_setup);
    }
}
} // namespace

void AuctionRoom::draw(Host &host, const World &world, const GameDecision &decision,
                       const std::function<void(std::size_t)> &respond) {
    auto &frame = host.frame;
    const auto key = std::to_string(decision.land_id) + ':' + std::to_string(decision.player) + ':' +
                     std::to_string(decision.amount);
    if (bid_ && bid_->key != key)
        bid_.reset(); // a different bidder or auction: nothing in flight belongs to it
    if (bid_ && !bid_->started)
        bid_->started = frame.ticks;
    if (bid_ && frame.ticks >= bid_->started + 5 * bid_frame_ms) {
        const auto index = bid_->index;
        bid_.reset();
        respond(index); // the decision is gone now: draw nothing more of it this frame
        return;
    }
    const auto price = bid_ ? bid_->price : decision.amount;
    const auto seller = draw_room(frame, world, decision.land_id, price, false);
    announce(frame, "目前標價" + std::to_string(decision.amount) + "元\n請意者出價。");
    const auto bid_frame = bid_ ? int((frame.ticks - bid_->started) / bid_frame_ms) : 0;
    draw_bidders(frame, world, decision.player, seller, [&](std::size_t k) {
        if (decision.inactive_players & (1U << k))
            return std::pair{1, 0}; // ineligible or withdrew (AU:2798-2830)
        if (bid_ && k == decision.player)
            return std::pair{0, (bid_frame + 1) % 5}; // frames 1..4,0
        return std::pair{0, 0};
    });
    if (bid_) { // starburst 26:1 next to the bidder with the new price, ＰＡＳＳ or 放棄
        const int y = 80 + 120 * int(decision.player);
        graphics::blit(frame.canvas, frame.artwork.frame('p', 26, 1), 480, y - 15);
        const auto label = bid_->action == 0 ? std::string("ＰＡＳＳ")
                           : bid_->action == 6 ? std::string("放棄")
                                               : std::to_string(bid_->price) + "元";
        text(frame, label, 480, y - 15, 4, {16, 0x101010, 0, bold});
    }
    // Seven bid buttons: PASS, +100, +500, +1000, +5000, +10000, give up (ref_00475b84).
    constexpr std::array<int, 7> rows{133, 181, 229, 277, 325, 373, 421};
    constexpr std::array<std::int32_t, 7> raise{0, 100, 500, 1000, 5000, 10000, 0};
    for (int i = 0; i < 7; ++i) {
        const graphics::Rect rect{363, rows[std::size_t(i)] - 19, 450, rows[std::size_t(i)] + 20};
        const bool pressed = (frame.pointer_down && frame.pointer_in(rect)) || (bid_ && bid_->action == i);
        graphics::blit(frame.canvas, frame.artwork.frame('p', 26, std::size_t(2 * i + (pressed ? 3 : 4))), 406,
                       rows[std::size_t(i)]);
        for (std::size_t index = 0; index < decision.options.size(); ++index) {
            if (decision.options[index].value != i || (bid_ && bid_->index != index))
                continue;
            // While the bid animates, a tap anywhere finishes it at once.
            hit(host, 210 + int(index), bid_ ? graphics::Rect{0, 0, 640, 480} : rect,
                bid_ ? no_sound : click_sound,
                [this, instant = host.instant, sound = host.sound, respond, index, i, key,
                 price = decision.amount + raise[std::size_t(i)]] {
                    if (bid_ || instant) { // replays never wait for the picture
                        bid_.reset();
                        respond(index);
                        return;
                    }
                    bid_ = Bid{key, index, i, price, 0};
                    if (sound)
                        sound(29); // auction bid (ui_auction.asm:1776)
                });
        }
    }
}

void AuctionRoom::start_sold(std::size_t winner, std::int32_t price, std::uint16_t property) {
    sold_ = Sold{winner, price, property, 0, false};
}

void AuctionRoom::draw_sold(Host &host, const World &world) {
    if (!sold_)
        return;
    auto &frame = host.frame;
    if (!sold_->started)
        sold_->started = std::max<std::uint64_t>(frame.ticks, 1);
    const auto elapsed = frame.ticks - sold_->started;
    if (elapsed >= sold_ms || sold_->winner >= world.players.size()) {
        sold_.reset();
        return;
    }
    if (!sold_->sounded) {
        sold_->sounded = true;
        host.sound(63); // gavel (ui_auction.asm:502)
    }
    (void)draw_room(frame, world, sold_->property, sold_->price, true); // the lot now belongs to the winner
    const auto winner = sold_->winner;
    const std::string buyer = names.at(world.players[winner].state.character);
    announce(frame, elapsed < sold_ms / 2 ? std::to_string(sold_->price) + "元成交" : "恭喜" + buyer + "\n購得此地！");
    draw_bidders(frame, world, winner, -1, [&](std::size_t k) {
        return k == winner ? std::pair{2, int(elapsed / 100) % 5} : std::pair{0, 0}; // winner celebrates
    });
    hit(host, ids::auction_skip, {0, 0, 640, 480}, no_sound, [this] { sold_.reset(); });
}

} // namespace rich4::native::screens
