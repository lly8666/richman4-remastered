// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/auction.hpp"
#include "rich4/mod_rules.hpp"
#include "rich4/calendar.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace rich4 {
namespace {
constexpr std::array<std::int32_t, 7> increments{0, 100, 500, 1000, 5000, 10000, 0};

std::int32_t multiply32(std::int32_t a, std::int32_t b) noexcept {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
}
std::string_view original_name(const std::string &name) noexcept {
    return std::string_view(name).substr(0, name.find('\0'));
}

struct Property {
    std::uint16_t base_price;
    std::uint8_t level;
    std::uint8_t *owner;
    std::uint32_t *expiry;
    std::string_view street;
    bool facility;
};
Property property(World &world, std::uint16_t code) {
    if (code > 2000 && code < 4000) {
        auto &land = world.lands.at(static_cast<std::size_t>(code - 2000));
        return {land.land_price,
                land.economy.level,
                &land.economy.owner_code,
                &land.expired_date,
                original_name(land.economy.street_name),
                false};
    }
    if (code > 4000 && code < 6000) {
        auto &facility = world.facilities.at(static_cast<std::size_t>(code - 4000));
        return {facility.land_price, facility.level, &facility.owner_code, &facility.expired_date, {}, true};
    }
    throw std::invalid_argument("auction requires an original land or facility code");
}

// x87 multiplies an int32 by two stored binary32 values before truncating.
// Their exact product needs at most 79 bits. Round its significand to x87's
// 64 bits before the integer conversion; this also works on ARM, where long
// double has only 53 significand bits. No native floating environment required.
std::int32_t original_product(std::int32_t integer, float a, float b) {
    using Wide = __uint128_t;
    const auto unpack = [](float number) {
        const auto bits = std::bit_cast<std::uint32_t>(number);
        if ((bits & 0x80000000U) || (bits & 0x7f800000U) == 0 || (bits & 0x7f800000U) == 0x7f800000U)
            throw std::invalid_argument("invalid auction valuation coefficient");
        return std::pair{(bits & 0x7fffffU) | 0x800000U, static_cast<int>((bits >> 23U) & 255U) - 127 - 23};
    };
    const auto [ma, ea] = unpack(a);
    const auto [mb, eb] = unpack(b);
    const auto magnitude = integer < 0 ? -static_cast<std::int64_t>(integer) : integer;
    Wide product = static_cast<Wide>(magnitude) * ma * mb;
    int exponent = ea + eb;
    unsigned bits = 0;
    for (auto value = product; value; value >>= 1U)
        ++bits;
    if (bits > 64) {
        const auto shift = bits - 64;
        const Wide remainder = product & ((Wide{1} << shift) - 1);
        const Wide half = Wide{1} << (shift - 1);
        product >>= shift;
        if (remainder > half || (remainder == half && (product & 1)))
            ++product;
        exponent += static_cast<int>(shift);
    }
    if (exponent < 0)
        product >>= static_cast<unsigned>(-exponent);
    else
        product <<= static_cast<unsigned>(exponent);
    const Wide limit = integer < 0 ? Wide{2147483648U} : Wide{2147483647U};
    // Original masked x87 invalid conversion stores its integer-indefinite value.
    if (product > limit)
        return std::numeric_limits<std::int32_t>::min();
    const auto value = static_cast<std::int64_t>(product);
    return static_cast<std::int32_t>(integer < 0 ? -value : value);
}

std::int32_t ai_maximum(const World &world, Random &random, const Property &target, std::size_t player,
                        std::int32_t initial_price) {
    // Both intermediate ratios are explicitly stored as binary32 by 0x439f0d.
    const float enthusiasm = static_cast<float>((static_cast<double>(random.next()) / 32767.0) * 0.3 + 0.5);
    std::size_t unowned = 0, same_street = 0;
    const auto count = target.facility ? world.facilities.size() - 1 : world.lands.size() - 1;
    if (target.facility) {
        for (std::size_t i = 1; i < world.facilities.size(); ++i)
            if (!world.facilities[i].owner_code)
                ++unowned;
    } else {
        for (std::size_t i = 1; i < world.lands.size(); ++i) {
            const auto &land = world.lands[i].economy;
            if (!land.owner_code)
                ++unowned;
            if (land.owner_code == player + 1 && original_name(land.street_name) == target.street)
                ++same_street;
        }
    }
    const auto scarcity =
        static_cast<float>(6.0 - static_cast<double>(unowned) / static_cast<double>(count) * 4.0);
    auto coefficient = static_cast<std::int32_t>(target.level / 2 + 1 + same_street);
    coefficient = multiply32(multiply32(coefficient, initial_price), world.header.price_index);
    const auto desired = original_product(coefficient, scarcity, enthusiasm);
    const auto indexed_base = static_cast<float>(multiply32(target.base_price, world.header.price_index));
    const float ceiling_ratio = 3.0F + static_cast<float>(random.next()) / 65536.0F;
    // These are two binary32 significands: the product is exact in binary64.
    const double ceiling_product = static_cast<double>(indexed_base) * ceiling_ratio;
    const auto ceiling =
        ceiling_product < std::numeric_limits<std::int32_t>::min() || ceiling_product >= 2147483648.0
            ? std::numeric_limits<std::int32_t>::min()
            : static_cast<std::int32_t>(ceiling_product);
    return mod::display_i32(std::min<std::int64_t>({desired, ceiling, world.players.at(player).state.cash}));
}
bool excluded_by_status(const PlayerStatus &value) noexcept {
    return value.days_in_hotel || value.days_disappearing || value.days_in_prison || value.days_in_hospital ||
           value.days_sleeping || value.days_sleep_walking;
}
} // namespace

