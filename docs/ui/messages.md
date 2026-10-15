# 原版 UI 规格：语音气泡、消息框、是/否、新闻/命运/神明公告

Scope: speech bubbles, generic message boxes and yes/no prompts, the full-screen news and fortune
screens, god arrival, hospital/prison/bankruptcy notices and money/land feedback. This spec covers the
original 大富翁4 (Rich4) only, so it can be rebuilt in the native SDL3 port.

Sources: the pinned disassembly in `.local/upstream/asm` (paths below are relative to it) and the C
re-implementations in `.local/upstream/csrc` (`drawing.c`, `font.c`, `news.c`, `fortune.c`). String and
table contents were read from `game-assets/rich4.exe` at the given virtual addresses (Big5). Frame sizes
and anchors were read from the decoded MKF entries.

Conventions:

- `Data.mkf 517:6` means archive `Data.mkf`, entry 517 (0-based, the same index as `read_mkf` and the
  contact-sheet labels), frame 6.
- **Anchor rule.** Every sprite blit (`fcn_00456418` transparent, `fcn_004563f5` opaque →
  `_draw_*_image_in_rect`) puts the frame's `(x,y)` hotspot at the given point. The top-left is
  `point − (frame.x, frame.y)`. "Anchor (220,130)" therefore means the hotspot, not the top-left
  corner. The top-left is given as well.
- All coordinates are in the 640×480 screen. The board viewport is x 0–439, y 40–479. The toolbar is
  y 0–39 and the side panel is x 440–639.
- Pixel value 0 is transparent (RGB555). Raw 388×251 photos are RGB555.
- "UNVERIFIED" means the item was inferred but not proven from code or a capture.

Reference images are in `.local/ui-ref/messages/` (local only, contains commercial art):

| File | Content |
| --- | --- |
| `data-517-519.png` | Contact sheet of Data.mkf 517 (bubble, dialog box, info tips, step digits), 518 (player chooser), 519 (金貝貝 pictograms) |
| `data517-f6-speech-cloud.png`, `data517-f5-message-box.png`, `data440-f0-yesno.png`, `data519-f3-pictogram.png` | Single frames |
| `data-440-yesno.png` | Yes/no frames 0–2 (normal, YES lit, NO lit) |
| `map-27-29-portraits.png` | map.mkf portraits (entry 27 + character), frames 0–6 |
| `panel-66-news.png`, `panel66-f0-news-bg.png`, `panel66-f1-fortune-bg.png` | News background (frame 0) and fortune background (frame 1) |
| `news-photos-441-476.png` | Data.mkf 441–476 (news id 0–35), shown at half size in 6 columns |
| `fortune-photos-477-516.png` | Data.mkf 477–516 (fortune photos), shown at half size in 8 columns |
| `news-photo-441.png` | One full-size news photo |
| `panel-67-71.png` | Slot machine (67) and cupid wheels (68–71) used by god and service spinners |
| `mock-speech-bubble.png`, `mock-speech-bubble-pictogram.png`, `mock-yesno-dialog.png`, `mock-news.png` | Mockups composed from the exact anchors below over the reference screenshot. Red/yellow boxes are text areas and the green box is the save/restore rectangle. |

---

## 0. Shared text renderer (`_rich4_create_font` / `_rich4_draw_text`)

Source: `rich4_text_drawing.asm` `_rich4_create_font` (0x44f9d8) and `_rich4_draw_text` (0x44fabc);
`csrc/font.c` `create_some_font` and `draw_some_text`.

`create_font(size, color1, color2, flags, extra)`:

- Font: typeface 細明體 (MingLiU), Big5 charset 0x88, height `-size`.
- Weight: 400 when `flags == 2`, otherwise 700 (bold).
- Spacing: `SetTextCharacterExtra(extra − 1)`.
- `flags & 1`: drop shadow in `color2` at (+1,+1), main text at (0,0).
- `flags & 4`: 4-direction outline in `color2`, main text at (1,1).
- `flags & 8`: alternate blit routine (UNVERIFIED purpose).

`draw_text(dst, str, x, y, mode)`:

- **Voice prefix.** If `str` starts with `#NNNN`, voice clip `Speaking.mkf` entry NNNN is played and the
  5 characters are skipped.
- **Line breaks.** The text is laid out with `DrawTextA` without `DT_WORDBREAK`. There is no automatic
  wrapping: lines break only at `\n`, and `\n\n` gives a blank line.
- **Text surface.** The off-screen text surface is 512×200, which bounds one text block.
- **Bounding box.** The final ink bounding box (w,h) is measured from the pixels, then placed by `mode`:

| mode | placement of (x,y) | line alignment |
| --- | --- | --- |
| 0 | top-left | left |
| 1 | top-right | left |
| 2, 3, 4 | centre (x−w/2, y−h/2) | 4 = centred lines (`DT_CENTER`), 2/3 = left |
| 5 | left edge, vertical centre (y−h/2) | left |
| 6 | right edge, vertical centre | left |
| 7 | bottom-centre (x−w/2, y−h) | centred lines |

---

## 1. Character speech bubbles (`_rich4_player_say`)

Code: `rich4.asm:23534` `_rich4_player_say(player, mood, str)` (0x44ef41), decompiled in
`csrc/drawing.c:286` `player_say`. Callers: `rich4_player_say_on_events.asm` helpers plus about 60
direct call sites (cards, tools, gods, news, fortune, hospital, prison, bankrupt).

### 1.1 Gating

- **Repeat suppression.** If `str` is the same pointer as the previous call (static 0x4762c8), nothing
  is shown. The same line twice in a row is shown once.
