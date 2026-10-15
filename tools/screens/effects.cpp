// SPDX-License-Identifier: GPL-3.0-or-later
// See effects.hpp. Asset/timing/sound values cite docs/ui/effects.md and the upstream asm.
#include "effects.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

namespace rich4::native {

bool on_board(const World &world, std::size_t slot) {
    if (slot >= world.players.size())
        return false;
    const auto &state = world.players[slot].state;
    const auto &value = world.players[slot].status;
    return state.controller_flags && (state.x || state.y) &&
           (!(value.days_in_hotel | value.days_disappearing | value.days_in_prison | value.days_in_hospital) ||
            (state.controller_flags & 0x20U));
}

namespace {
board::PlayerSprite player_sprite(const World &world, std::size_t slot, std::uint8_t frame, std::uint8_t costume) {
    const auto &player = world.players.at(slot);
    const auto &state = player.state;
    board::PlayerSprite sprite{{state.x, state.y}, state.character, state.direction, frame,
                               static_cast<std::uint8_t>(state.traffic_method & 3U),
                               ui::player_color(state.color), state.slot,
                               state.slot == world.header.current_player, false,
                               state.node_id < world.nodes.size() &&
                                   (world.nodes[state.node_id].flags & 0x80000000U) != 0,
                               player.status.days_sleep_walking != 0, (state.controller_flags & 0x40U) != 0};
    sprite.special_pose = costume;
    return sprite;
}
} // namespace

std::vector<board::ObjectSprite> object_sprites(const World &world, const std::vector<board::PlayerSprite> &players,
                                                std::uint8_t frame, std::optional<std::size_t> hidden_arriving_god) {
    std::vector<board::ObjectSprite> objects;
    for (std::size_t id = 0; id < world.objects.size(); ++id) {
        const auto &object = world.objects[id];
        // The rule attaches immediately, but the carried god must not be drawn
        // until the arrival movie and caption have finished.
        if (hidden_arriving_god && object.attached_player == *hidden_arriving_god + 1U &&
            *hidden_arriving_god < world.players.size() &&
            world.players[*hidden_arriving_god].status.god_info == id + 1U)
            continue;
        if (object.attached_player) { // drawn at the carrier, offset behind it (board_renderer)
            const auto carrier = std::size_t(object.attached_player - 1U);
            for (const auto &player : players)
                if (player.slot == carrier) {
                    const bool far = object.kind == 18 && carrier < world.players.size() &&
                                     world.players[carrier].status.god_info; // rich4.asm:2016
                    objects.push_back({player.position, object.kind, player.direction, frame,
                                       static_cast<std::uint8_t>(far ? 2 : 1)});
                }
        } else if (object.node_id && object.node_id < world.nodes.size()) {
            const auto &node = world.nodes[object.node_id];
            objects.push_back({{node.x, node.y}, object.kind, object.direction, frame});
        }
    }
    return objects;
}

void hide_objects(std::vector<board::ObjectSprite> &objects,
                  const std::function<bool(board::Point, std::uint8_t)> &hidden) {
    std::erase_if(objects, [&](const board::ObjectSprite &object) {
        return !object.follow && hidden(object.position, object.kind);
    });
}

std::vector<board::PropertyOwner> property_owners(const World &world) {
    std::vector<board::PropertyOwner> owners;
    for (const auto &player : world.players)
        owners.push_back({ui::player_color(player.state.color), player.state.character});
    return owners;
}

SceneData make_scene(const World &world, std::uint64_t ticks, const std::array<std::uint8_t, 4> &costumes,
                     std::optional<std::size_t> hidden_arriving_god) {
    SceneData scene;
    scene.owners = property_owners(world);
    const auto frame = static_cast<std::uint8_t>((ticks / 120) % 6);
    for (std::size_t slot = 0; slot < world.players.size(); ++slot)
        if (on_board(world, slot))
            scene.players.push_back(player_sprite(world, slot, frame, slot < 4 ? costumes[slot] : 0));
    for (std::size_t index = 1; index < world.lands.size(); ++index) {
        const auto &land = world.lands[index];
        scene.lands.push_back({land.id, land.economy.owner_code, land.economy.level, land.economy.chain_store,
                               land.temporary_state});
    }
    for (std::size_t index = 1; index < world.facilities.size(); ++index) {
        const auto &facility = world.facilities[index];
        scene.facilities.push_back({facility.id, facility.owner_code, facility.level, facility.facility_type,
                                    facility.temporary_state});
    }
    scene.objects = object_sprites(world, scene.players, frame, hidden_arriving_god);
    for (std::size_t index = 0; index < world.special_actors.size(); ++index) {
        const auto &actor = world.special_actors[index];
        if (actor.inactive_state || !actor.node_id || actor.node_id >= world.nodes.size())
            continue;
        scene.special_players.push_back({{actor.x, actor.y}, static_cast<std::uint8_t>(index + 4), actor.direction,
                                         frame, false, false, (world.nodes[actor.node_id].flags & 0x80000000U) != 0,
                                         actor.days_sleep_walking != 0});
    }
    for (const auto &company : world.commercials)
        if (company.id)
            scene.commercials.push_back({company.id, company.owner_code, 0, 0});
    return scene;
}

EffectSpec effect_spec(const EffectEvent &effect) {
    switch (effect.kind) {
    case EffectKind::dice_roll: // §1: drawn by the main screen's own dice animation, not here
        return {'p', -1, 0, -1, 0, 0, false};
    case EffectKind::top_floor_sparkle: // §2 fcn_0040b0cd
        return {'d', 523, 0, 90, 0, 0, true};
    case EffectKind::build_rise: // §2 robot worker, flags 0x2C0001
        return {'d', 553, 44, 91, 0, 0, true};
    case EffectKind::object_throw: // §0 _rich4_animate_object, hold 100; sound after landing
        return {'d', -1, 0, effect.object_kind == 16 ? 33 : effect.object_kind == 17 ? 34 : 10, 0, 100, false};
    case EffectKind::missile: // §4 flags 0x90001
        return {'d', 528, 9, 81, 0, 0, true};
    case EffectKind::nuclear_missile: // §4 flags 0x80090001 (whiten mode not reproduced)
        return {'d', 530, 9, 83, 0, 0, true};
    case EffectKind::explosion: // §4 mine / time bomb, flags 0x30001
        return {'d', 525, 3, 82, 0, 0, true};
    case EffectKind::demolish: // §5 flags 0x260001, then 500 ms
        return {'d', 529, 38, 97, 0, 500, true};
    case EffectKind::monster: // §5 flags 0x100001, then 500 ms
        return {'d', 557, 16, 80, 0, 500, true};
    case EffectKind::god_appear: { // §6 _rich4_attach_god jump table, flags 1
        struct God {
            std::uint8_t kind;
            int entry, sound;
        };
        static constexpr std::array<God, 12> gods{{{1, 540, 102}, {2, 541, 103}, {3, 542, 104}, {4, 543, 105},
                                                   {5, 544, 106}, {6, 545, 107}, {7, 546, 108}, {8, 547, 109},
                                                   {9, 548, 110}, {10, 549, 112}, {12, 550, 111}, {15, 551, 113}}};
        for (const auto &god : gods)
            if (god.kind == effect.object_kind)
                return {'d', god.entry, 0, god.sound, 0, 0, true, 0, 40, EffectSpec::Actor::world, false, true};
        return {'d', -1, 0, -1, 0, 0, true}; // 11/13/14: no appearance movie
    }
    case EffectKind::demon_puff: // §6 Data 526 at building anchor - (55,55), flags 0x30001
        return {'d', 526, 3, 95, 0, 0, false};
    case EffectKind::bankrupt: // §6 rich4_player_bankrupt.asm, flags 1
        return {'d', 555, 0, 100, 0, 0, true};
    case EffectKind::money_spinner: // drawn by screens/spinners.cpp, never queued here
    case EffectKind::roulette:
    case EffectKind::dividend: // MONTHLY: full-screen, drawn by screens/monthly.cpp
    case EffectKind::lottery_draw:
    case EffectKind::month_report:
        return {'p', -1, 0, -1, 0, 0, false};
    // ---- docs/audit/data-400-601.md board movies (fcn_0045144f flags/sounds from the asm) ----
    case EffectKind::ambulance: // 440x74 at (0,210), flags 0x1E0001, Effect 92
        return {'d', 524, 30, 92, 0, 0, true, 0, 210, EffectSpec::Actor::leave, true};
    case EffectKind::police: // flags 0x120001, Effect 94
        return {'d', 538, 18, 94, 0, 0, true, 0, 40, EffectSpec::Actor::leave};
    case EffectKind::dog_vehicle: // flags 0x10001, Effect 85
        return {'d', 552, 1, 85, 0, 0, true};
    case EffectKind::dog_fight: // flags 0x30001, Effect 93; hospital (ambulance) follows
        return {'d', 532, 3, 93, 0, 0, true, 0, 40, EffectSpec::Actor::stay, true};
    case EffectKind::ufo: // fcn_0040d375 / fcn_0040d4e5: flags 0x1C0001, Effect 84
        return {'d', 533, 28, 84, 0, 0, true, 0, 40,
                effect.object_kind ? EffectSpec::Actor::arrive : EffectSpec::Actor::leave};
    case EffectKind::airplane: // flags 0x140001, Effect 96
        return {'d', 558, 20, 96, 0, 0, true, 0, 40,
                effect.object_kind ? EffectSpec::Actor::arrive : EffectSpec::Actor::leave};
    case EffectKind::parachute: // fcn_00418c55: Data 559 + character, flags 1, no sound; the player
        return {'d', 559 + std::min<int>(effect.object_kind, 11), 0, -1, 0, 0, true, 0, 40, EffectSpec::Actor::hide}; // appears afterwards
    case EffectKind::news_movie: // rich4_news.asm, camera on the hit property
        switch (effect.object_kind) {
        case 4: // meteor, flags 0x180001
            return {'d', 531, 24, 86, 0, 0, true};
        case 5: // UFO, flags 0x200001
            return {'d', 539, 32, 84, 0, 0, true};
        case 15: // gas explosion, flags 0x50001
            return {'d', 527, 5, 87, 0, 0, true};
        case 20: // typhoon / tornado, flags 0x80001
            return {'d', 534, 8, 89, 0, 0, true};
        case 21:
            return {'d', 535, 8, 88, 0, 0, true};
        default:
            return {'d', -1, 0, -1, 0, 0, true};
        }
    case EffectKind::card_throw: // _rich4_animate_object(0, ...): Data 415, hold 100 ms, no sound
        return {'d', -1, 0, -1, 0, 100, false};
    case EffectKind::item_box: // core_actions:2507, flags 1 at (208,180), Effect 99
        return {'d', 536, 0, 99, 0, 0, false, 208, 180};
    case EffectKind::coupon: // core_actions:2394, flags 1 at (204,180), Effect 98
        return {'d', 537, 0, 98, 0, 0, false, 204, 180};
    case EffectKind::truck_demolish: // player_utils state 3: pose 12 + Data 526 puff, Effect 15
        return {'d', 526, 0, 15, 0, 0, false};
    case EffectKind::holiday_fireworks: // fcn_00452444, flags 1
        return {'d', 523, 0, 90, 0, 0, false};
    case EffectKind::christmas:
        return {'d', 554, 0, 114, 0, 0, false};
    case EffectKind::pause: // a timed hold, run by the app's presentation queue (never queued here)
        return {'p', -1, 0, -1, 0, 0, false};
    case EffectKind::god_leave: // rich4_gods.asm:239-419: sprite rise, no movie; sound 54 is cued before
        return {'d', -1, 0, -1, 0, 0, false};
    }
    return {};
}

void add_pending_actor(std::vector<PendingActor> &pending, const EffectEvent &effect) {
    using Actor = EffectSpec::Actor;
    const auto spec = effect_spec(effect);
    if (std::any_of(pending.begin(), pending.end(), [&](const auto &entry) { return entry.slot == effect.player; }))
        return;
    if ((spec.actor == Actor::leave || spec.actor == Actor::stay) && effect.from)
        pending.push_back({effect.player, board::Point{effect.from->x, effect.from->y}, spec.injured});
    else if (spec.actor == Actor::arrive || spec.actor == Actor::hide)
        pending.push_back({effect.player, std::nullopt, false});
}

void apply_pending_actors(std::vector<board::PlayerSprite> &players, const World &world,
                          const std::vector<PendingActor> &pending, std::uint8_t frame,
                          const std::array<std::uint8_t, 4> &costumes) {
    for (const auto &entry : pending) {
        if (entry.slot >= world.players.size())
            continue;
        std::optional<board::PlayerSprite> sprite;
        for (const auto &player : players)
            if (player.slot == entry.slot)
                sprite = player;
        std::erase_if(players, [&](const auto &player) { return player.slot == entry.slot; });
        if (!entry.position)
            continue;
        if (!sprite)
            sprite = player_sprite(world, entry.slot, frame, entry.slot < 4 ? costumes[entry.slot] : 0);
        sprite->position = *entry.position;
        sprite->walking = sprite->tossing = false;
        sprite->on_water = false;
        sprite->injured = sprite->injured || entry.injured;
        players.push_back(*sprite);
    }
}

void restore_pre_movie_property(std::vector<board::PropertyState> &lands,
                                std::vector<board::PropertyState> &facilities, const EffectEvent &effect) {
    if (!effect.has_previous_property)
        return;
    const bool facility = effect.property > 4000 && effect.property < 6000;
    if (!facility && !(effect.property > 2000 && effect.property < 4000))
        return;
    auto &list = facility ? facilities : lands;
    const auto id = std::uint32_t(effect.property - (facility ? 4000 : 2000));
    for (auto &plot : list)
        if (plot.id == id) {
            plot.owner = effect.previous_owner;
            plot.level = effect.previous_level;
            plot.type = effect.previous_type;
            break;
        }
}

void EffectPlayer::remember(const World &world, const void *owner, std::uint64_t ticks) {
    if (active())
        return;
    snapshot_ = make_scene(world, ticks, costumes_);
    snapshot_owner_ = owner;
}

void EffectPlayer::enqueue(std::vector<EffectEvent> effects, const void *owner) {
    if (effects.empty())
        return;
    if (owner != snapshot_owner_) { // A different session: nothing cached belongs to it.
        clear();
        snapshot_owner_ = owner;
    }
    if (skip_)
        return;
    for (auto &effect : effects)
        if (effect.kind == EffectKind::object_throw || effect.kind == EffectKind::card_throw ||
            effect.kind == EffectKind::god_leave || effect_spec(effect).entry >= 0)
            queue_.push_back(std::move(effect));
}

std::optional<std::size_t> EffectPlayer::arriving_god_player() const noexcept {
    if (current_ && current_->event.kind == EffectKind::god_appear)
        return current_->event.player;
    for (const auto &effect : queue_)
        if (effect.kind == EffectKind::god_appear)
            return effect.player;
    if (lingering_ && lingering_->event.kind == EffectKind::god_appear)
        return lingering_->event.player;
    return std::nullopt;
}

bool EffectPlayer::throw_pending(board::Point node, std::uint8_t kind) const {
    const auto matches = [&](const EffectEvent &effect) {
        return effect.kind == EffectKind::object_throw && effect.object_kind == kind && effect.target.x == node.x &&
               effect.target.y == node.y;
    };
    return (current_ && matches(current_->event)) || std::any_of(queue_.begin(), queue_.end(), matches);
}

void EffectPlayer::draw_linger(graphics::Image &canvas, board::Renderer &board, const World &world,
                               std::uint8_t rotation, std::uint64_t ticks) {
    if (!lingering_)
        return;
    const auto &play = *lingering_;
    const board::Camera camera{{play.event.target.x, play.event.target.y}, static_cast<std::uint8_t>(rotation & 7U)};
    board.render(canvas, make_scene(world, ticks, costumes_,
                                    play.event.kind == EffectKind::god_appear
                                        ? std::optional<std::size_t>(play.event.player) : std::nullopt).view(), camera);
    graphics::blit(canvas, play.frame, play.spec.x, play.spec.y, {0, 40, 440, 480});
}

void EffectPlayer::clear() {
    queue_.clear();
    current_.reset();
    lingering_.reset();
    snapshot_.reset();
}

std::vector<std::uint8_t> &EffectPlayer::movie_bytes(char archive, int entry) {
    auto &bytes = movies_[{archive, entry}];
    if (bytes.empty())
        bytes = artwork_.archive(archive).decode(static_cast<std::size_t>(entry));
    return bytes;
}

void EffectPlayer::start(EffectEvent event) {
    lingering_.reset(); // the next picture replaces it
    Playing playing{std::move(event), {}, nullptr, {}, 0, 100, false, {}};
    playing.spec = effect_spec(playing.event);
    if (playing.spec.entry >= 0) {
        playing.movie = std::make_unique<flc::Decoder>(movie_bytes(playing.spec.archive, playing.spec.entry));
        playing.delay = playing.movie->delay_ms() ? playing.movie->delay_ms() : 100;
    }
    current_ = std::move(playing);
}

void EffectPlayer::draw(graphics::Image &canvas, board::Renderer &board, const World &world, std::uint8_t rotation,
                        unsigned elapsed_ms, std::uint64_t ticks, const SoundFunction &play_sound) {
    try {
        if (!current_) {
            if (queue_.empty())
                return;
            auto next = std::move(queue_.front());
            queue_.pop_front();
            start(std::move(next));
            elapsed_ms = 0; // the first frame is shown in full
        }
        auto &play = *current_;
        play.elapsed += std::min(elapsed_ms, 100U);
        const auto &event = play.event;
        const auto sound = [&](int index) {
            if (index >= 0 && play_sound)
                play_sound(static_cast<std::size_t>(index));
        };
        const bool truck = event.kind == EffectKind::truck_demolish;
        // A god which will appear later in this batch is already attached in
        // world data. Hide it under earlier movies as well as its own entrance.
        std::optional<std::size_t> arriving_god;
        if (event.kind == EffectKind::god_appear)
            arriving_god = event.player;
        else {
            for (const auto &future : future_effects_)
                if (future.kind == EffectKind::god_appear) {
                    arriving_god = future.player;
                    break;
                }
            if (!arriving_god)
                for (const auto &later : queue_)
                    if (later.kind == EffectKind::god_appear) {
                        arriving_god = later.player;
                        break;
                    }
        }
        // Camera (fcn_0041d476(x,y,0)): the target for targeted movies, else the actor.
        board::Point actor{event.target.x, event.target.y};
        if (event.from)
            actor = {event.from->x, event.from->y};
        if (event.kind == EffectKind::demon_puff && event.player < world.players.size())
            actor = {world.players[event.player].state.x, world.players[event.player].state.y};
        const board::Camera camera{play.spec.camera_on_target ? board::Point{event.target.x, event.target.y} : actor,
                                   static_cast<std::uint8_t>(rotation & 7U)};
        const graphics::Rect viewport{0, 40, 440, 480};

        // ---- timeline ----
        int frame_index = 0;
        bool finished = false, committed = true;
        int pose_frame = 0; // truck: pose 12 frame of the swinging player
        if (truck && play.movie) {
            // player_utils.asm state 3, one step per 80 ms game tick: tick 0 turns to the building,
            // ticks 1-6 wait (0x4749e0), then the swing frame c advances one per tick from 1; at c = 2
            // the sound plays and the Data 526 puff starts (one frame per tick), at c = 3 the
            // building is demolished (board commit). No puff during the wait is UNVERIFIED.
            const auto tick = static_cast<int>(play.elapsed / 80U);
            const auto count = static_cast<int>(play.movie->frame_count());
            pose_frame = tick == 0 ? 0 : tick <= 7 ? 1 : tick - 6;
            frame_index = tick - 8; // puff frame, < 0 = not started
            while (frame_index >= 0 && static_cast<int>(play.movie->decoded_frames()) <= frame_index &&
                   play.movie->next())
                play.frame = play.movie->image();
            if (!play.sounded && tick >= 8) {
                sound(play.spec.sound);
                play.sounded = true;
            }
            committed = tick >= 9;
            finished = frame_index >= count;
        } else if (play.movie) {
            const auto count = static_cast<int>(std::max<std::size_t>(1, play.movie->frame_count()));
            frame_index = std::min(static_cast<int>(play.elapsed / play.delay), count - 1);
            finished = play.elapsed >= static_cast<unsigned>(count) * play.delay + play.spec.hold_ms;
            while (static_cast<int>(play.movie->decoded_frames()) <= frame_index && play.movie->next())
                play.frame = play.movie->image();
            if (!play.sounded && frame_index >= play.spec.sound_frame) {
                sound(play.spec.sound);
                play.sounded = true;
            }
            committed = frame_index >= play.spec.commit_frame;
        } else if (event.kind == EffectKind::god_leave) {
            // 24 steps of 60 ms (fcn_0045285e(0x3c)), each 10 px higher and one facing frame on; it
            // ends early once the sprite's bottom clears the board's top edge (y < 40).
            frame_index = static_cast<int>(std::min(play.elapsed / 60U, 23U));
            committed = true;
            finished = play.elapsed >= 24U * 60U;
            if (const auto start = board::project(actor, camera); !start)
                finished = true;
        } else { // object/card throw: one step per 8 screen px, >= 24 ms per step
            if (play.path.empty()) {
                const auto from = board::project(actor, camera);
                const auto to = board::project({event.target.x, event.target.y}, camera);
                if (from && to) {
                    const double dx = to->x - from->x, dy = to->y - from->y;
                    const int steps = static_cast<int>(std::sqrt(dx * dx + dy * dy) * 0.125 + 1);
                    for (int step = 1; step <= steps; ++step)
                        play.path.push_back({from->x + static_cast<int>(dx * step / steps),
                                             from->y + static_cast<int>(dy * step / steps)});
                } else
                    play.path.push_back({-1000, -1000}); // off screen: nothing to show
            }
            const auto steps = static_cast<unsigned>(play.path.size());
            frame_index = static_cast<int>(std::min(play.elapsed / 24U, steps - 1));
            if (!play.sounded && play.elapsed >= steps * 24U) {
                sound(play.spec.sound);
                play.sounded = true;
            }
            finished = play.elapsed >= steps * 24U + play.spec.hold_ms;
            committed = false;
        }
        if (finished) {
            if (play.spec.linger && play.movie && play.frame.width) {
                lingering_ = std::move(*current_);
                current_.reset();
                draw_linger(canvas, board, world, rotation, ticks);
                return;
            }
            current_.reset();
            return; // draw_board already painted the live world this frame
        }

        // ---- board: pre-mutation snapshot until the commit frame, then the live world ----
        SceneData scene;
        if (committed || !snapshot_) {
            if (committed)
                snapshot_.reset(); // later effects of the same batch use the live world (spec §9.4)
            scene = make_scene(world, ticks, costumes_, arriving_god);
        } else
            scene = *snapshot_;
        // Multiple property effects may have been computed in one core tick.
        // Reconstruct the property state from the effects not yet committed,
        // rather than exposing the final building before its construction,
        // collapse or explosion FLC. This also works after snapshot_ is reset.
        for (auto it = future_effects_.rbegin(); it != future_effects_.rend(); ++it)
            restore_pre_movie_property(scene.lands, scene.facilities, *it);
        for (auto it = queue_.rbegin(); it != queue_.rend(); ++it)
            restore_pre_movie_property(scene.lands, scene.facilities, *it);
        if (!committed)
            restore_pre_movie_property(scene.lands, scene.facilities, event);
        { // movies still to come that take a player away or bring them back (see PendingActor)
            auto pending = pending_actors_;
            for (const auto &later : queue_)
                add_pending_actor(pending, later);
            if (play.spec.actor != EffectSpec::Actor::world || truck) // this movie draws its own actor
                std::erase_if(pending, [&](const auto &entry) { return entry.slot == event.player; });
            if (!pending.empty()) {
                const auto frame = static_cast<std::uint8_t>((ticks / 120) % 6);
                apply_pending_actors(scene.players, world, pending, frame, costumes_);
                scene.objects = object_sprites(world, scene.players, frame, arriving_god);
            }
        }
        hide_objects(scene.objects, [this](board::Point node, std::uint8_t kind) { return throw_pending(node, kind); });
        // The acting player as the movie shows them (the world already moved or hid them).
        using Actor = EffectSpec::Actor;
        const auto mode = play.spec.actor;
        if ((mode != Actor::world || truck) && event.player < world.players.size()) {
            const auto slot = world.players[event.player].state.slot;
            std::optional<board::PlayerSprite> sprite;
            for (const auto &player : scene.players)
                if (player.slot == slot)
                    sprite = player;
            std::erase_if(scene.players, [&](const auto &player) { return player.slot == slot; });
            if (((mode == Actor::leave && !committed) || mode == Actor::stay) && event.from) {
                if (!sprite) {
                    const auto &state = world.players[event.player].state;
                    sprite = board::PlayerSprite{{}, state.character, state.direction, 0,
                                                 static_cast<std::uint8_t>(state.traffic_method & 3U),
                                                 ui::player_color(state.color), slot, true};
                    sprite->special_pose = event.player < 4 ? costumes_[event.player] : 0;
                }
                sprite->position = {event.from->x, event.from->y};
                sprite->injured = sprite->injured || play.spec.injured;
                scene.players.push_back(*sprite);
            } else if (mode == Actor::arrive && committed && sprite)
                scene.players.push_back(*sprite);
            else if (truck && sprite) { // facing the building, demolish swing pose 12
                sprite->direction = static_cast<std::uint8_t>(event.object_kind & 7U);
                sprite->special_pose = 12;
                sprite->tossing = sprite->walking = false;
                sprite->animation_frame = static_cast<std::uint8_t>(pose_frame);
                scene.players.push_back(*sprite);
            }
        }
        board.render(canvas, scene.view(), camera);

        // ---- picture ----
        if (event.kind == EffectKind::god_leave) {
            const auto start = board::project(actor, camera);
            if (!start || event.object_kind == 0 || event.object_kind > 20)
                return;
            // The carried god's spot (board renderer `near` trailing offset) and frame (d + 4) & 7.
            static constexpr std::array<std::pair<int, int>, 8> near{
                {{-10, -22}, {-22, -10}, {-22, 10}, {-10, 22}, {10, 22}, {22, 10}, {22, -10}, {10, -22}}};
            const auto d = std::size_t((event.direction + 8 - rotation) & 7U);
            const auto frame = (d + 4 + std::size_t(frame_index) + 1) & 7U;
            const auto &sprite = artwork_.frame('d', 0x18c + event.object_kind - 1U, frame);
            const int x = start->x + near[d].first, y = start->y + near[d].second - 10 * (frame_index + 1);
            if (y - sprite.origin_y + sprite.height < 40) { // the original stops once the god left the board
                play.elapsed = 24U * 60U; // finishes through the normal path on the next draw
                return;
            }
            graphics::blit(canvas, sprite, x, y, viewport);
            return;
        }
        if (!play.movie) {
            const auto &point = play.path[static_cast<std::size_t>(frame_index)];
            if (event.kind == EffectKind::card_throw) { // Data 415 frame 0
                graphics::blit(canvas, artwork_.frame('d', 0x19f, 0), point.x, point.y, viewport);
                return;
            }
            std::uint8_t direction = 0;
            for (const auto &object : world.objects)
                if (object.node_id == event.node && object.kind == event.object_kind)
                    direction = object.direction;
            const auto &sprite = artwork_.frame('d', 0x18c + event.object_kind - 1U,
                                                static_cast<std::size_t>((8 - rotation + direction) & 7U));
            graphics::blit(canvas, sprite, point.x, point.y, viewport);
            return;
        }
        if (truck && frame_index < 0)
            return; // the puff has not started yet
        int x = play.spec.x, y = play.spec.y; // 440x440 movies cover the viewport (= project(target) - (220,220))
        if (event.kind == EffectKind::demon_puff || truck) {
            // Building anchor approximated by the projected plot point (spec §9.7, UNVERIFIED).
            const auto anchor = board::project({event.target.x, event.target.y}, camera);
            if (!anchor)
                return;
            x = anchor->x - play.movie->width() / 2;
            y = anchor->y - play.movie->height() / 2;
        }
        graphics::blit(canvas, play.frame, x, y, viewport);
    } catch (const std::exception &error) { // A broken asset must never stop the game.
        std::cerr << "Effect skipped: " << error.what() << '\n';
        current_.reset();
    }
}

} // namespace rich4::native
