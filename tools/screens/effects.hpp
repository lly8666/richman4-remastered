// SPDX-License-Identifier: GPL-3.0-or-later
// Original on-board animations (docs/ui/effects.md): missiles, explosions, god appearances,
// demolition, throws, ... replayed from the session's EffectEvent queue. The dice roll is drawn
// by the main screen itself, so dice_roll effects are ignored here.
//
// Model (§9 of the spec): the session has ALREADY applied the rule when an effect is drained.
// The player keeps a scene snapshot of the board from the last idle frame and shows it until
// the movie's board-commit frame, then the live world. While anything is queued or playing,
// the app must not tick the session (the original blocks its game loop) and must ignore board
// input. No SDL here: time and sound are supplied by the caller, so tools can render frames.
#pragma once

#include "../native_frame.hpp"
#include "rich4/board_renderer.hpp"
#include "rich4/flc.hpp"
#include "rich4/game.hpp"
#include "rich4/world.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace rich4::native {

// Owned copy of the sprite lists that board::Renderer draws (a board::Scene view of them).
struct SceneData {
    std::vector<board::PlayerSprite> players;
    std::vector<board::ObjectSprite> objects;
    std::vector<board::PropertyState> lands, facilities, commercials;
    std::vector<board::SpecialPlayerSprite> special_players;
    std::vector<board::PropertyOwner> owners;
    [[nodiscard]] board::Scene view() const {
        return {players, objects, lands, facilities, special_players, commercials, owners};
    }
};
// Every player's property colour and flag character by slot, drawn or not (board::PropertyOwner).
[[nodiscard]] std::vector<board::PropertyOwner> property_owners(const World &world);
// Original board draw rule (fcn_0040829d): a player is drawn when placed and not away or confined
// (hotel/UFO/prison/hospital), unless walking into the hotel (controller 0x20).
[[nodiscard]] bool on_board(const World &world, std::size_t slot);
// Board object sprites (fcn_0040829d object loop, rich4.asm:1928-2026). An object carried by a player
// (god, picked-up time bomb) follows that player's drawn sprite `players` (interpolated point and
// heading) and is skipped when the carrier is not drawn; the rest stand on their node.
std::vector<board::ObjectSprite> object_sprites(const World &world, const std::vector<board::PlayerSprite> &players,
                                                std::uint8_t frame);
// Same sprite construction as the main board (no walking interpolation). costumes: per-slot
// GameSession::player_costume (gown/stripes pose), 0 = none.
// Removes unattached object sprites that `hidden(position, kind)` says are not on the board yet.
void hide_objects(std::vector<board::ObjectSprite> &objects,
                  const std::function<bool(board::Point, std::uint8_t)> &hidden);
SceneData make_scene(const World &world, std::uint64_t ticks, const std::array<std::uint8_t, 4> &costumes = {});
// Reverse one pending visual building mutation, without rolling back rule state.
// Apply pending movies in reverse chronological order to reconstruct the picture
// that belongs before their original on-screen commit frames.
void restore_pre_movie_property(std::vector<board::PropertyState> &lands,
                                std::vector<board::PropertyState> &facilities, const EffectEvent &effect);

// Per-kind playback table (values from docs/ui/effects.md §1-§6).
struct EffectSpec {
    char archive = 'd';        // 'd' Data.mkf, 'p' Panel.mkf
    int entry = -1;            // FLC entry; -1 = no movie (throw)
    int commit_frame = 0;      // board shows the pre-mutation snapshot before this frame
    int sound = -1;            // Effect.mkf index, -1 none
    int sound_frame = 0;       // frame whose start plays `sound`
    unsigned hold_ms = 0;      // after the last frame (fcn_0045285e)
    bool camera_on_target = true; // false: camera stays on the actor (demon puff, throws)
    int x = 0, y = 40;         // screen top-left of the movie (fcn_0045144f x, y)
    // How the acting player is drawn under the movie, whatever the (already mutated) world says:
    // leave = at EffectEvent::from until the commit frame, then not at all; stay = at `from`
    // throughout; arrive = hidden until the commit frame; hide = hidden throughout.
    enum class Actor : std::uint8_t { world, leave, stay, arrive, hide } actor = Actor::world;
    bool injured = false; // the overridden actor uses the grey injured pose
    // The last frame stays on screen after playback (no board redraw follows), e.g. under the god
    // arrival caption (fcn_0040e2a2 draws on the primary surface over it). See EffectPlayer::linger.
    bool linger = false;
};
EffectSpec effect_spec(const EffectEvent &effect);