- **Absent players.** Nothing is shown when the player is disappearing (+0x33), dream-walking (+0x37)
  or in winter sleep (+0x36).
- **Camera.** Unless `player | 0x8000` is passed, the camera first centres on the player through
  `fcn_0041d476(x, y, 0)` (core_actions:5227). The bubble is **not** positioned relative to the sprite.
  It is fixed on screen, and the camera moves so the tail points at the player standing near the
  viewport centre.

### 1.2 Layout (absolute, fixed)

| Element | Asset | Anchor | Resulting rect |
| --- | --- | --- | --- |
| Cloud | `Data.mkf 517:6` (271×199, hotspot 127,92) | (220,130) | x 93–363, y 38–236. Rows 38–39 fall outside the blitted area (see below). |
| Portrait | `map.mkf (27+character):(mood+1)` (about 40×34, hotspot about 20,17) | (170,130) | about x 150–190, y 113–147 |
| Text | 16 px, `create_font(0x10, 0x101010, 0, 2, 1)`: regular weight, near-black #101010, no shadow, extra spacing 0 | (200,130), mode 5 | left edge x=200, vertically centred on y=130, lines left-aligned, explicit `\n` only |
| Pictogram (instead of text) | `Data.mkf 519:(NN−1)` when the text after `#NNNN` is `@NN` | (240,130) | Used only by character 11 (金貝貝), whose lines are all pictograms |

- **Portrait source.** map.mkf entries 27–38 are the 12 characters (loaded in
  `rich4_load_map.asm:487`, entry `char + 0x1b`). Frame 0 is the large face. Frames 1–4 are moods 0–3,
  and frames 5–6 are tiny icons.
- **Blitting.** Only rect (0,40)–(440,260) is saved (`crop_graph(…, 0, 40, 440, 220)`) and blitted to
  the screen. The bubble is a modal overlay on the upper map area. The game loop is blocked while it is
  shown.

### 1.3 Timing and voice sync

- **Order.** The voice plays first (the `#NNNN` prefix → `fcn_0045441a(NNNN)`, Speaking.mkf), then the
  bubble is blitted.
- **Hold time.** `fcn_004544f6(1000)` (rich4_sound_effect.asm:915) waits until the voice buffer stops,
  then pads to at least 1000 ms. Left/right mouse-up or key-up skips the wait and stops the voice. With
  sound effects off (`cfg+3 == 0`), the hold is a fixed 1000 ms. So the bubble is shown for
  `max(voice length, 1000 ms)`.
- **Restore.** The saved background is restored and blitted, so the bubble disappears instantly with no
  fade.

### 1.4 Text source: `_rich4_event_strings` (0x48084a)

- **Table shape.** 12 characters × 27 slots of `char*` (stride 108 bytes). Every entry is
  `#NNNN` + Big5 text, with NNNN = `1050 + character*27 + slot`. This equals `Audio::play_voice`
  in `tools/native_audio.cpp:322`.
- **Other tables.** Cards (`_rich4_card_strings`, 0x48123a, 90 slots per character, voices 0426–1049)
  and tools (`_tool_strings`, 0x480d5a, 26 slots per character, voices 0234–0425) use the same bubble
  but other tables. Their slot meaning is set by each `rich4_card_*`/`rich4_tool_*` file.
- **Mood.** The mood argument, and so the portrait frame, is set by the caller. Values are taken from
  the cited call sites.

| slot | meaning (helper or call site) | mood (portrait frame−1) | selection rule |
| --- | --- | --- | --- |
| 0–2 | lucky gain (`fcn_0044f230(p, v)`; prize, card or points value v) | 0 | v>100 → 0; v>50 → rand 0/1; v>0 → 2 |
| 3–5 | bad luck / days lost (`fcn_0044f2c2(p, days)`) | 2 | >6 → 3; >3 → rand 3/4; >0 → 5 |
| 6–8 | received money (`fcn_0044f354`) | 3 | ≥9000·PI → 6; ≥5000·PI → 6/7; ≥2000·PI → 8 (else silent) |
| 9–11 | paid money (`fcn_0044f42d`) | 2 | ≥9000·PI → 9; ≥5000·PI → 9/10; >0 → 11 |
| 12–14 | avoided a payment (`fcn_0044f567`, core_actions:5441) | 3 | same thresholds as 9–11 |
| 15 | building reached level 5 / proud (core_actions:454, 1854; gods:1684) | 0 | fireworks follow (§4.2) |
| 16 | bought land, owning ≥3 on the same street (`fcn_0044f627`, flag 0) | 0 | always |
| 17 | upgraded land, owning ≥3 on the street (`fcn_0044f627`, flag 1) | 0 | rand%3==0 |
| 18 | revenge line when paying the most hostile owner (`fcn_0044f4ed`) | 1 | owner is most hostile, amount ≥5000·PI, rand&1 |
| 19 | in prison (prison_utils:141, rich4.asm:6651) | 2 | |
| 20 | in hospital (hospital_utils:141, rich4.asm:6689) | 2 | |
| 21 | asleep (winter sleep; card_mengyouka:152, rich4.asm:6729) | 1 | |
| 22 | bad god attached (gods:829…1155) | 2 | spoken **before** the god animation |
| 23 | god left (gods:404) | 2 | |
| 24 | winner (bankrupt:293, core_actions:5683) | 3 | |
| 25 | lost / bankrupt (bankrupt:484) | 2 | |
| 26 | `rich4_new_game.asm:4684` ("我要再接再勵" etc.) | 3 | Context UNVERIFIED (probably game-over replay prompt) |

