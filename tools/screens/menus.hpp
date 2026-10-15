// SPDX-License-Identifier: GPL-3.0-or-later
// Original 大富翁4 menu screens (docs/ui/finance-menus.md §1, §2, §5, §6, §10): main menu
// (Data 1), new-game setup (jump.mkf 8 + backdrops), save/load slots (Data 520), options
// (Data 3) and the player information view (Panel 9).
#pragma once

#include "draw.hpp"
#include "rich4/world.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace rich4::native::screens {

namespace ids {
inline constexpr int menu_start = 1, menu_load = 2, menu_options = 3, menu_new_stage = 4, menu_exit = 5;
inline constexpr int setup_character = 10, setup_map = 30, setup_ok = 40, setup_exit = 41, setup_dropdown = 43,
                     setup_dropdown_close = 49, setup_item = 130;
inline constexpr int options_speed = 50, options_outside = 51, options_cancel = 52, options_music = 53,
                     options_sound = 54, options_ok = 55, options_song = 56, options_volume = 47,
                     options_confirm_yes = 58, options_confirm_no = 59, options_animation = 63,
                     options_view_first = 64;
inline constexpr std::array<int, 3> options_big{57, 61, 62};
inline constexpr int info_exit = 60, info_player = 160, info_page = 165, info_up = 168, info_down = 169,
                     info_category = 170;
inline constexpr int load_slot = 520, save_slot = 530, slot_cancel = 529;
} // namespace ids

// ---------------------------------------------------------------------------------------
struct MenuActions {
    std::function<void()> start, load, options, exit, new_stage;
    bool load_enabled = true;
};
class MainMenu {
  public:
    void draw(Host &host, const MenuActions &actions);

  private:
    int hovered_ = -1;
};

// ---------------------------------------------------------------------------------------
struct NewGameChoice {
    unsigned stage = 0, map = 0;
    std::int32_t fund = 200000;
    std::uint32_t lease_mode = 0;
    std::vector<NewGamePlayer> players;
};
class NewGameSetup {
  public:
    void open(unsigned stage, unsigned map, std::uint32_t seed);
    void draw(Host &host, const std::function<void(const NewGameChoice &)> &start,
              const std::function<void()> &exit);
    void cancel(Host &host, const std::function<void()> &exit);
    [[nodiscard]] const std::vector<std::uint8_t> &picks() const noexcept {
        return picks_;
    }
    [[nodiscard]] unsigned map() const noexcept {
        return map_;
    }
    [[nodiscard]] unsigned stage() const noexcept {
        return stage_;
    }
    [[nodiscard]] int player_count() const noexcept {
        return values_[0] + 2;
    }
    [[nodiscard]] bool busy() const noexcept {
        return filling_ || leaving_;
    }

  private:
    NewGameChoice choice() const;
    void fill_one();
    void draw_backdrop(Host &host);
    unsigned stage_ = 0, map_ = 0;
    std::array<int, 6> values_{2, 1, 0, 0, 0, 0};
    std::vector<std::uint8_t> picks_;
    std::size_t human_count_ = 0;
    int dropdown_ = -1, hovered_ = -1;
    bool filling_ = false, leaving_ = false;
    std::uint64_t fill_tick_ = 0, leave_tick_ = 0;
    std::mt19937 random_{1};
    graphics::Image backdrop_;
    int backdrop_key_ = -1;
};

// ---------------------------------------------------------------------------------------
struct SaveSlot {
    bool valid = false;
    SaveDate date{};
    std::uint16_t map = 0, stage = 0;
    std::vector<std::uint8_t> characters;
};
class SaveLoadScreen {
  public:
    void open(bool saving, std::array<SaveSlot, 6> slots) {
        saving_ = saving;
        slots_ = std::move(slots);
        hovered_ = -1;
    }
    void draw(Host &host, const std::function<void(int)> &choose, const std::function<void()> &cancel);
    [[nodiscard]] bool saving() const noexcept {
        return saving_;
    }
    [[nodiscard]] const std::array<SaveSlot, 6> &slots() const noexcept {
        return slots_;
    }

  private:
    bool saving_ = false;
    std::array<SaveSlot, 6> slots_{};
    int hovered_ = -1;
};

// ---------------------------------------------------------------------------------------
struct OptionsState {
    unsigned speed = 1; // 1, 2, 4
    bool music = true, sound = true;
    bool animation = true; // 動畫過程 (RICH4.CFG[1]): interactive mini-games for humans
    unsigned view_mode = 1; // 視窗 (RICH4.CFG[5]): 0 calendar, 1 minimap, 2 combined
};
struct OptionsActions {
    std::function<void(const OptionsState &)> apply;
    std::function<void()> close;
    std::function<void(unsigned)> play_song; // 1..8
    unsigned playing_song = 0;               // 1..8, 0 none
    std::function<void()> restart, quit;     // in-game big buttons (重新遊戲 / 結束遊戲)
    std::function<void()> help;              // out-of-game 遊戲說明: the Help window, centred
};
class OptionsScreen {
  public:
    void open(OptionsState state, bool in_game) {
        state_ = state;
        state_.view_mode %= 3;
        in_game_ = in_game;
        confirm_ = -1;
    }
    void draw(Host &host, const OptionsActions &actions);

  private:
    OptionsState state_{};
    bool in_game_ = false;
    int confirm_ = -1;
};

// ---------------------------------------------------------------------------------------
class PlayerInfo {
  public:
    void open(std::size_t player) {
        player_ = player;
        page_ = 0;
        category_ = 0;
        scroll_ = 0;
    }
    void draw(Host &host, const World &world, const std::function<void()> &exit);

  private:
    void draw_assets(Host &host, const World &world);
    void draw_properties(Host &host, const World &world);
    void draw_stocks(Host &host, const World &world);
    std::size_t player_ = 0, scroll_ = 0;
    int page_ = 0, category_ = 0;
};

} // namespace rich4::native::screens
