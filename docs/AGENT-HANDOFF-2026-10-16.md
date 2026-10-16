# 《大富翁4》重构 / MOD：全新开发 Agent 交接说明

> **交接基线：2026-10-16；代码快照 `mod` = `8bc6eb54c92a205001ed699786394f930c242457`。** 本文仅描述已在远端核实的 GitHub 状态，之后的文档提交不改变此代码基线。每次接手必须先 `git fetch`、查看最新 `mod` HEAD、CI 与构建工作流绑定 SHA；**本文不是自动更新的实时状态**。

## 1. 先读这里：目标、项目、不得破坏的边界

用户希望独立开发一个《大富翁4》Android ARM64 **MOD**，在保留原版手感、动画、存档规则的基础上放宽经济上限，特别要求“多方排查数据耦合，不要为修改上限引入读档或结算 bug”。

- 游戏代码：<https://github.com/lly8666/richman4-remastered>
  - `main`：Windows v3.11 原版规则复刻，2026-10-16 交接时提交 `f87e8dcaaf6a3fc14c9489829162cf7d0bfeaf7c`。
  - **`mod`：本次继续开发的唯一分支**，基线 `8bc6eb54c92a205001ed699786394f930c242457`；相对于 `main` 有 **90 个额外提交**。**严禁把 MOD 经济参数合并回 `main`**。
- Android 工作流仓库：<https://github.com/lly8666/qairt-sdk-archive>，`main`。
- **最新 MOD 构建工作流**：`.github/workflows/build-richman4-android-mod.yml`，交接时 `RICH4_UPSTREAM_SHA = 8bc6eb54c92a205001ed699786394f930c242457`。
- **不要误用旧的同名工作流** `.github/workflows/build-richman4-mod-android.yml`，它在交接时仍绑定很早的 `a779b7f9d31ec511967240d7c4e8fa125f96e5a3`，并不包含最新 64-bit 改造。
- 原版工作流 `build-richman4-android.yml` 仍绑定 `main` 对应提交，不能为 MOD 改写。
- Android 应用包名：原版 `io.divby0.rich4`，MOD **`io.divby0.rich4.mod`**，可同时安装但应用私有数据相互独立。

## 2. 需求与**已实际提交**的功能（不是未来计划）

| 用户要求 | 最新 `mod` 的实现与关键限制 |
| --- | --- |
| 股票最高价 999999 | `mod::max_stock_price = 999999.0F`；调整股价边界、校验、相关精度测试。报价仍使用原版 IEEE-754 32 位浮点，不是十进制整数货币格式。 |
| 每种道具上限 999 | `ToolAmounts = array<uint16_t,15>`；道具获得、商店、卸载交通工具、神明事件等入口按 999 限制；旧 DAT 只有每种 1 byte，真实超额量存在 `.DAT.mod`。 |
| 新游戏初始资金 30000000、900000000 | 原六档保留，现八档：**900000000、30000000、300000、200000、100000、50000、30000、10000**；开局默认仍 **200000**；原版选项布局已为八档改位置与选项数。 |
| 物价指数始终为 1 | `mod::fixed_price_index = 1`；新局、真实 DAT 读入、月结更新归一；务必区分依赖原版金标的合成 `World` 测试。 |
| 股票交易量扩大 4 个数量级 | **原始流通/供应规模 ×10000**，单只市场容量最大 **100000000 股**；剩余量、交易配额改成 `uint32_t`。单笔输入仍受有符号 32 位 UI/接口限制；当前市场容量本身小于该上限。 |
| 持有大量股票 | 每人单只股票上限 **1000000000 股**（`mod::max_holding_shares`），持仓计数 `int64_t`。 |
| 9 亿初始资金与巨额总资产不溢出 | 玩家现金、存款、贷款、融资额、持仓及主要股票成本/卖出收入、公共基金和净资产转为 **signed int64**；交易执行前校验，可拒绝越界但不应部分成交；月结利息、冠军排名、AI 股权统计等后续已经补了专项修复。 |

数值量级：999999 × 100000000 = **99999900000000**（单只满流通市值）；多只股票总资产远超 `INT32_MAX=2147483647`。所以绝不能回退到 32-bit 金钱或在 UI/事件/AI 中默认 `int` 足够。

