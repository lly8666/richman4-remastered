// SPDX-License-Identifier: GPL-3.0-or-later
// Original in-game screen chrome (docs/ui/main-screen.md): icon toolbar, 4-tab info panel,
// minimap with rotate buttons, season/calendar panel, floating GO button and the FLC dice roll.
// The Application owns the board render and dispatches the registered hit areas by id.
#pragma once

#include "../native_frame.hpp"
#include "rich4/board_renderer.hpp"
#include "rich4/game.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

namespace rich4::native {

// Hit ids. The replay tests click the pre-existing ids, so they keep their actions.
namespace main_ids {
inline constexpr int save = 71, load = 72, rotate_left = 73, rotate_right = 74, query = 75, options = 76,
                     cards = 77, tools = 78, stocks = 79, trading = 80, help = 81, ai = 82, full_map = 83,
                     full_map_close = 84, tab_first = 85, sun = 89, moon = 90, minimap = 91, roll = 100,
                     dice_count = 101, go_frame = 102;
}

struct MainScreenActions {
    std::function<void()> help, options, ai, load, save, query, tools, cards, trading, stocks;
    std::function<void(unsigned)> rotate;     // +7 turns left, +1 turns right
    std::function<void()> roll;               // GO
    std::function<void(unsigned)> dice_count; // requested count 1..3
    std::function<void(unsigned)> sound;      // original sound effect id
    bool can_save = false, can_load = false;
};

class MainScreen {
  public:
    MainScreen() = default;
    // Development preset "view=N,tab=N,calendar=N,map=1" for screenshots of every state.
    explicit MainScreen(std::string_view preset);
    // Board camera centre: the minimap free-scroll centre when detached, else `follow`.
    [[nodiscard]] board::Point camera_center(board::Point follow) const {
        return free_scroll_.value_or(follow);
    }
    void follow_player() {
        free_scroll_.reset();
    }
    // Starts the dice animation on a roll and re-centres the view when a turn or roll starts.
    void on_event(const GameEvent &event, const GameSession &session, Artwork &artwork);
    // The original roll animation is synchronous: while busy the application holds the game clock.
    // It covers the character's toss pose, the dice FLC and the result-face hold.
    [[nodiscard]] bool dice_busy() const;
    void advance(unsigned elapsed_ms);
    // The player whose turn is being presented. While the app's presentation queue is still
    // showing an earlier turn, the core's current_player has already moved on.
    void set_displayed_player(std::optional<std::size_t> slot) {
        displayed_player_ = slot;
    }
    // The core waits for a human AND the screen has caught up with every earlier turn: computer
    // turns are resolved at once and then presented, so GO (and turn-only toolbar icons) must not
    // appear while an earlier player's turn is still being shown.
    [[nodiscard]] bool human_turn_shown(const GameSession &session) const {
        return session.phase() == GamePhase::human_ready && !displayed_player_;
    }
    [[nodiscard]] std::size_t displayed_player(const World &world) const {
        const auto slot = displayed_player_.value_or(world.header.current_player);
        return std::min<std::size_t>(slot, world.players.size() - 1);
    }
    // Toss frame to draw for `slot` (board::PlayerSprite::tossing) while its roll animation runs;
    // the last toss frame holds under the dice FLC and faces, as the original does not redraw.
    [[nodiscard]] std::optional<std::uint8_t> toss_frame(std::size_t slot) const;
    // Headless replays skip the toss (the dice FLC keeps its existing timing).
    void set_skip_toss(bool skip) {
        skip_toss_ = skip;
    }
    void cycle_view_mode() {
        view_mode_ = (view_mode_ + 1) % 3;
    }
    void set_view_mode(unsigned mode) {
        view_mode_ = mode % 3;
        // Switching layouts must end a minimap drag left over from the old panel position.
        minimap_drag_ = false;
    }
    [[nodiscard]] unsigned view_mode() const {
        return view_mode_;
    }
    // Raw pointer input beyond the click dispatch: drags (GO button, minimap) and button releases.
    void pointer_motion(int x, int y, bool left_down);
    void pointer_up(int x, int y, bool right_button);

    // Dice and GO button over the board, then toolbar and right-hand panels.
    void draw(Frame &frame, const GameSession &session, unsigned rotation, const MainScreenActions &actions);

  private:
    void draw_toolbar(Frame &frame, const GameSession &session, const MainScreenActions &actions);
    void draw_info_panel(Frame &frame, const World &world, const MainScreenActions &actions);
    void draw_header(Frame &frame, const World &world);
    void draw_minimap(Frame &frame, const World &world, int top, const MainScreenActions &actions);
    void draw_calendar(Frame &frame, const World &world, const MainScreenActions &actions);
    void draw_go(Frame &frame, const GameSession &session, const MainScreenActions &actions);
    void draw_dice(Frame &frame, const GameSession &session, unsigned rotation, const MainScreenActions &actions);
    void draw_full_map(Frame &frame, const World &world);
    std::uint8_t go_mask(Frame &frame, int x, int y);

    unsigned view_mode_ = 1, calendar_mode_ = 0;
    std::array<std::uint8_t, 9> tab_{};
    int hover_ = -1;
    int go_x_ = 180, go_y_ = 120;
    std::optional<std::pair<int, int>> go_drag_; // pointer offset inside the GO button
    std::optional<std::size_t> displayed_player_;
    // Touch: a tap anywhere on GO rolls (the original's 35x41 roll hotspot is too small for a
    // finger); it becomes a drag only once the pointer moves. Set on press, fired on release.
    std::function<void()> pending_roll_;
    std::pair<int, int> press_point_{};
    std::optional<board::Point> free_scroll_;
    bool minimap_drag_ = false, full_map_ = false;
    int minimap_top_ = 280;
    std::vector<std::uint8_t> go_mask_;
    // Dice roll animation (Panel 4-6 FLC, then Panel 3 faces).
    std::map<std::size_t, std::vector<graphics::Image>> dice_animations_;
    std::vector<std::uint8_t> dice_faces_;
    std::uint8_t dice_direction_ = 0;
    bool dice_active_ = false, dice_sound_played_ = false, skip_toss_ = false;
    unsigned dice_elapsed_ = 0, dice_frames_ = 0;
    // Pre-roll toss pose (movement state 2): frames per direction and total duration in ms.
    std::size_t toss_slot_ = 0;
    unsigned toss_frames_ = 0, toss_ms_ = 0;
};

} // namespace rich4::native