PI = price index (`_rich4_price_index`). The full table text was dumped from the EXE. For example,
character 10 (小丹尼): slot 0 "#1320帥呆了！", slot 9 "#1329太貴了！", slot 19 "#1339讓我出去！".

**Card and tool lines** (implemented: `SpeechTable::card/tool` events, `messages::item_speech_bubble`).
`_rich4_card_strings[char][row][card-1]` (slot = row·30 + card−1): row 0 the user, row 1 the user
targeting itself, row 2 the victim. `_tool_strings[char][slot]`: slot = tool−1, plus 14 roadblock hit,
15 mine hit, 16 time bomb picked up (`rich4_player_core_actions.asm:3277/3398/3493`, moods 1/1/2). No
line choice uses `rand()`. Call order, from each handler (popup → card function, so the user always
speaks after the card art):

| card / tool | user line (mood) | then | victim line (mood) |
| --- | --- | --- | --- |
| 1 均富, 15 冬眠, 9 天使 | row 0 (3), after consuming | — | — |
| 2 均貧 | row 0 (3) | cash split | target row 2 (1) |
| 3 購地 | row 0 (3), before the owner change | payment | seller row 2 (1) |
| 4 換地, 5 換屋 | row 0 (3) only if the swap favours the user | swap | other owner row 2 (2) |
| 6 轉向 | row 0 (3) | turn | self: row 1 (0); player: row 2 (2) |
| 7 改建 | row 0 (3), before the facility type pick | — | — |
| 8 拍賣 | row 0 (3) | owner ≠ user: row 2 (1) | then the auction |
| 10 惡魔, 22 送神 (after removal), 23 請神 (before attach), 24 紅, 25 黑 (before the stock pick), 27 漲價, 28 查封 | row 0 (0) | — | — |
| 11 怪獸, 12 拆除 | row 0 (0) | Data 557 / 529 movie | owner row 2 (1) |
| 13 搶奪 | row 0 (3), before the item pick | steal | target row 2 (1) |
| 14 停留, 30 烏龜 | row 0 (3) if target ≠ user | — | self: row 1 (3); player: row 2 (2) |
| 16 夢遊 | row 0 (3) | 免罪/嫁禍 | victim **event** slot 21 (1), then 復仇 |
| 17 陷害 | row 0 (3) | 免罪/嫁禍, jail | victim row 2 (1), then 復仇 |
| 26 查稅 | row 0 (0) | 免費/嫁禍, payment, box | payer row 2 (2) |
| 29 同盟 | row 0 (3) | alliance | target row 2 (0) |
| 18 復仇 / 19 嫁禍 / 20 免費 / 21 免罪 (`rich4_card_passive.asm`) | holder row 0 (0) after popup | — | 18: user row 2 (2); 19: framed row 2 (2); 20: payee row 2 (1) |
| tools 1–13 | slot tool−1 (0) | throw / movie / effect | — (remote dice: human only) |

Character 6's card lines #0752/#0753 end with the Big5 user-defined glyph 0x9DDD, which has no
Unicode mapping and is dropped in the port.

### 1.5 Second popup type (`fcn_0044ec30` / `fcn_0044ecb6` / `fcn_0044ee18`)

This popup is used inside facility screens (bank, shop, hospital, prison, lottery, auction, magic house,
monthly report). It is a clerk's speech in any sprite at any position:

- **Setup.** `fcn_0044ec30(sprite, x, y, textdx, textdy, color, shadow)`.
- **Text.** 20 px, flags 3 (with shadow) or 2 (without), mode 4, centred at
  `(top-left + w/2 + textdx, top-left + h/2 + textdy)`.
- **Timing.** It is non-blocking. `fcn_0044ee18` is polled and removes it once the voice has finished,
  or after 2000 ms, or immediately when `arg & 1`.

These screens belong to the respective facility specs. Positions are per call site.

---

## 2. Message boxes and yes/no

All three boxes share one dialog artwork: **`Data.mkf 517:5`** (249×170, brown board with a jewelled
gold crown, hotspot 123,101), anchored at **(220,140)** → x 97–345, y 39–208. The box rect is
precomputed in `rich4.asm:11968` (`ref_0048bdb8..c4`). The board viewport (0,40)–(440,480) is saved
before drawing and restored afterwards (`fcn_00451e7e` / `fcn_00451edb`).

The text style is the same for all three: `create_font(0x10, 0xf0f0f0, 0x101010, 3, 1)`. That is
16 px, bold, white #F0F0F0 with a #101010 drop shadow at (+1,+1). Text uses mode 4: centred at
(220,140), each line centred, `\n` line breaks only.

### 2.1 Timed message box `fcn_00440cac(text, ms)` (rich4.asm:21601)

- **Placement.** Box at (220,140) and text centred at (220,140). With `ms | 0x80000000`, both are
  shifted 100 px right (anchor 320,140).
- **Hold.** `fcn_004528b9(ms)` waits ms, skippable with left/right mouse-up or key-up. It does **not**
  wait for voice. Then the box is restored.
- **Durations used.** 1500 ms (0x5dc) almost everywhere, 1000 ms for point coupons and theft, 2000 ms
  for bank robbery.

### 2.2 Yes/no question `fcn_00440ba8(text)` (rich4.asm:21522) + `_rich4_ui_yesno(cx, cy)`

- **Question.** Box and text as above. The question text is blitted first.
- **Yes/no strip.** `_rich4_ui_yesno(220, 320)` (`rich4_ui_yesno.asm`) loads `Data.mkf 440`
  (3 frames, 96×48, hotspot 0,0). The strip is centred at (220,320) → top-left **(172,296)**,
  opaque blit.
  - Frame 0: both buttons idle ("YES" pink, "NO" teal, gold binder ring in the middle).
  - Frame 1: YES highlighted.
  - Frame 2: NO highlighted.