**这几项需求已经有对应源码和测试，绝不能将过去聊天里“尚未实现”的进度判断当成当前事实。**

## 3. 已有文档与完整历史

优先阅读：
1. `docs/mod-README.md`：MOD 限制、字段扩宽、存档侧车和边界说明。
2. `docs/history/COMPLETE-DEVELOPMENT-LOG-2026-10-16.md`：已归档 **330 条可达 Git 提交**，附提交链接、开发时间线、编译失败及修复过程。它只涵盖 Git 可查历史，不保证包含未提交的聊天或临时实验。
3. `docs/migrations/README.md` 与 `docs/migrations/000..032`：资源格式、Windows 反汇编依据、库存、经济、随机数、股票、卡片、道具、神明、UI 迁移。
4. `docs/audit/road-card-animation-order-2026-10-15.md` 和 `docs/audit/animation-order-2026-10-15.md`：地格、30 张卡片、动画提交帧与尚未完成的视觉差异。
5. `docs/ui/facility-build.md`、`docs/ui/messages.md`、`docs/ui/finance-menus.md`：研究所选项、神明附身卡片得失、原始界面位置与提示规则。
6. `docs/STATUS.md`：**可能包含主线早期资料**（例如旧 Android 包名），用于历史参考，涉及 MOD 时优先以代码、`docs/mod-README.md` 和本文为准。

主线近期香港修复已经作为 `mod` 分支底座包含：土地公附身踩自有地可升级、研究所研发和图标对齐、资产查询界面偏移、神明的奖励/失卡提示及登场动画后出现随身神明、传送机落地结算、导弹/核弹先动画后送医、建筑施工/破坏时显示旧房屋直至提交帧。曾**错误**修过一次“AI 骰子重复面”后已整体撤回；实际修复是交互式反复载入同一 DAT 时重新播种（自动回放仍可按 `.rng` 复现）。**用户明确说过股东赠送卡片相关功能正常，不要随意碰它**。

## 4. 关键代码图谱

- MOD 常量/上限与受检财务算术：`include/rich4/mod_rules.hpp`。
- 玩家/市场/企业的字段定义：`include/rich4/save.hpp`、`include/rich4/world.hpp`、`include/rich4/inventory.hpp`、`include/rich4/stocks.hpp`。
- 新开局默认资金、初始化 10000 倍股票、原版 DAT 编解码与饱和预览：`src/world.cpp`、`src/save.cpp`。
- 每次交易/股票配额/股价趋势/真实资产价值：`src/stocks.cpp`、`src/stocks_ai.cpp`。
- 物价指数、付款、新闻/命运、银行/时间机器、回合/结算：`src/game.cpp`、`src/events.cpp`、`src/services.cpp`、`src/auction.cpp`。
- 道具上限及各种直接发放入口：`src/inventory.cpp`、`src/tools.cpp`、`src/gods.cpp`、`src/services.cpp`。
- **MOD 扩展格式**：`include/rich4/mod_save.hpp`，`RICH4-MOD-ECON-2`，并兼容旧 `RICH4-MOD-TOOLS-1`。
- Android 桌面/手机实际读写、股票/银行数字输入与展示：`tools/native_game.cpp`、`tools/screens/stocks.cpp`、`tools/screens/finance.cpp`、`tools/screens/monthly.cpp`、`tools/screens/menus.cpp`、`tools/screens/items.cpp`。
- CMake/无专有素材 CI：`CMakeLists.txt`、`.github/workflows/research-core.yml`。
- 应用 ID/名称：`android/app/build.gradle.kts`、`android/app/src/main/res/values/strings.xml`。

代码审计时不能只搜 `int32_t`；还要搜：`uint16_t`、`uint8_t`、`static_cast<int>`、`std::accumulate`、`std::clamp`、`INT32_MAX`、`UINT16_MAX`、`price_index`、`shares_remaining`、`stock_market_index`、`funds_change`、金额格式化与 `GameDecision.amount`、事件金额字段、AI 预算、拍卖出价及玩家之间的交易价格。

## 5. 存档契约——这是最高优先级风险

