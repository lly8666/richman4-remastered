// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <bit>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace rich4::numeric {

static_assert(sizeof(float) == 4 && sizeof(double) == 8 && std::numeric_limits<float>::is_iec559 &&
              std::numeric_limits<double>::is_iec559);

// Finite x87 arithmetic, rounded to its 64-bit significand after every
// operation. ARM long double is only binary64, so it cannot preserve the
// original halfway cases. All storage and rounding here use fixed-width
// integers; there is no host floating-point control word or assembler.
struct Extended {
    using Wide = __uint128_t;
    std::uint64_t significand{};
    int exponent{}; // value = signed significand * 2^exponent
    bool negative{};

    static Extended normalized(Wide value, int scale, bool sign = false) {
        if (value == 0) {
            return {};
        }
        unsigned length = 0;
        for (auto copy = value; copy; copy >>= 1) {
            ++length;
        }
        if (length > 64) {
            const auto shift = length - 64;
            const auto remainder = value & ((Wide{1} << shift) - 1);
            const auto halfway = Wide{1} << (shift - 1);
            value >>= shift;
            if (remainder > halfway || (remainder == halfway && (value & 1))) {
                ++value;
            }
            scale += static_cast<int>(shift);
            if (value == (Wide{1} << 64)) {
                value >>= 1;
                ++scale;
            }
        } else {
            value <<= 64 - length;
            scale -= static_cast<int>(64 - length);
        }
        return {static_cast<std::uint64_t>(value), scale, sign};
    }

    static Extended from(double value) {
        const auto raw = std::bit_cast<std::uint64_t>(value);
        const auto exponent_bits = (raw >> 52) & 2047U;
        if (exponent_bits == 2047) {
            throw std::invalid_argument("nonfinite legacy floating arithmetic");
        }
        const auto mantissa = (raw & 0xfffffffffffffULL) | (exponent_bits == 0 ? 0 : 0x10000000000000ULL);
        return normalized(mantissa, exponent_bits == 0 ? -1074 : int(exponent_bits) - 1075, (raw >> 63) != 0);
    }

    Extended operator-() const {
        return {significand, exponent, !negative};
    }

    friend Extended operator+(Extended a, Extended b) {
        if (!a.significand) {
            return b;
        }
        if (!b.significand) {
            return a;
        }
        if (a.exponent < b.exponent || (a.exponent == b.exponent && a.significand < b.significand)) {
            std::swap(a, b);
        }
        const auto difference = static_cast<unsigned>(a.exponent - b.exponent);
        const Wide large = Wide{a.significand} << 3;
        Wide small;
        if (difference <= 3) {
            small = Wide{b.significand} << (3 - difference);
        } else if (difference >= 67) {
            small = 1; // Sticky bit: the discarded nonzero operand is retained.
        } else {
            const auto shift = difference - 3;
            small = b.significand >> shift;
            small |= (Wide{b.significand} & ((Wide{1} << shift) - 1)) != 0;
        }
        return normalized(a.negative == b.negative ? large + small : large - small, a.exponent - 3,
                          a.negative);
    }
    friend Extended operator-(Extended a, Extended b) {
        return a + -b;
    }
    friend Extended operator*(Extended a, Extended b) {
        return normalized(Wide{a.significand} * b.significand, a.exponent + b.exponent,
                          a.negative != b.negative);
    }
    friend Extended operator/(Extended a, Extended b) {
        if (!b.significand) {
            throw std::invalid_argument("legacy floating division by zero");
        }
        if (!a.significand) {
            return {};
        }
        const unsigned shift = a.significand >= b.significand ? 63 : 64;
        const Wide dividend = Wide{a.significand} << shift;
        Wide quotient = dividend / b.significand;
        const Wide doubled_remainder = (dividend % b.significand) * 2;
        if (doubled_remainder > b.significand || (doubled_remainder == b.significand && (quotient & 1))) {
            ++quotient;
        }
        return normalized(quotient, a.exponent - b.exponent - static_cast<int>(shift),
                          a.negative != b.negative);
    }

    [[nodiscard]] float single() const {
        if (!significand) {
            return 0;
        }
        int power = exponent + 63;
        if (power < -126 || power > 127) {
            throw std::out_of_range("legacy arithmetic outside normal binary32 range");
        }
        auto rounded = significand >> 40;
        const auto discarded = significand & ((std::uint64_t{1} << 40) - 1);
        const auto halfway = std::uint64_t{1} << 39;
        if (discarded > halfway || (discarded == halfway && (rounded & 1))) {
            ++rounded;
            if (rounded == 0x1000000) {
                rounded >>= 1;
                ++power;
            }
        }
        if (power > 127) {
            throw std::out_of_range("legacy binary32 store overflow");
        }
        return std::bit_cast<float>((negative ? 0x80000000U : 0) |
                                    (static_cast<std::uint32_t>(power + 127) << 23) |
                                    static_cast<std::uint32_t>(rounded & 0x7fffffU));
    }

    [[nodiscard]] std::int32_t integer() const {
        if (!significand || exponent <= -64) {
            return 0;
        }
        if (exponent >= 0) {
            return INT32_MIN;
        }
        const auto magnitude = significand >> -exponent;
        if (magnitude > (negative ? 0x80000000U : 0x7fffffffU)) {
            return INT32_MIN;
        }
        return static_cast<std::int32_t>(negative ? -static_cast<std::int64_t>(magnitude)
                                                  : static_cast<std::int64_t>(magnitude));
    }

    // MOD peer-to-peer stock trades need x87 truncation at signed 64-bit.
    [[nodiscard]] std::int64_t integer64() const {
        if (!significand || exponent <= -64)
            return 0;
        if (exponent >= 0) {
            if (negative && exponent == 0 && significand == (std::uint64_t{1} << 63))
                return INT64_MIN;
            throw std::overflow_error("MOD legacy integer exceeds signed 64-bit range");
        }
        const auto magnitude = significand >> -exponent;
        if (magnitude > (negative ? (std::uint64_t{1} << 63) : std::uint64_t{INT64_MAX}))
            throw std::overflow_error("MOD legacy integer exceeds signed 64-bit range");
        if (negative && magnitude == (std::uint64_t{1} << 63))
            return INT64_MIN;
        const auto positive = static_cast<std::int64_t>(magnitude);
        return negative ? -positive : positive;
    }

    // Explicit FSTP qword rounding. Some original game call sites subsequently
    // reinterpret its low 32 bits as an integer; callers express that separately.
    [[nodiscard]] double double_precision() const {
        if (!significand) {
            return 0;
        }
        int power = exponent + 63;
        if (power < -1022 || power > 1023) {
            throw std::out_of_range("legacy arithmetic outside normal binary64 range");
        }
        auto rounded = significand >> 11;
        const auto discarded = significand & 2047U;
        if (discarded > 1024 || (discarded == 1024 && (rounded & 1))) {
            ++rounded;
            if (rounded == 0x20000000000000ULL) {
                rounded >>= 1;
                ++power;
            }
        }
        if (power > 1023) {
            throw std::out_of_range("legacy binary64 store overflow");
        }
        return std::bit_cast<double>((negative ? 0x8000000000000000ULL : 0) |
                                     (static_cast<std::uint64_t>(power + 1023) << 52) |
                                     (rounded & 0xfffffffffffffULL));
    }
};

} // namespace rich4::numeric
