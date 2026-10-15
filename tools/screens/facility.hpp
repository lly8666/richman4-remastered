// SPDX-License-Identifier: GPL-3.0-or-later
// Original facility-type picker (docs/ui/facility-build.md): fcn_00440aac + window proc
// fcn_0043fae4. The Data.mkf 517:5 message box with "請選擇設施類別" and the Data.mkf 517:4 strip of
// five 68x68 icons (公園, 旅館, 購物中心, 加油站, 研究所). Used when a human builds on an owned
// empty facility lot (landing, construction company, 天使卡, 福神, magic house, 機器工人) and by
// 改建卡 and research institute tool choices. Touch: the first tap selects a slot (yellow
// outline and item name in the original Data 517:5 box); a second tap or 確定 commits.
// Research reuses the Panel 11:1 original tool tray and Panel 11 tool sprites, with a
// separate 稍後研究 button. 改建卡 alone has its original right-click cancel.
#pragma once

#include "draw.hpp"
#include "rich4/game.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>

namespace rich4::native::facility {

inline constexpr int confirm_button = 207; // 確定 (same id as the map-target confirm)

// construct_facility, research_tool, and 改建卡's facility type choice.
[[nodiscard]] bool handles(const GameDecision &decision);

class Picker {
  public:
    // `key` identifies the decision instance; a new key clears the selection. Option ids stay
    // 210 + option index (slot taps and cancel/postpone), so the replay harness keeps working.
    void draw(screens::Host &host, const GameDecision &decision, const std::string &key,
              const std::function<void(std::size_t)> &respond);
    // True when the decision has a cancel option (改建卡: the original's right-click).
    [[nodiscard]] static bool cancellable(const GameDecision &decision);
    // Android Back / Escape: answers the cancel option with Effect 4.
    void cancel(screens::Host &host, const GameDecision &decision, const std::function<void(std::size_t)> &respond);
    // Gallery: preselects a facility type or research tool slot (0..4).
    void select(std::optional<int> slot, std::string key) {
        key_ = std::move(key);
        selected_ = slot;
    }

  private:
    std::string key_;
    std::optional<int> selected_; // facility type / research tool slot 0..4
};

} // namespace rich4::native::facility