**原始 DAT 字段宽度不能随意更改。** 旧 Windows 格式道具数量仅 8-bit、剩余股票数/配额 16-bit、现金/存款/持仓 32-bit。MOD 运行时宽化，保存时：
- `Save?.DAT` 保持原始偏移，在装不下的字段写**饱和兼容预览**（例如道具 255、股数 65535 或 `INT32_MAX`）。
- 同名 `Save?.DAT.mod` 保存真实 999 件、巨额资金、持仓、市场供应/配额、企业剩余股票与公共基金，使用标识 `RICH4-MOD-ECON-2`、DAT 字节数、FNV-1a 64-bit hash、实体数量进行校验。
- 载入时 `mod::apply_tool_extension` 先在候选 `World` 验证全部侧车内容与 DAT 预览一致，再一次性提交；支持旧版只有道具扩展的 `RICH4-MOD-TOOLS-1`。
- `.rng` 保存可重播 RNG；交互式读档仍按单调时钟重新播种。
- **备份/拷贝必须成套保留 `.DAT`、`.DAT.mod`、`.rng`。** 丢失或对错侧车应提示/拒读，不能悄悄变成损坏余额或被截断的股票。MOD 高额 DAT 不可直接拿到原版 Windows 版继续玩。
- **尚待改进**：多文件顺序写入并非整个三文件组的跨文件事务。异常中断、电量耗尽、移动文件不同步时要有原子替换/回退与灾难恢复策略，但不能擅自降低严格侧车验证要求。
- 使用不同应用包名后，主线与 MOD 的 Android 私有存档路径不同；安装/卸载前提醒备份，不能承诺保留原版数据。

## 6. 已核实的自动构建、测试与 APK

