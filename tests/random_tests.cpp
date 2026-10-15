// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/random.hpp"
#include <array>
#include <charconv>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
std::array<std::uint32_t, 4> parse_row(std::string_view line) {
    std::array<std::uint32_t, 4> values{};
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto [end, error] = std::from_chars(line.data(), line.data() + line.size(), values[i]);
        if (error != std::errc{})
            throw std::runtime_error("invalid RNG golden number");
        line.remove_prefix(static_cast<std::size_t>(end - line.data()));
        if (i + 1 < values.size()) {
            if (line.empty() || line.front() != ',')
                throw std::runtime_error("invalid RNG golden delimiter");
            line.remove_prefix(1);
        }
    }
    if (line != "" && line != "\r")
        throw std::runtime_error("unexpected RNG golden data");
    return values;
}
void compare_original(const char *path) {
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("missing original RNG vectors");
    std::string line;
    std::getline(input, line);
    if (line != "seed,index,return,state" && line != "seed,index,return,state\r")
        throw std::runtime_error("unexpected RNG golden header");
    rich4::Random random(0);
    std::uint32_t seed{}, step{};
    constexpr std::array<std::uint32_t, 12> seeds{
        0, 1, 2, 3, 42, 12345, 0x7fffffffU, 0x80000000U, 0xfffffffeU, 0xffffffffU, 0x12345678U, 0xdeadbeefU};
    std::set<std::uint32_t> remaining(seeds.begin(), seeds.end());
    std::size_t cases{};
    while (std::getline(input, line)) {
        const auto row = parse_row(line);
        if (row[1] == 0) {
            check(cases == 0 || step == 1024, "truncated original RNG stream");
            check(remaining.erase(row[0]) == 1, "duplicate or unknown original RNG seed");
            random.seed(row[0]);
            seed = row[0];
            step = 0;
        }
        check(row[0] == seed && row[1] == step, "nonsequential original RNG corpus");
        check(step < 1024, "too many steps in original RNG stream");
        check(random.next() == row[2], "original RNG result differs");
        check(random.state() == row[3], "original RNG state differs");
        ++step;
        ++cases;
    }
    check(remaining.empty() && step == 1024 && cases == 12288, "incomplete original RNG corpus");
    std::cout << cases << " original random steps matched\n";
}
} // namespace

int main(int argc, char **argv) {
    try {
        rich4::Random random(1);
        for (const auto expected : {16838, 5758, 10113, 17515, 31051})
            check(random.next() == expected, "Watcom known sequence");
        rich4::Random zero(0);
        check(zero.next() == 0 && zero.state() == 12345, "seed zero must stay zero until first advance");
        rich4::Random first(0xffffffffU), second(0xffffffffU);
        for (int i = 0; i < 10000; ++i) {
            check(first.next() == second.next(), "game streams must be independent");
            check(first.state() == second.state(), "game stream state changed unexpectedly");
        }
        auto copy = first;
        check(copy.next() == first.next(), "copied state must resume the same stream");
        first.seed(1);
        check(first.next() == 16838, "reseed must reset the full state");

        // The same DAT loaded interactively twice should NOT rewind to the
        // same native RNG state. Original save_load.asm reseeds from uptime.
        // Two loads in one millisecond must still receive different seeds.
        rich4::LoadSeedSequence interactive_loads;
        const auto load1 = interactive_loads.next(123456U);
        const auto load2 = interactive_loads.next(123456U);
        const auto load3 = interactive_loads.next(123457U);
        check(load1 == 123457U && load2 == 123458U && load3 == 123460U,
              "reloading the same DAT must not restore the previous random seed");
        rich4::Random first_load(load1), second_load(load2);
        check(first_load.state() != second_load.state(),
              "interactive loaded sessions must not replay identical initial RNG state");
        // Identical seeds remain supported for deterministic headless regression.
        rich4::Random replay(load1);
        check(replay.next() == first_load.next(), "headless seed replay changed");

        if (argc == 2)
            compare_original(argv[1]);
        else if (argc != 1)
            throw std::runtime_error("usage: random_tests [random.csv]");
        std::cout << "Random tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "random: " << error.what() << '\n';
        return 1;
    }
}
