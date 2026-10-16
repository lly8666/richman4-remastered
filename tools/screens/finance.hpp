// SPDX-License-Identifier: GPL-3.0-or-later
// Original 大富翁4 finance screens (docs/ui/finance-menus.md §3, §4, §7, §8, §9):
// the shared number pad (Panel 21/22), ATM (Panel 24), loan desk and director's office
// (Panel 23), primary-share subscription, the stock market (Panel 75), player trading
// (Panel 73), the lottery bar (Panel 12) and the auction room (Panel 26).
//
// The screens only draw, register hit areas and call back into GameSession; every rule
// stays in the core. Button ids are stable because the replay harness clicks by id.
#pragma once

#include "draw.hpp"
#include "rich4/game.hpp"
#include "rich4/trading.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace rich4 {
class World;
}

namespace rich4::native::screens {

// Button ids (the replay harness depends on them).
namespace ids {
inline constexpr int pad_body = 900, pad_cancel = 901, pad_max = 902, pad_enter = 903, pad_clear = 904,
                     pad_zero = 905, pad_back = 906, pad_gauge = 916;
int pad_digit(int digit); // 905 for 0, 907..915 for 7 8 9 4 5 6 1 2 3
inline constexpr int atm_withdraw = 920, atm_deposit = 921, atm_exit = 922, atm_gauge = 923, atm_max = 936,
                     atm_enter = 937;
int atm_digit(int digit);
inline constexpr int atm_clear = 933, atm_back = 935, atm_keypad = 938;
inline constexpr int desk_exit = 940, desk_loan = 941, desk_repay = 942, desk_window = 943, director_finance = 944,
                     director_return = 945, director_exit = 946, bank_skip = 947;
inline constexpr int auction_skip = 1650; // tap ending the auction sold sequence
inline constexpr int stock_page = 434, stock_buy = 430, stock_sell = 431, stock_info = 435, stock_exit = 60,
                     stock_info_close = 436, stock_row = 400;
} // namespace ids

// Panel 21/22 shared numeric pad (fcn_00453544). Modal: while active the caller draws
// nothing clickable underneath.
class NumberPad {
  public:
    using Done = std::function<void(std::int64_t)>;
    void open(std::int64_t maximum, Done done);
    // Touch screens: draw the pad's original artwork at an integer scale (2 on phones) so each
    // key is a finger-sized target. Desktop keeps the original 1x geometry.
    void set_scale(int scale) {
        scale_ = std::clamp(scale, 1, 2);
    }
    [[nodiscard]] int scale() const noexcept {
        return scale_;
    }
    // Top of a Panel 73:5 prompt box (88 px) placed directly above the pad.
    [[nodiscard]] int prompt_top() const noexcept {
        return scale_ > 1 ? 2 : 42;
    }
    void close() {
        active_ = false;
    }
    [[nodiscard]] bool active() const noexcept {
        return active_;
    }
    [[nodiscard]] std::int64_t value() const;
    [[nodiscard]] std::int64_t maximum() const noexcept {
        return maximum_;
    }
    [[nodiscard]] const std::string &digits() const noexcept {
        return text_;
    }
    void draw(Host &host);
    void cancel(Host &host); // right-click / Back: returns 0, as the original
    // Keyboard: '0'..'9', 'M' (MAX), 'C' (clear), '\b', '\r' (Enter), 'H' (gauge middle).
    void key(Host &host, char key);
    // Replay helper: the id of the next key that moves the display towards target (Enter when done).
    [[nodiscard]] int next_key_towards(std::int64_t target) const;

  private:
    void press(int key);
    void set_from_gauge(int x);
    bool active_ = false, dragging_ = false;
    int x_ = 256, y_ = 144, scale_ = 1;
    std::int64_t maximum_ = 0;
    std::string text_ = "0";
    Done done_;
};

// What the bank screens need from the application: answer the pending decision with the
// option whose value is `value` (see BankScreen) and an amount.
struct BankActions {
    std::function<bool(std::int32_t value, std::int64_t amount)> respond;
};

// DecisionKind::bank: ATM (Panel 24) first, then on a stop the loan desk (Panel 23) with the
// director's office for the owner of this bank. Values: 1 存款, 2 提款, 3 贷款, 4 还贷,
// 5 融资, 6 归还融资, 0 离开 (GameSession::respond).
class BankScreen {
  public:
    void reset() {
        *this = {};
    }
    void draw(Host &host, const World &world, const GameDecision &decision, NumberPad &pad,
              const BankActions &actions);
    [[nodiscard]] bool at_atm() const noexcept {
        return stage_ == Stage::atm;
    }
    [[nodiscard]] bool at_desk() const noexcept {
        return stage_ == Stage::desk;
    }
    [[nodiscard]] bool at_director() const noexcept {
        return stage_ == Stage::director;
    }
    [[nodiscard]] int atm_mode() const noexcept {
        return mode_;
    }
    [[nodiscard]] const std::string &atm_digits() const noexcept {
        return atm_text_;
    }
    void cancel(Host &host, const GameDecision &decision, const BankActions &actions);