AuctionSession::AuctionSession(World &world, Random &random, std::uint16_t code, std::int32_t seller,
                               AuctionOptions options)
    : world_(&world), property_(code), seller_(seller), options_(options) {
    if (world.players.size() > bidders_.size() || seller < -1 ||
        (seller >= 0 && static_cast<std::size_t>(seller) >= world.players.size()))
        throw std::invalid_argument("invalid auction participants");
    if (!options_.lease_interval) {
        constexpr std::array<std::uint32_t, 6> intervals{0, 0x20000, 0x10000, 0x600, 0x300, 0x100};
        if (world.lease_mode >= intervals.size())
            throw std::invalid_argument("invalid saved auction lease mode");
        options_.lease_interval = intervals[world.lease_mode];
    }
    const auto target = property(world, code);
    initial_price_ = multiply32(
        static_cast<std::int32_t>(static_cast<std::uint32_t>(target.base_price) * (target.level + 2U) / 2U),
        world.header.price_index);
    if (initial_price_ < 0)
        throw std::out_of_range("auction starting price overflow");
    price_ = initial_price_;
    result_.property = code;
    // Original records are packed by active player before excluded records are
    // erased, so keep holes for active-but-ineligible players in turn order.
    std::size_t record = 0;
    for (std::size_t slot = 0; slot < world.players.size(); ++slot) {
        const auto &entry = world.players[slot];
        if (!entry.state.controller_flags)
            continue;
        auto &bidder = bidders_.at(record++);
        bidder.player = slot;
        if (entry.state.cash <= initial_price_ || excluded_by_status(entry.status) ||
            static_cast<std::int32_t>(slot) == seller)
            continue;
        bidder.present = true;
        bidder.human = entry.state.controller_flags == 1;
        if (entry.state.controller_flags & 6U)
            bidder.limit = ai_maximum(world, random, target, slot, initial_price_);
        ++original_eligible_count_;
        current_ = record - 1; // 0x43af13 retains the last eligible record.
    }
    original_records_ = record;
    if (!original_eligible_count_)
        settle();
}

std::optional<std::int32_t> AuctionSession::ai_limit(std::size_t player) const noexcept {
    for (const auto &bidder : bidders_)
        if (bidder.present && bidder.player == player && !bidder.human)
            return bidder.limit;
    return std::nullopt;
}

bool AuctionSession::legal(AuctionAction action) const {
    if (finished_ || !bidders_[current_].present)
        return false;
    const auto code = static_cast<unsigned>(action);
    if (code > 6)
        return false;
    if (code == 0 || code == 6)
        return true;
    const auto offered = static_cast<std::int64_t>(price_) + increments[code];
    return offered <= INT32_MAX && offered <= world_->players.at(bidders_[current_].player).state.cash;
}

std::optional<AuctionDecision> AuctionSession::decision() const {
    if (finished_ || !bidders_[current_].human)
        return std::nullopt;
    AuctionDecision value{bidders_[current_].player, price_, {}};
    for (unsigned code = 0; code <= 6; ++code) {
        const auto action = static_cast<AuctionAction>(code);
        if (legal(action))
            value.actions.push_back(action);
    }
    for (std::size_t record = 0; record < original_records_; ++record)
        if (!bidders_[record].present)
            value.inactive |= static_cast<std::uint8_t>(1U << bidders_[record].player);
    return value;
}