// A movie that takes a player away (ambulance, police car, UFO, airplane: Actor::leave; dog fight:
// stay) shows them at its `from` point until its commit frame, and one that brings them back
// (arrive) hides them until then. The world already holds the result, so while such a movie is still
// pending, every earlier step (bubbles, boxes, other movies) must draw the player the same way: the
// original only moves them right before that movie (rich4_hospital_utils.asm:40-90 bubble, move,
// Data 524; rich4_prison_utils.asm likewise).
struct PendingActor {
    std::size_t slot = 0;
    std::optional<board::Point> position; // nullopt = hidden
    bool injured = false;
};
// Adds the override of `effect` unless an earlier pending effect already decides that player.
void add_pending_actor(std::vector<PendingActor> &pending, const EffectEvent &effect);
// Applies the overrides to a sprite list (players not on the board are added at their old point).
void apply_pending_actors(std::vector<board::PlayerSprite> &players, const World &world,
                          const std::vector<PendingActor> &pending, std::uint8_t frame,
                          const std::array<std::uint8_t, 4> &costumes);

class EffectPlayer {
  public:
    explicit EffectPlayer(Artwork &artwork) : artwork_(artwork) {}

    // Headless/replay mode: effects are dropped on enqueue (zero duration).
    void set_skip(bool skip) noexcept { skip_ = skip; }

    // Call once per frame before any session mutation can happen (start of draw). Only
    // records while idle; `owner` identifies the session so a new game never reuses it.
    void remember(const World &world, const void *owner, std::uint64_t ticks);
    // Per-slot gown/stripes poses for the boards this player draws (GameSession::player_costume).
    void set_costumes(const std::array<std::uint8_t, 4> &costumes) noexcept { costumes_ = costumes; }
    void enqueue(std::vector<EffectEvent> effects, const void *owner);
    // Overrides from effects the app has not handed over yet (its presentation queue), per frame.
    void set_pending_actors(std::vector<PendingActor> pending) { pending_actors_ = std::move(pending); }
    // Property-changing movies still waiting in the application's presentation
    // queue (not yet handed to this player); only metadata is copied.
    void set_future_effects(std::vector<EffectEvent> pending) { future_effects_ = std::move(pending); }
    [[nodiscard]] bool active() const noexcept { return current_.has_value() || !queue_.empty(); }
    // The world already holds a thrown roadblock/mine/time bomb on its node while the throw is still
    // queued or flying; the original only shows it after the throw lands (_rich4_animate_object
    // restores its own background, then _rich4_refresh_screen). True for such an object.
    [[nodiscard]] bool throw_pending(board::Point node, std::uint8_t kind) const;
    void clear();
    // The finished movie of a `linger` spec, still on screen until the app drops it.
    [[nodiscard]] bool lingering() const noexcept { return lingering_.has_value(); }
    void draw_linger(graphics::Image &canvas, board::Renderer &board, const World &world, std::uint8_t rotation,
                     std::uint64_t ticks);
    void drop_linger() noexcept { lingering_.reset(); }

    using SoundFunction = std::function<void(std::size_t)>;
    // Advances by elapsed_ms (clamped) and paints the board viewport [0,40,440,480) of canvas:
    // the board (snapshot or live, camera per effect) plus the movie/sprites. No-op when idle.
    void draw(graphics::Image &canvas, board::Renderer &board, const World &world, std::uint8_t rotation,
              unsigned elapsed_ms, std::uint64_t ticks, const SoundFunction &play_sound);

  private:
    struct Playing {
        EffectEvent event;
        EffectSpec spec;
        std::unique_ptr<flc::Decoder> movie;
        graphics::Image frame;  // last decoded movie frame
        unsigned elapsed = 0;   // ms since start
        unsigned delay = 100;   // ms per frame
        bool sounded = false;
        std::vector<board::Point> path; // throws: screen points
    };
    void start(EffectEvent event);
    std::vector<std::uint8_t> &movie_bytes(char archive, int entry);

    Artwork &artwork_;
    bool skip_ = false;
    std::deque<EffectEvent> queue_;
    std::optional<Playing> current_;
    std::optional<Playing> lingering_;
    std::optional<SceneData> snapshot_;
    const void *snapshot_owner_ = nullptr;
    std::map<std::pair<char, int>, std::vector<std::uint8_t>> movies_;
    std::array<std::uint8_t, 4> costumes_{};
    std::vector<PendingActor> pending_actors_;
    std::vector<EffectEvent> future_effects_;
};

} // namespace rich4::native
