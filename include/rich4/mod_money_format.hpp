// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Presentation-only human-readable money. Balances, trading and save values stay
// exactly signed64; callers needing exact input use grouped_digits().
#include <cstdint>
#include <string>
#include <string_view>

namespace rich4::mod {
inline std::string grouped_digits(std::int64_t amount) {
    std::string result = std::to_string(amount);
    const auto from = amount < 0 ? 1U : 0U;
    for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(result.size()) - 3;
         i > static_cast<std::ptrdiff_t>(from); i -= 3)
        result.insert(static_cast<std::size_t>(i), ",");
    return result;
}

// One or two fractional digits in Chinese financial units. The approximate
// glyph is shown ONLY if sub-display-unit digits would otherwise be hidden.
inline std::string compact_money(std::int64_t amount, bool dollar = true) {
    const std::uint64_t magnitude = amount < 0 ? std::uint64_t(-(amount + 1)) + 1
                                              : std::uint64_t(amount);
    constexpr std::uint64_t wan = 10'000, yi = 100'000'000,
                            wanyi = 1'000'000'000'000ULL, jing = 10'000'000'000'000'000ULL;
    std::uint64_t unit = 1;
    std::string_view suffix;
    if (magnitude >= jing) { unit = jing; suffix = "京"; }
    else if (magnitude >= wanyi) { unit = wanyi; suffix = "万亿"; }
    else if (magnitude >= yi) { unit = yi; suffix = "亿"; }
    else if (magnitude >= wan * 1000) { unit = wan; suffix = "万"; }
    if (unit == 1)
        return (dollar ? "$" : "") + grouped_digits(amount);
    const bool approximate = (magnitude % unit) % (unit / 100) != 0;
    const auto main = magnitude / unit;
    const auto fraction = static_cast<unsigned>((magnitude % unit) / (unit / 100));
    std::string result = (dollar ? "$" : "") + std::string(approximate ? "≈" : "") +
                         std::string(amount < 0 ? "-" : "") + std::to_string(main);
    if (fraction) {
        result += '.';
        result += char('0' + fraction / 10);
        if (fraction % 10)
            result += char('0' + fraction % 10);
    }
    return result + std::string(suffix);
}
} // namespace rich4::mod