- **Hover.** Hit zone = frame rect, `index = 1 + (mx − 172) / (w/2)` → left half YES, right half NO.
  Entering a new half plays Effect 0. Leaving the strip returns to frame 0.
- **Initial cursor.** The mouse is warped to (left+22, top+22), i.e. onto YES.
- **Result.**
  - Click YES → 1 (Effect 2).
  - Click NO → 0 (Effect 4).
  - Right-click → 0 (Effect 4).
  - The configured YES/NO hotkeys (`cfg+0x20`, `cfg+0x22`) do the same.
- **Callers and texts.** All human-only. AI skips the prompt (core_actions:395, flag `+0x15 == 1`).

| Situation | Text (format) | asm |
| --- | --- | --- |
| Buy land | `"%s\n\n費用:%d元\n\n是否買下此地？"` (land name, cost) | core_actions:1003, 1653 |
| Upgrade | `"%s\n\n升級費用:%d元\n\n是否升級？"` | core_actions:1185 |
| Subscribe to new shares | `"%s\n\n每股售價%d\n\n是否認購股份？"` | core_actions:4994 |
| Others: hospital/prison bail, sale, options, quit (keyboard hook) | Per UI | `rich4_ui_hospital/prison/sale/options`, `rich4_keyboard_hook` |

- **After YES on buy/upgrade.**
  - Effect 0x31 (buy) or 0x32 (upgrade) plays.
  - Cash is deducted and the camera is refreshed.
  - The bubble from §1 follows (slot 15/16/17).
  - At level 5, fireworks play (§4.2).
- **Not enough cash.** A 1500 ms box with "您的現金不足！" is shown instead (core_actions:1082). Whether
  AI players also reach this branch is UNVERIFIED.

### 2.3 Other dialog-box variants (same frame 5 and font)

- `fcn_00441f73` (rich4.asm:22062), card received:
  - Box at (220,129), text mode 4 at (220,129).
  - Card picture opaque at (138,200).
  - Used by holiday gifts and gods.
- `fcn_00440e1a`, choose a player:
  - Prompt at (220,140).
  - Chooser panel `Data.mkf 518:(n−2)` (n = number of players) at (220,320).
  - Faces from `Data.mkf 2:character` are placed at panel offsets x 12+80k, y 12.
- `fcn_00440aac`, choose a facility type:
  - Text "請選擇設施類別" mode 2 at (220,122).
  - Icon strip `Data.mkf 517:4` opaque, top-left (43,279).
  - Full spec, flows and native mapping: `facility-build.md`.