AuctionAction AuctionSession::ai_action() const {
    const auto &bidder = bidders_[current_];
    unsigned choice = 0;
    if (price_ > world_->players.at(bidder.player).state.cash)
        choice = 6;
    else {
        for (unsigned code = 1; code <= 5; ++code)
            if (static_cast<std::int64_t>(price_) + increments[code] <= bidder.limit)
                choice = code;
        if (choice && last_bidder_) {
            const auto last_cash = world_->players.at(bidders_[*last_bidder_].player).state.cash;
            const auto target = static_cast<std::int64_t>(last_cash) + 500;
            if (static_cast<std::int64_t>(price_) + increments[choice] > target) {
                const auto difference = target - price_;
                if (difference <= 100)
                    choice = 1;
                else if (difference <= 500)
                    choice = 2;
                else if (difference <= 1000)
                    choice = 3;
                else if (difference <= 5000)
                    choice = 4;
            }
        }
    }
    if (original_eligible_count_ == 1 && choice)
        choice = 1;
    return static_cast<AuctionAction>(choice);
}

bool AuctionSession::tick() {
    if (finished_ || bidders_[current_].human)
        return false;
    return bid(ai_action());
}

bool AuctionSession::bid(AuctionAction action) {
    if (!legal(action))
        return false;
    // Settlement can reject a 64-bit overflow; do not leave a half-applied
    // winning bid in the resumable auction when its financial preflight fails.
    const auto old_bidders = bidders_;
    const auto old_current = current_;
    const auto old_last_bidder = last_bidder_;
    const auto old_price = price_;
    try {
        auto &bidder = bidders_[current_];
        if (action == AuctionAction::pass)
            bidder.passed = true;
        else if (action == AuctionAction::withdraw)
            bidder.present = false;
        else {
            price_ += increments[static_cast<unsigned>(action)];
            last_bidder_ = current_;
            for (auto &candidate : bidders_)
                candidate.passed = false;
        }
        after_action();
    } catch (...) {
        bidders_ = old_bidders;
        current_ = old_current;
        last_bidder_ = old_last_bidder;
        price_ = old_price;
        throw;
    }
    return true;
}

void AuctionSession::after_action() {
    std::size_t present = 0, passed = 0;
    for (const auto &bidder : bidders_) {
        if (!bidder.present)
            continue;
        ++present;
        if (bidder.passed)
            ++passed;
    }
    if (!present || present == passed || ((present == 1 || present - passed == 1) && last_bidder_)) {
        settle();
        return;
    }
    do {
        current_ = (current_ + 1) % bidders_.size();
    } while (!bidders_[current_].present);
}

void AuctionSession::settle() {
    if (!last_bidder_) {
        finished_ = true;
        return;
    }
    const auto winner = bidders_[*last_bidder_].player;
    auto &buyer = world_->players.at(winner);
    auto *seller = seller_ == -1 ? nullptr : &world_->players.at(static_cast<std::size_t>(seller_));

    // Preflight both sides before changing property ownership or accounts.
    // Auction prices remain original int32, but MOD balances are signed int64.
    const auto buyer_cash = mod::money_sub(buyer.state.cash, price_);
    const auto public_fund = seller ? world_->public_fund : mod::money_add(world_->public_fund, price_);
    const auto seller_bank = seller ? mod::money_add(seller->state.bank_balance, price_) : std::int64_t{0};
    auto target = property(*world_, property_);
    if (*target.owner != winner + 1) {
        if (!*target.owner && *options_.lease_interval) {
            const auto date = world_->header.date;
            *target.expiry = calendar::add_packed_interval(calendar::pack({date.year, date.month, date.day}),
                                                           *options_.lease_interval);
        }
        *target.owner = static_cast<std::uint8_t>(winner + 1);
    }
    buyer.state.cash = buyer_cash;
    buyer.status.cumulative_spending += static_cast<std::uint32_t>(price_);
    if (!seller)
        world_->public_fund = public_fund;
    else {
        seller->state.bank_balance = seller_bank;
        seller->status.cumulative_income += static_cast<std::uint32_t>(price_);
    }
    result_.winner = winner;
    result_.price = price_;
    finished_ = true;
}

} // namespace rich4