  private:
    enum class Stage { atm, desk, director };
    void draw_atm(Host &host, const World &world, const GameDecision &decision, NumberPad &pad,
                  const BankActions &actions);
    void draw_desk(Host &host, const World &world, const GameDecision &decision, NumberPad &pad,
                   const BankActions &actions);
    void draw_director(Host &host, const World &world, const GameDecision &decision, NumberPad &pad,
                       const BankActions &actions);
    void draw_side_panel(Host &host, const World &world, std::size_t slot);
    void atm_key(Host &host, int index, const World &world, const GameDecision &decision,
                 const BankActions &actions);
    void leave_atm(Host &host, const GameDecision &decision, const BankActions &actions);
    void say(Host &host, std::vector<std::string> lines);
    std::int64_t atm_maximum(const World &world, std::size_t slot) const;

    Stage stage_ = Stage::atm;
    bool started_ = false, atm_dragging_ = false, side_panel_ = false;
    int mode_ = 0; // 0 提款, 1 存款
    std::string atm_text_ = "0";
    std::vector<std::string> lines_;
    std::uint64_t line_started_ = 0, side_started_ = 0, popup_until_ = 0;
    std::string popup_;
    std::uint64_t said_ = 0, voiced_said_ = 0; // AUDIO: say() serial, and the one being voiced
    std::size_t voiced_index_ = 0;
};

// DecisionKind::primary_stock: Panel 73:5 prompt box above the number pad.
void draw_primary_stock(Host &host, const World &world, const GameDecision &decision, NumberPad &pad,
                        const BankActions &actions);

// Panel 75 stock market (rich4_ui_stock.asm).
struct StockActions {
    std::function<bool(std::size_t stock, std::int32_t quantity)> buy, sell;
    std::function<void()> close;
};
class StockMarket {
  public:
    void reset() {
        *this = {};
    }
    void draw(Host &host, const World &world, std::size_t player, NumberPad &pad, const StockActions &actions);
    void cancel(Host &host, const StockActions &actions); // EXIT / right-click / Back
    // 紅卡 / 黑卡 target (_rich4_ui_stock_entry modes 1/2): the price page with trading disabled. A
    // row's option (subject = stock) is answered through `respond(option index)` with the first tap
    // selecting and the second committing; EXIT answers the 取消 option. `decision` may be null to
    // draw the read-only page the original holds for 1000 ms after the card took effect.
    void draw_card_target(Host &host, const World &world, std::size_t player, const GameDecision *decision,
                          int option_base, const std::function<void(std::size_t)> &respond);
    std::optional<std::size_t> selected;
    int page = 0;
    bool info_open = false;

  private:
    void draw_prices(Host &host, const World &world, std::size_t player);
    void draw_holdings(Host &host, const World &world);
    void draw_info(Host &host, const World &world, std::size_t stock);
    std::uint64_t popup_until_ = 0;
    std::string popup_;
};

bool market_closed(const World &world);

namespace ids {
inline constexpr int trade_sale = 480, trade_exit = 481, trade_category = 484, trade_item = 460, trade_offer = 600,
                     trade_act = 456, trade_detail_exit = 457, trade_picker_close = 458, trade_tab = 490,
                     trade_up = 495, trade_down = 496, trade_outside = 459;
} // namespace ids

// Panel 73 player trading board (rich4_ui_sale.asm).
struct TradingActions {
    std::function<bool(const trading::Listing &)> post;
    std::function<bool(std::size_t index)> cancel; // own offer
    std::function<bool(std::size_t seller, std::size_t index)> buy;
    std::function<void()> close;
};
class TradingBoard {
  public:
    enum class View { board, menu, stocks, properties, items, detail };
    void reset() {
        *this = {};
    }
    void draw(Host &host, const World &world, std::size_t actor, NumberPad &pad, const TradingActions &actions);
    void cancel(Host &host, const TradingActions &actions);
    [[nodiscard]] View view() const noexcept {
        return view_;
    }
    // Replay helper: what the number pad currently asks for (quantity 1, or the market price).
    [[nodiscard]] std::int32_t suggestion() const noexcept {
        return suggestion_;
    }