- Spinners:
  - `fcn_00440706`: slot machine `Panel.mkf 67` at (220,320).
  - `fcn_0044090e`: cupid wheel `Panel.mkf 68+k` at (220,320) and (265,230).
  - Both show texts such as `"%s附身\n\n送您..."` or `"%s的旅館\n\n請進來休息..."` in the box.
  - Native: `DecisionKind::money_spinner` / `roulette` (human) and `EffectKind::money_spinner` /
    `roulette` (computer or sleep-walking spins, and the wheel's slow-down after STOP), drawn by
    `tools/screens/spinners.cpp`.
- **Slot machine** (`fcn_00440706(kind−1)` + loop `fcn_0043f23e`). Gods 小財神 1, 大財神 2,
  小窮神 5, 大窮神 6 (rich4_gods.asm:582/667/863/932), after the appearance FLC and caption.
  - Box 517:5 at (220,140), text mode 4: `"%s附身\n\n向所有對手收..."` (1), `"…送您..."` (2),
    `"…付給每個人..."` (5), `"…損失..."` (6); `%s` = `_rich4_god_names[kind−1]`.
  - Machine Panel 67 frame v opaque at (220,320), v = ((kind−1)&1)^1: frame 1 (3 reels) for kinds
    1/5, frame 0 (4 reels) for 2/6. Lever 67:2 at (x,240), x = 317 (v 0) / 298 (v 1) (`0x475ce0`).
  - Reels 67:(4+pos) opaque at y 320, x = 145,182,219,256 (v 0) / –,163,200,237 (v 1) (`0x475ce8`);
    pos 0..19 = digit·2, odd = between digits.
  - 30 ms frames. Every 10th frame while spinning re-randomises all reels (`rand()%10*2+1`). STOP =
    any L/R mouse-up or key-up in phase 1 (human, not sleep-walking); computers stop at frame 40.
    STOP draws the pulled lever 67:3 and plays Effect 1; phase 3 frame 4 restores 67:2. The reels
    then stop right to left (slowing periods). Rattle: Effect 51 looped from start until the last
    reel stops; then the box shows `"%d元"` for 40 frames (no skip).
- **Cupid wheel** (`fcn_0044090e(type, name)` + loop `fcn_0043f7c6`). Types: 0 travel (company,
  `"%s\n\n送您出國旅遊..."`), 1 hotel (`"%s的旅館\n\n請進來休息..."`, owner), 2 department store
  (`"%s的購物中心\n\n您的消費倍數為..."`, owner), 3 insurance (`"%s\n\n您的投保天數為..."`, company).
  - Wheel Panel (68+type):(2+cursor) at (220,320) (transparent), cupid frame 0 at (265,230).
  - Initial cursor `rand()%12`; 36 ms frames, one sector per frame; Effect 52 looped.
  - STOP (human click; computers at frame 40): cupid frame 1 (arrow shot) drawn over the wheel,
    Effect 1. Slow-down: 3 sectors each every 1, 2, 3, 4 frames, then (period ≥ 5) stop on the first
    non-empty sector of table `0x475d0c` (= core `roulette_value`); the loop sound stops; the result
    holds 40 frames, a human click ends it. The caller then shows the toll box.
  - Computers see both spinners as well; only a human (not sleep-walking) can stop or skip them.

### 2.4 Message box texts in normal play (all 1500 ms unless noted)

| Situation | Text |
| --- | --- |
| Pay toll | `"%s\n\n此地屬%s\n\n請付%d元過路費"`, or `"%s\n\n屬%s與%s\n\n請付%d元過路費"` (allied owners) |
| Company fees | `"%s\n\n董事長%s\n\n請付%d元%s"`, `"%s\n\n幫主%s\n\n請付%d元%s"` |
| Toll waived | `"房屋查封中\n\n免收%s！"`, `"與%s同盟中\n\n免收%s！"`, `"死神顯靈\n\n免收%s！"`, `"%s住宿中\n\n免收%s！"` (also 消失/坐牢/住院/冬眠/夢遊中) |
| God modifiers | `"小財神顯靈\n\n%s減免一半！"`, `"大財神顯靈\n\n免付%s！"`, `"小窮神顯靈\n\n%s加付50％！"`, `"大窮神顯靈\n\n加倍付%s！"` |
| Death god | `"死神顯靈\n\n由%s賠償%s"` |
| Hotel / mall | `"休息%d天\n\n費用%d元！"`, `"您的消費金額為\n\n%dx%d倍=%d元"` |
| Point coupons (1000 ms) | `"得點券５０點"`, `"得點券３０點"`, `"得點券１０點"`. Before the box, AF12 `Data.mkf 0x219` plays at (204,180) with sound 0x62. |
| Mini-game closed mode (2000 ms) | `sprintf("得點券%d點")` with half-width digits, 50..69 (rich4_small_games.asm:4385–4427, `loc_00415457`), then `_rich4_event_strings[character][rand()&1]`. Not shown after an interactive game (its end screen holds the big score). Native: `Announcement::mini_game_points`. |
| Other | `"得到%s！"`, `"小偷偷得%s\n\n給%s！"`, `"施捨給乞丐%d元"`, `"不用出國！"`, `"%s\n\n請選擇欲加蓋地點"`, `"恭喜您成為幫主！"`, `"恭喜您獲得經營權！"`, `"%s開發完成！"` |
| Robbery and theft | `"強盜搶奪銀行\n\n得款%d元\n\n給%s！"` (2000 ms), `"勒索%s\n\n%d元保護費！"`, `"取走過路費\n\n%d元！"`, `"取走盈餘\n\n%d元！"`, `"偷取%s\n\n%d點點券！"` (1000 ms), `"奪取%s%s！"` (1000 ms) |
| Turn-start status (rich4.asm:6600–6757, `fcn_0040c912`) | `"%s住宿中\n\n還剩%d天！"`, `"%s消失中…"`, `"%s坐牢中…"`, `"%s住院中…"`, `"%s冬眠中…"`. Each is preceded by the matching bubble (slot 19/20/21). |
| Insurance | `"保險期間\n\n得到理賠金\n\n%d元"` (rich4.asm:22922) |
| Stock trade | `"%s\n\n買進%s%d張"`, `"%s\n\n賣出%s%d張"` (rich4.asm:15335, 16035) |
| Holidays (rich4.asm:26186–26210, `fcn_00452444`) | `"銀河系和平日\n\n%s得到%s！"`, `"恐龍蛋節…"`, `"除夕…"`, `"聖誕節…"`. Shown with the card-received box from §2.3. |

---

## 3. Full-screen announcements

### 3.1 News (`fcn_0044b6df`, `rich4_news.asm:3451`; `csrc/news.c` `news_events`)

- **Selection.** Walk the shuffled deck `0x499090[cursor]` and skip ineligible ids (`fcn_00448be2`).
  The cursor wraps at 36.
- **Background.** `Panel.mkf 66:0` (440×480, blue "NEWS" pattern with a white photo frame), opaque at
  (0,0). It covers the **whole left area including the toolbar**: rect (0,0)–(440,480). The side panel
  stays visible.
- **Photo.** `Data.mkf (441 + newsId)` is a raw 388×251 RGB555 image, copied to (25,44) inside the panel.
- **Title.**
  - Font 28 px, `create_font(0x1c, 0xf0f0f0, 0x101010, 3, 0)`: bold white with shadow.
  - Placed at (24,8), mode 0 (top-left).
  - Text = `0x475ed8[0x475eb4[newsId]]`, the category name (news id ranges):

| ids | title |
| --- | --- |
| 0–5 | 無責任新聞 |
| 6–13 | 政府公告 |
| 14–15 | 社會新聞 |
| 16–17 | 路況報導 |
| 18–21 | 氣象報導 |
| 22–35 | 財經新聞 |

- **Body.**
  - Drawn by the per-id handler `0x475e24[newsId](0)`.
  - Same 28 px white bold font, at (24,310), mode 0 (top-left), `\n` breaks.
  - Each body starts with `#0149`…`#0184`, which plays the announcer voice from Speaking.mkf. For
    example `"#0154外星怪獸襲擊%s\n摧毀建築一棟"`.
- **Affected-player rows.**
  - Prison/hospital news: portrait `map.mkf (27+char):4` at (390, 328 + 42·k), one per affected player.
  - Tax news (`#0160–0162`):
    - Font switches to 24 px (`0x18`) with the same style.
    - Lines `"%s繳交%d元"` at (24, 346 + 32·k).
    - Portrait frame 3 at (390, 358 + 32·k).
  - Bank bonus `#0172`: `"%s得到%d元"` uses the same layout.
- **Sequence.**
  1. Blit and hold `fcn_004544f6(2400)`: at least 2400 ms and until the announcer voice ends; click or
     key skips.
  2. Call `handler(1)` to apply the effect. Effects may play map animations (`fcn_0045144f`,
     `Data.mkf 0x213/0x21b/0x20f/0x216/0x217`) and bubbles (slot 3/4, mood 2) over the restored map.
  3. The panel is freed, and the map redraw restores the screen.
- **Mockup.** `mock-news.png`.

### 3.2 Fortune 命運 (`rich4_fortune.asm:2529` `fcn_0044db81`; `csrc/fortune.c` `fortune_events`)

- **Background.** `Panel.mkf 66:1` (pink "?!" pattern) at (0,0). There is **no title** text.
- **Photo.**
  - `Data.mkf fortune_data_idx[id]` (raw 388×251) at (25,44).
  - The table covers ids 0–48 → entries 477–516 (some repeat).
  - Ids ≥33 are map-specific: `game_map*4 + t`.
- **Body.**
  - 28 px white bold with shadow, at (24,330), mode 0.
  - Voices are `#0185…#0233`, e.g. `"#0185強制拆除房屋一棟"`.
  - Current player's portrait `map.mkf (27+char):2` at (390,344).
- **Sequence.**
  1. Blit (0,0)–(440,480).
  2. Hold `fcn_004544f6(1600)`: at least 1600 ms and until the voice ends; skippable.
  3. `handler(1)` applies the effect. It often shows a follow-up 1500 ms message box (built in buffer
     0x48c5b8) and a bubble (slot 3/5, mood 2).
  4. `fcn_004528b9(800)` waits a final 800 ms (skippable).
- **Birthday fortune (`fcn_0044c3b7`).** Extra 20 px lines `"向%s收一張卡片"` at (24,10), drawn on the map
  afterwards (UNVERIFIED exact flow).

### 3.3 Gods arriving (`rich4_gods.asm`)

The attach handler for god kind k (`_rich4_god_names`, 0x47ed7a, order: 小財神, 大財神, 小福神,
大福神, 小窮神, 大窮神, 小衰神, 大衰神, 天使, 惡魔, 惡犬, 土地公) runs these steps:

1. **Bad gods only.** 窮神, 衰神 and the 0x227 god speak first: bubble slot 22, mood 2.
2. **Animation.** When the animation option is on (`cfg+1`), the AF12 animation `Data.mkf 0x21c+i` plays
   at (0,40) over the board (440×440, flags 1).

| entry | handler | speech text start | sound |
| --- | --- | --- | --- |
| 0x21c | 小財神 | 呦喝！∼發財發財 | 0x66 |
| 0x21d | 大財神 | 呵呵∼恭喜發財 | |
| 0x21e | 小福神 | Ｙｅａｈ!! | |
| 0x21f | 大福神 | 呵呵呵∼天官賜福 | |
| 0x220 | 小窮神 | 噎噎噎∼賠錢 | |
| 0x221 | 大窮神 | 喔∼錢掉了 | |
| 0x222 | 小衰神 | 有我小小衰神 | |
| 0x223 | 大衰神 | 哈哈哈！真是好衰啊 | |
| 0x224 | 天使 | 哈雷路亞∼ | |
| 0x225 | 惡魔 | 哈∼哈∼與我同生共死 | |
| 0x226 | 土地公 | 呵∼呵，有土斯有財 | |
| 0x227 | probably 惡犬 (UNVERIFIED, inferred from the line "被我盯上，你完蛋了") | 嘿嘿嘿嘿… | |

   Only the sound for 0x21c (0x66) was recorded. The sounds of the other god animations were not
   collected.

3. **Arrival speech (`fcn_0040e2a2`).** Drawn directly on the screen over the last animation frame:
   - Font 28 px, `create_font(0x1c, 0xffffff, 0x101010, 6, 0)`: bold white with a 4-direction #101010
     outline.
   - Mode 7: bottom-centre at **(220,460)**, lines centred.
   - Hold 2400 ms (`fcn_004528b9`, skippable). There is no voice.
4. **Follow-ups.**
   - Message boxes such as `"%s附身\n\n得到%s！"`, `"大福神附身\n\n得到%s及%s！"`,
     `"小衰神附身\n\n遺失%s！"`, `"大衰神附身\n\n遺失一半卡片！"`.
   - Spinner dialogs for 財神/窮神 (§2.3).
   - A bubble (slot 6–8 or 0–2).
   - **2026-10-15 native parity fix:** `gods::attach` now queues
     `Announcement::god_inventory` after `god_appear` and `god_arrival`.
     The overlay receives the exact awarded/lost card ID(s) without drawing RNG
     again; small/big fortune gods name one/two awarded cards, small poverty
     god names the lost card, big poverty god reports half-card loss.
     Kind-15 all-card loss also receives a readable notice, but the precise
     original kind-15 box wording has not yet been independently verified.
   - The carried god sprite must not be visible during the introduction movie
     or the 2400ms caption. The native UI suppresses only the newly attached
     god (by actual object ID and carrier), not unrelated time bombs or actors,
     until the last original intro image/caption is finished.
5. **God leaving.** Slot 23 bubble, mood 2, and Effect 0x36 (`_rich4_detach_god`).
6. **Active god on a property event.** Boxes `"%s顯靈\n\n加蓋一層房屋！"`, `"小惡魔顯靈\n\n拆毀一層房屋！"`,
   `"土地公顯靈\n\n強佔土地！"`, `"%s顯靈\n\n投資失敗！"`.

### 3.4 AF12 map animations (shared player `fcn_0045144f(data, x, y, flags, sound)`, rich4.asm:25526)

- **Format.** Entries with `0xAF12` at +4 are FLIC-like. Header fields:
  - +6 frame count
  - +8 width
  - +0xA height
  - +0x10 frame delay in ms
  - Data starts at +0x80. Frames carry magic 0xF1FA, with chunk types 4/0xC/0xF/0x10 (LC/BRUN/COPY
    style) and 16-bit pixels. Decoder: `fcn_00450f04`.
  - **The native port has no AF12 decoder (gap).**
- **Flags.**
  - bit0: draw into the back buffer.
  - bit1: click/key skips.
  - bit3: modes.
  - bits4–7: delay override ×10 ms.
  - bits16–23: non-zero → composite over the saved map using a 440×440 buffer.
  - bit31: full-screen variant.
  - Exact semantics beyond this are UNVERIFIED.
- **Sound.** The `sound` argument is an Effect.mkf index. −1 means none.
- **Entries and their use.**

| entry | size, frames, delay | use (asm) |
| --- | --- | --- |
| 0x20b | 440², 66 f, 42 ms | Fireworks: level-5 building (`fcn_0040b0cd`, sound 0x5a), New Year holiday |
| 0x20c | 440×74, 62 f, 100 ms | Hospital ambulance at (0,210), flags 0x1e0001, sound 0x5c |
| 0x21a | 440², 35 f | Prison at (0,40), flags 0x120001, sound 0x5e |
| 0x22b | 440², 10 f | Bankruptcy at (0,40), sound 0x64. Also starts music 2 and is followed by a 2000 ms wait. |
| 0x229 | 440², 68 f | Construction (加蓋 / remodel) at (0,40), flags 0x2c0001, sound 0x5b |
| 0x218 / 0x219 | 28×40 / 31×39, 14 f | Item and point-coupon pickup at (204,180), sound 0x62 |
| 0x20d, 0x214, 0x228 | | Thief, robbery and related events (core_actions:2837, 2953, 2927) |
| 0x213, 0x21b, 0x20f, 0x216, 0x217 | | News effects: aliens, monster, gas explosion, typhoon, tornado |
| 0x22a | 440², 90 f | Christmas holiday (sound 0x72, MIDI 0xd) |
| 0x20e | 110², 8 f | Small effect (gods.asm:1415) |
| 0x211 | 440², 68 f | Magic house |
| 0x20a | | Not AF12 |

- **Holiday table.** At 0x47ff4c, rows of 12 bytes: {month, day, ?, flags, anim entry u16, sound u16,
  midi?…}. Field semantics are UNVERIFIED.

### 3.5 Hospital, prison, bankruptcy and victory

- **Hospital (`_rich4_add_player_days_in_hospital`, hospital_utils:30).**
  1. Centre the camera.
  2. `fcn_0044f2c2` bubble (slot 3–5, mood 2).
  3. Move the sprite.
  4. Animation 0x20c.
  5. Centre the camera again.
  6. Bubble slot 20, mood 2.
- **Prison.** The same sequence with animation 0x21a and slot 19.
- **Bankrupt (`_rich4_player_bankrupt`).**
  1. Music 2.
  2. Animation 0x22b.
  3. Wait 2000 ms.
  4. If one opponent is left, the winner says slot 24 (mood 3). Otherwise the auction of assets
     follows, then the loser says slot 25 (mood 2).
- **Victory / game over.** Driven by `rich4_new_game.asm` (slot 26 bubble) and AVI endings
  (`End*.avi`, `Over.avi`, `rich4_media_video.asm`). The victory text "恭喜勝利過關！！" (0x463176)
  layout is UNVERIFIED and not researched here.
- **Monthly report (`fcn_00437e61`, rich4.asm:16815).** Uses voices `#0108`/`#0109`/`#0122` and §1.5
  popups. It is not covered here (UNVERIFIED).

---

## 4. Money and land feedback

### 4.1 Rent and payments: no floating numbers

`fcn_0041d2c6(payer, payee, amount, flags)` (core_actions) only moves the money. It then calls
`_rich4_update_player_info_window` for the current player, which repaints the side panel. There are
**no floating numbers or coin particles** in the original.

The toll sequence (core_actions:690–970):

1. 1500 ms message box (§2.4 toll text).
2. Payer bubble: revenge (slot 18, mood 1) or pay (slot 9–11, mood 2).
3. Owner bubble (slot 6–8, mood 3).
4. Money transfer and side-panel repaint.

Each bubble is a separate blocking modal. Each recentres the camera on its speaker.

### 4.2 Land purchase and upgrade

1. Optional yes/no (§2.2).
2. Effect 0x31 for buy or 0x32 for upgrade.
3. Cash deducted and the map redrawn, showing the new ownership marker or the new building level.
4. Speech:
   - At level 5: bubble slot 15, then fireworks 0x20b (sound 0x5a).
   - Otherwise: slot 16 for a buy, or 17 with a 1/3 chance for an upgrade, when the player owns ≥3 lots
     on the street.

Ownership markers on the map are `Data.mkf 517:22–29` silhouettes tinted with the owner colour
(`fcn_00456384`, rich4.asm:3755). They belong to the board-renderer spec.

### 4.3 Related on-map indicators (pointers only)

- **Steps-remaining counter.** Green digits `Data.mkf 517:8–17` (35×43). For `"%d"` of
  `ref_0048baf8`, each digit is drawn at x = 245 − len·25 + 50·i, y = 400 (rich4.asm:2700, `fcn_00409449`).
- **Object countdown.** `"%d"` above timed objects at (obj.x, obj.y−60), mode 2 (rich4.asm:2655).
- **Land info tips.** `Data.mkf 517:0–3` are the four tail directions (rich4.asm:9680).

---

## 5. Native mapping

Current native state: `tools/native_game.cpp` `observe()` (line 469) plays `audio_->play_voice` for
`GameEvent::voice_category`, plays effects 10/49/50/55, and appends `message` to `recent_` (side panel
text, line 765). Decisions use a generic paper dialog (`draw_decision`, line 1143). None of the elements
above are implemented.

### 5.1 Driving each element

| Element | Driver in `include/rich4/game.hpp` | Notes |
| --- | --- | --- |
| Speech bubble (§1) | `GameEvent{kind=notice, player, voice_category}` with empty `message` (from `Impl::voice`, `src/game.cpp:135`) | Text = event_strings[char][category]. Voice = 1050+char·27+category (already right). Mood from the §1.4 table, keyed by category. Hold = max(voice, 1000 ms). Queue bubbles: each is modal and blocks later events. |
| Yes/no buy/upgrade (§2.2) | `GameDecision{buy_land / upgrade_land, land_id, amount}`, options {1, 0} | Build the text from the land name (`world.lands[land_id]`) and `amount`. Do not use `prompt`, whose Simplified Chinese text does not match the original. YES → `respond(index of value 1)`. |
| Timed message box (§2.1) | `GameEventKind::rent_paid`, `notice` (`message`), `points_received`, `bank_transfer` | Only a few events carry enough data for the original texts (see 5.2). |
| News screen (§3.1) | `GameEvent{notice}` from `resolve_event(true)` (`src/game.cpp:2435`) | Needs the news id. |
| Fortune screen (§3.2) | `GameEvent{notice}` from `apply_fortune` (`src/game.cpp:2435, 2503`) | Needs the fortune id. |
| God arrival (§3.3) | `check_god` / `gods::Result.message` → `notice` | Needs the god kind and attach/detach/trigger kind. |
| Spinner dialogs | `DecisionKind::money_spinner`, `roulette` | Panel 67/68–71 in the dialog box (§2.3). |
| Hospital/prison (§3.5) | `notice` "…住院/入狱N天" (`src/game.cpp:1504`) + voice 19/20 | Play AF12 0x20c/0x21a first, then the bubble. |
| Bankrupt / finish | `GameEventKind::player_bankrupt`, `game_finished` | AF12 0x22b, music 2, bubbles 24/25. |
| Buy/upgrade feedback (§4.2) | `land_purchased` / `land_upgraded` (amount = cost) + voice 15/16/17 | Fireworks when voice 15 follows `land_upgraded`. |
| Point coupon | `points_received` (amount 10/30/50) | AF12 0x219 at (204,180) + 1000 ms box. |

### 5.2 Data the native core does NOT expose yet (to add)

1. **Event string tables.** The UI or assets layer needs 324 per-character event lines (0x48084a), card
   lines (0x48123a, 12×90) and tool lines (0x480d5a, 12×26), as Big5 data with their `#NNNN` voice ids.
   Done: all three are ported to UTF-8 in `tools/screens/message_strings.inc`.
2. **Card and tool speech events.** Done: `GameEvent::speech_table/speech_slot/speech_mood`, emitted
   through `CardCallbacks::say` and `tools::Callbacks::say` at the original call points (§1.4 table);
   tables ported to `tools/screens/message_strings.inc`.
3. **Mood.** It is a pure function of the event-category slot (table §1.4), so the UI can derive it.
   Card and tool lines pass mood 0 at the call sites checked (hongka, dilei). Other call sites are
   UNVERIFIED.
4. **Ordering and blocking.** The original sequence for rent is box → payer bubble → owner bubble →
   transfer. Native `pay_toll` (`src/game.cpp:1289`) pushes the voice events **before** `rent_paid`, and
   `rent_paid` lacks `owner`, `land_id`, the toll-kind string (過路費 / company fee), allied-owner info
   and the waiver reason. Fields are needed for: owner slot, land id, fee kind and waiver reason
   (查封, 同盟, 死神, 住宿/消失/坐牢/住院/冬眠/夢遊, 財神/窮神 modifiers).
5. **News id and fortune id in `GameEvent`.** `EventResult::event_id` exists in `include/rich4/events.hpp`
   but is dropped. The UI also needs the formatted body arguments: affected players and per-player tax
   amounts. Photos: news → Data 441+id; fortune → `fortune_data_idx[id]` (copy the 49-entry table from
   `csrc/fortune.c`).
6. **God events.** The god kind (0–11) and the phase (attach, detach, triggered effect) are needed to
   pick AF12 0x21c+kind, the arrival speech text (rich4_gods.asm strings 0x463250…0x463495) and the
   follow-up box.
7. **"Not enough cash" for buy/upgrade.** Native silently ends the turn (`src/game.cpp:661`). The
   original shows the "您的現金不足！" box. A `notice` with a reason code is needed.
8. **Turn-start status boxes** ("%s坐牢中\n\n還剩%d天！" etc.) need the status kind and days left. These
   are partially present as `notice` amount and text.
9. **Holiday celebrations.** Native has the closure rules (`holidays.hpp`) but no event for the holiday
   animation, music or gift card (`fcn_00452444`).
10. **AF12 animation decoder and player.** Required for all §3.4 entries. Must also honour the
    "animation" option (cfg byte 1).
11. **Font.** The original uses GDI 細明體 at 16, 20, 24 and 28 px with shadow/outline modes. Native uses
    FreeType, and glyph metrics will differ (already noted in STATUS.md).