- **MOD 核心回归 CI 已成功**：[GitHub Actions run 38853006131](https://github.com/lly8666/richman4-remastered/actions/runs/38853006131)，覆盖到提交 `bbb120f7c3a08fd0e04f2b9842d42aaf4c70a253`。其后的 `8bc6eb54` 是开发记录重新索引（不更改引擎）。更早的大规模边界测试成功：[run 38850256892](https://github.com/lly8666/richman4-remastered/actions/runs/38850256892)。
- **最新 MOD Android ARM64 APK 工作流成功**：[run 38853125080](https://github.com/lly8666/qairt-sdk-archive/actions/runs/38853125080)，该流程绑定 `8bc6eb54`，产物 `Richman4-MOD-Android-arm64-test-signed-5`，GitHub artifact ID `12026311084`，上传包约 174 MB，**短期保存到 2026-10-19T05:01:32Z**。本记录仅确认 CI/签名/产物，不等于已在实体 Android 按钮/音效/大额股票 UI 实机验收。
- 独立 `main` 编译/原版 APK 工作流不可用来证明 MOD 改造成功。
- **本地核心测试**（在具备 CMake/C++20 工具链的源码根目录）：

```sh
git fetch origin
git checkout mod
git pull --ff-only origin mod
cmake -S . -B build/ci -DCMAKE_BUILD_TYPE=Release -DRICH4_BUILD_VIEWER=OFF -DRICH4_BUILD_GAME=OFF
cmake --build build/ci --parallel 3
ctest --test-dir build/ci --output-on-failure --parallel 3
```

无需原版收费资源的核心单元测试在此路径执行；真实 UI/音效和 Android 需要另外构建并在设备上复测。旧原版 oracle 记录只适用于低数值未改语义范围，MOD 的高额场景应另加专属边界/性质测试。

### Android 构建与签名操作

正确流程：
1. 新 `mod` 改动提交并等待 `research-core.yml` green。
2. 在 **qairt-sdk-archive** 的 `.github/workflows/build-richman4-android-mod.yml` 更新 `RICH4_UPSTREAM_SHA` 到新 `mod` 的 **完整 40 字节 SHA**。
3. 等构建成功，核对 APK 内原始素材与 `lib/arm64-v8a`、包名 `io.divby0.rich4.mod`、V1/V2/V3 签名、`SHA256SUMS.txt`；上传/下载 APK 后再实机验收。
4. 注意另一个旧 `build-richman4-mod-android.yml` 路径的旧 pin，不要拿它的成功包当作最新交付；有时间再审慎清理重复流程。
5. GitHub Actions 可用加密 secrets `RICH4_KEYSTORE_B64`、`RICH4_KEYSTORE_PASSWORD` 提供固定 PKCS12 私钥；**是否已配置不可假定**。没有私钥时每次构建会临时生成新测试证书，同包名无法保证覆盖安装。不要把密码/密钥写入代码、文档、日志、Release 或公开 artifact。以前临时签名的私钥无法从 APK 恢复。原版和 MOD 虽然包名不同，可以并行安装，但各自更新仍受签名约束。

## 7. 尚未完成的事情 / 不可贸然宣布“无 bug”

1. **高金额全链路检查**：已扩展主要玩家金融与股票系统、月结利息及排名，但尚不能保证每一个企业账面资金 `Commercial.funds/funds_change`、股票市场总指数 `stock_market_index`、交易要约 `TradeOffer`、点券、新闻/命运、魔法、拍卖、贷款罚息/强制扣费都在 64-bit 下正确。部分仍使用原有 16/32 位接口；明确评估哪些应该扩大、哪些属于有意保留的封顶/饱和规则。
2. **数值边界/组合测试**：跨 `INT32_MAX`、`UINT16_MAX`、`uint8` 最大值、900m 开局、100m 总发行、1bn 单股持仓、999999 报价、12 股合计市值、负资产、超限拒绝原子性、财务守恒、月结/股息/企业收购/AI 轮流操作；以多天真实回合和保存重载交叉验证。
3. **Android 显示**：以实际手机检查八档开局资金可选、30m/900m 实际到账、股票数量输入框/每支股票价格/资产页完整显示不截断、不遮挡、银行及月结统计 64-bit、3 位道具数量。CI 编译不能替代真机验证。
4. **存档可靠性**：一笔交易后保存 `DAT+mod+rng`、重启加载、读回一致；错配、损坏、旧版本、丢失/只拷贝 DAT、断电中断场景必须有清楚错误，不要静默降级丢钱。
5. **原版动画/界面差异仍开放**：换地换屋多段飞卡/地块脉冲、天使恶魔亮暗脉冲、核弹白化、部分设置页面。审计记录已有诚实标注，勿宣称所有地格与卡片已逐帧对原版验收。
6. **代码文档可读性**：根 `docs/STATUS.md` 的一些原版包名/旧状态可能与 `mod` 不同；新增 MOD 相关改动应同步更新 `docs/mod-README.md`、本文和全历史日志。

## 8. 给下一个 Agent 的执行顺序与验收门槛

- 第一轮：打开实际 `mod` HEAD、检查近期 Actions、复核目标工作流 pin；读 `docs/mod-README.md`、`mod_rules.hpp`、`mod_save.hpp`、`stocks.cpp`、`game.cpp`；跑完无素材 CI。不要因为本文的固定 SHA 就覆盖更新进度。
- 第二轮：对照这份未解决清单，从**现金/股息/企业资金/高值界面**里先挑实际残留的窄字段，通过独立最小复现用例验证；每改一个宽度同步检查调用接口、格式化、持久化、AI、边界、音画显示。
- 第三轮：实现、提交 **只到 `mod`**、跑完整 CTest、更新独立 MOD Android workflow 的 pin、取得完整产物并检查哈希/签名/包名，再请用户按可复现步骤实机验证。若未跑过完整设备流程，明确标记“待实测”，不能保证零 bug。
- 与用户沟通用简体中文，简要记录证据、提交 SHA、通过的测试和确切 APK 位置；避免在未完成构建前提供不存在的下载链接。**不要主动修改主线**、不要更改用户明确确认正常的股东赠卡规则。

## 9. 核对入口（不用依赖此前聊天）

- [MOD 分支](https://github.com/lly8666/richman4-remastered/tree/mod)
- [MOD README](https://github.com/lly8666/richman4-remastered/blob/mod/docs/mod-README.md)
- [330 条历史提交索引](https://github.com/lly8666/richman4-remastered/blob/mod/docs/history/COMPLETE-DEVELOPMENT-LOG-2026-10-16.md)
- [MOD 最近核心 CI 成功记录](https://github.com/lly8666/richman4-remastered/actions/runs/38853006131)
- [MOD 最新 Android 成功构建 / APK artifact](https://github.com/lly8666/qairt-sdk-archive/actions/runs/38853125080)
- [MOD 正确 Android 构建脚本](https://github.com/lly8666/qairt-sdk-archive/blob/main/.github/workflows/build-richman4-android-mod.yml)

> 本交接目的：下一名 Agent **接得上已完成进度、不会重新做已完成改造、不误打旧分支 APK、不因高额字段狭窄或丢失侧车而损坏存档**。有新需求应先以当时的 HEAD 和测试为准重新复核。


## 10. 后续 Agent 续开发增量核查（2026-10-16）

> 以上 §1–9 为截至代码基线 `8bc6eb54` 的原始交接快照；本节是后续追加，不能反向把旧 APK 视作包含新源码。

- **远端状态**：新增代码提交 [`a204cfec`](https://github.com/lly8666/richman4-remastered/commit/a204cfec7818a6331ea39522ed60ae61260c2bc3) 与 [`9d05838a`](https://github.com/lly8666/richman4-remastered/commit/9d05838a8ca7b6476b689bae4340a2e0d233059e)，只写入 `mod`，不修改原版 `main`。
- **拍卖 64 位结算**：旧 `src/auction.cpp` 用 `add32` 接受宽化的玩家现金、卖家存款和公共资金，高余额会被窄化/环绕。已改为结算前对买卖双方做受检 64 位算术、失败恢复拍卖状态，附超过 INT32 与接近 INT64 上限的回归。[核心 CI `38862885859`](https://github.com/lly8666/richman4-remastered/actions/runs/38862885859) **success**。
- **魔法屋与银行**：`src/services.cpp` 的最多资产、现金、存款选择原用 32 位局部值，魔法效果 4“现金全存银行”又用 `add(int32,int32)`；已改为 64 位排序与全目标预检后统一转账，补万亿级及原子拒绝测试。[核心 CI `38863131499`](https://github.com/lly8666/richman4-remastered/actions/runs/38863131499) **success**。
- **企业资金仍是首要开放风险**：`Commercial.funds/funds_change` 仍为 int32（`include/rich4/world.hpp`）；`src/game.cpp::pay`、`src/events.cpp`、`src/services.cpp::close_shop` 可继续走 32 位企业计账；`src/game.cpp::pay_dividends` 依赖该资金并对股息做历史整数换算。**不得直接只改 struct 类型**：原 DAT 商业记录对应偏移 40、44 的字段仍为 32 位，`RICH4-MOD-ECON-2` 侧车目前没有保存这些真实字段。下一轮需整套设计新版侧车/兼容预览/旧侧车回读/金额结算/AI/股息与 Android 展示，并补跨保存重载测试。
- **剩余有意窄化待定规则**：`TradeOffer.price` 是 int32、`TradeOffer.quantity` 是 uint16，股票大持仓并不等于玩家间一次要约可出售 10 亿股；目前保留原有 DAT/UI 契约。日股市 `stock_market_index` 为 int32，若 12 支股价均在 999999 内，求和乘 10 的理论上界低于 1.2 亿；须继续测试实际旧浮点与无效报价路径，不能只按类型下结论。
- **存档未关闭**：Android `tools/native_game.cpp::write_save` 仍逐个写 DAT、mod、rng 三个文件，**没有跨文件原子替换**。侧车丢失时，一些饱和资金预览无法与合法的原版 DAT 最大值无歧义区分；不要擅自允许静默截断或宣称灾难恢复完成。
- **Android 跟进状态**：正确 workflow `qairt-sdk-archive/.github/workflows/build-richman4-android-mod.yml` 仍锁定 `8bc6eb54`。已核实的成功 [run 38853125080](https://github.com/lly8666/qairt-sdk-archive/actions/runs/38853125080) **不包含**这两次新修复。遵照此次“代码修改只提交到 mod”要求，本轮未触碰工作流仓库的 `main`；下次打包需明确取得修改工作流 pin 的授权/范围。新代码没有真机验收，不要将核心 CI green 说成 Android UI green。
