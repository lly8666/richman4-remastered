// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "rich4/random.hpp"
#include "rich4/world.hpp"
#include <functional>
#include <optional>
#include <span>
#include <string_view>

namespace rich4::services {
inline constexpr CardPrices card_prices{200, 200, 35, 25, 20, 20, 15, 20, 160, 180, 60, 15, 25, 20, 100,
                                        25,  20,  20, 40, 25, 25, 10, 20, 50,  30,  35, 35, 35, 40, 70};
inline constexpr ToolPrices tool_prices{15, 30, 25, 25, 80, 150, 100, 30, 30, 40, 95, 150, 250};
inline constexpr std::array<std::string_view, 30> card_names{
    "均富卡", "均貧卡", "購地卡", "換地卡", "換屋卡", "轉向卡", "改建卡", "拍賣卡", "天使卡", "惡魔卡",
    "怪獸卡", "拆除卡", "搶奪卡", "停留卡", "冬眠卡", "夢遊卡", "陷害卡", "復仇卡", "嫁禍卡", "免費卡",
    "免罪卡", "送神符", "請神符", "紅卡",   "黑卡",   "查稅卡", "漲價卡", "查封卡", "同盟卡", "烏龜卡"};
inline constexpr std::array<std::string_view, 13> tool_names{
    "機器娃娃", "路障",     "地雷",   "定時炸彈", "機車",   "汽車",    "飛彈",
    "遙控骰子", "機器工人", "時光機", "傳送機",   "工程車", "核子飛彈"};
inline constexpr std::array<std::string_view, 12> magic_categories{
    "最富有的人", "土地最多的人", "房屋最多的人", "現金最多的人", "存款最多的人", "點數最多的人",
    "步行的人",   "騎機車的人",   "開汽車的人",   "神明附身的人", "男性",         "女性"};
inline constexpr std::array<std::string_view, 12> magic_effects{
    "出售全部卡片", "連抽三次命運", "入獄三天",     "停留一天",     "現金全部存入銀行", "腳下房屋升級",
    "獲得一張卡片", "掉轉方向",     "出售全部道具", "腳下房屋降級", "住院三天",         "拍賣腳下土地"};

// Stock-weighted selection, one original random call only when stock is nonzero.
std::uint8_t receive_random_card(World &, Random &, std::size_t player);
std::uint8_t receive_random_tool(World &, Random &, std::size_t player);
struct Shop {
    std::vector<std::uint8_t> cards, tools;
    std::int64_t revenue = 0;
    std::uint8_t gift_card = 0, gift_tool = 0;
    bool closed = false;
};
// Human: construct one visit's finite shelf. Computer: run the original trading
// policy, no cosmetic/UI random calls. Owner gifts precede both paths.
Shop open_shop(World &, Random &, std::size_t player, std::uint16_t property = 0);
bool buy_card(World &, Shop &, std::size_t player, std::uint8_t card);
bool buy_tool(World &, Shop &, std::size_t player, std::uint8_t tool);
bool sell_card(World &, Shop &, std::size_t player, std::uint8_t card);
bool sell_tool(World &, Shop &, std::size_t player, std::uint8_t tool, unsigned quantity = 1);
void close_shop(World &, Shop &, std::uint16_t property = 0);

std::vector<std::size_t> magic_targets(const World &, unsigned category);
struct MagicChoice {
    unsigned category, effect;
};
std::optional<MagicChoice> choose_magic_ai(const World &, Random &, std::size_t actor);
struct MagicCallbacks {
    std::function<void(std::size_t)> fortune;
    std::function<int(std::size_t)> defense; // redirected player or -1 canceled
    std::function<void(std::size_t, std::size_t, std::int32_t)> hostility;
    std::function<void(std::size_t, unsigned)> prison, hospital;
    std::function<void(std::uint16_t)> upgrade, downgrade;
    std::function<void(std::uint16_t, std::size_t)> auction;
    std::function<void(std::size_t)> turn_around;
};
// Missing required callbacks return false before any mutation. A caller may
// suspend/replay callbacks using a World + RNG snapshot for nested UI decisions.
bool apply_magic(World &, Random &, std::size_t actor, std::span<const std::size_t> targets, unsigned effect,
                 const MagicCallbacks &);
} // namespace rich4::services