  private:
    void draw_board(Host &host, const World &world, std::size_t actor, const TradingActions &actions);
    void draw_detail(Host &host, const World &world, std::size_t actor, const TradingActions &actions);
    void draw_picker(Host &host, const World &world, std::size_t actor, NumberPad &pad,
                     const TradingActions &actions);
    void ask_price(const World &world, std::size_t actor, NumberPad &pad, const TradingActions &actions,
                   trading::Item item, std::uint16_t quantity);
    void notice(std::string message);
    View view_ = View::board;
    int category_ = 0, tab_ = 0;
    std::size_t scroll_ = 0;
    std::optional<trading::Quote> detail_;
    std::string prompt_, notice_;
    std::uint64_t notice_until_ = 0, now_ = 0;
    std::int32_t suggestion_ = 1;
};

// DecisionKind::lottery: Panel 12 bar with the 36-number board. Options are the free
// numbers plus 离开 (value 0); hit ids stay 210 + option index.
// After a pick (or leaving with a ticket still affordable) the original keeps the bar up: ring on
// the number, the cat girl's wink (12:2) and 「拜拜！祝您中獎！」 (#0014) / 「下次再來吧！」 (#0016),
// and only then closes. `farewell` holds that state; `goodbye(picked)` starts the sound/voice and
// returns the voice length. Without them (or with host.instant) the answer is sent at once.
struct LotteryFarewell {
    std::optional<std::size_t> option;
    int number = -1; // picked cell 0..35, -1 when leaving
    std::uint64_t started = 0;
    std::uint32_t voice_ms = 0;
};
void draw_lottery(Host &host, const World &world, const GameDecision &decision,
                  const std::function<void(std::size_t)> &respond, LotteryFarewell *farewell = nullptr,
                  const std::function<std::uint32_t(bool picked)> &goodbye = {});
// DecisionKind::auction: Panel 26 room. Options are the legal AuctionAction values 0..6;
// all seven original buttons are shown and illegal ones are ignored, as in the original.
// Bidders show the original poses: 27+3c (bidding; frames 1..4,0 with the 26:1 starburst for about
// 1 s after the human's bid, then the answer is sent), 28+3c for those who cannot bid
// (GameDecision::inactive_players) and 29+3c for the buyer in the sold sequence.
class AuctionRoom {
  public:
    void draw(Host &host, const World &world, const GameDecision &decision,
              const std::function<void(std::size_t)> &respond);
    // Sold sequence (gavel 26:21, "%d元成交", buyer's 29+3c celebration, congratulations; sound 63),
    // started from the core's 拍卖成交 notice. Presentation only; a tap (ids::auction_skip) ends it.
    void start_sold(std::size_t winner, std::int32_t price, std::uint16_t property);
    [[nodiscard]] bool sold_active() const noexcept { return sold_.has_value(); }
    void draw_sold(Host &host, const World &world);
    // Gallery: freeze a bid animation started at `started` ms (action 0..6, new price).
    void preview_bid(const GameDecision &decision, std::size_t index, int action, std::int32_t price,
                     std::uint64_t started) {
        bid_ = Bid{std::to_string(decision.land_id) + ':' + std::to_string(decision.player) + ':' +
                       std::to_string(decision.amount),
                   index, action, price, started};
    }
    void reset() {
        bid_.reset();
        sold_.reset();
    }

  private:
    struct Bid {
        std::string key; // land:bidder:price of the decision it answers
        std::size_t index = 0;
        int action = 0;
        std::int32_t price = 0;
        std::uint64_t started = 0; // 0 = stamped on the next frame
    };
    struct Sold {
        std::size_t winner = 0;
        std::int32_t price = 0;
        std::uint16_t property = 0;
        std::uint64_t started = 0;
        bool sounded = false;
    };
    std::optional<Bid> bid_;
    std::optional<Sold> sold_;
};

} // namespace rich4::native::screens
