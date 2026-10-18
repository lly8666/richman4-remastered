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


## 11. 2026-10-16 用户追加请求：贷款、买股票、道具补货、卡片堆叠

> **新代码 HEAD `6f8295ee35f4df014a7552b15f97577812e04fac`（文档提交前）；无素材 36 项 CTest 通过 [run 38873119777](https://github.com/lly8666/richman4-remastered/actions/runs/38873119777)。** 这不是 Android/SDL3 核心之外完整构建或真机验收。

- **贷款/股票负资产审计**：贷款增加存款及同额债务，单纯借入不会提高或降低净资产；相同报价购股也应守恒，后续股价下跌可以产生真实负资产。修复 `src/game.cpp` AI 贷款的 32 位乘法与还款判断、人类贷款两边额度的溢出前置检查；`src/trading.cpp` 玩家间股票买卖的 32 位卖家入账、平均成本与 1bn 持股限制，溢出拒绝时不变更产权或资金。股票主市场之前已有 signed64 检查。新增 900m 初始、实际 800m 银行借款、999999 单价 1000 股、ECON-3 DAT/侧车重载守恒；高金额私人股单 5 亿旧持仓与银行余额溢出原子拒绝。**用户报告的实际负资产存档尚未取得，不能断言该玩家存档已恢复或负值一定是软件 bug。**
- **道具商店**：原代码用了共用供应量 `world.tool_supply`（经常每类约 8）阻断再购买，即使玩家个人 MOD 上限是 999。改为每次进店展示前八类商品，世界供应量为零也可购买，个人仍严格封顶 999，失败不扣点。玩家间道具接收上限由 9 改为 999；旧针对 9 的测试同步更新。保留原版真实道具系统/效果，不能改变用户已认可的赠卡规则。
- **30 类卡片各堆叠到 999**：运行时 `CardSlots` 保留原 15 个快捷格 ID 序与原 DAT 偏移，`stacked_amounts[30]` 保存每种实际数量；装载原 DAT 后合并相同 ID，使用一张减少一张，购买/出售/偷取沿用原流程；最多显示 15 **不同种类**，已有种类可累加至 999。屏幕 `tools/screens/items.cpp` 卡片网格采用深色金边计数 `×n`，库存/商店与偷取面板共用。尚未真机检查字体、遮挡、点击命中和多卡动画。
- **ECON-3 侧车**：`include/rich4/mod_save.hpp` 新签名 `RICH4-MOD-ECON-3` 额外保存每玩家 30 种卡片的真实数量；旧 `RICH4-MOD-ECON-2` 与 `RICH4-MOD-TOOLS-1` 仍可按旧逻辑回读。DAT 保存仍使用原版卡片字节预览，完整数量仅存在 `.DAT.mod`；新 DAT 末尾带 `RICH4-MOD-COMPANION-REQUIRED-3` 标识，缺少侧车时拒读防止静默丢堆叠。注意跨文件写入仍非原子事务，**必须三文件组合备份**；旧版无标记的数据可能无法识别丢失侧车，不得承诺灾难恢复。
- **回归与编译失败历史**：初版道具修复 38870503313 因旧测试仍断言 9 上限失败，测试更新后 [38870705276](https://github.com/lly8666/richman4-remastered/actions/runs/38870705276) success；卡片堆叠中间先因 `PlayerState.cards` 未接入新类型、再因测试缺引用报编译错误，补齐后 [38871735937](https://github.com/lly8666/richman4-remastered/actions/runs/38871735937) success；卡片商店/侧车落盘检查 [38872075063](https://github.com/lly8666/richman4-remastered/actions/runs/38872075063) success；后续高额交易边界测试先因错误 fixture 数量少于要约量失败，银行测试一开始用了经过而非停靠银行的随机种子，修正两项测试设置后 [38873119777](https://github.com/lly8666/richman4-remastered/actions/runs/38873119777) **36/36 success**。
- **交付限制**：正确的 Android MOD 工作流仍绑定 `8bc6eb54c92a205001ed699786394f930c242457`，成功的旧版 APK [38853125080](https://github.com/lly8666/qairt-sdk-archive/actions/runs/38853125080) **不包含以上代码**。只向源码 `mod` 提交，本轮没有碰 `main` 或构建仓库 `main`；需要用户明确给出可动的单独 Android MOD 打包方式再交付新版 APK。新的界面尚未独立 Android/SDL3 编译、签名、真机审查。真实用户存档未作为输入，不能替用户宣称现存坏档已修复。


## 12. 2026-10-16 用户实际交易顺序复盘（股价 1000 → 2000 → 10000 → 20000）

- 用户报告两笔约 4.5 亿购股、股价 10000 时贷款约 8 亿继续购股、股价 20000 多时出售，随后看到贷款约 18 亿和现金全扣光，早先还出现负净值。**目前未取得出问题的存档/截图、用户是否开启 AI 托管或是否经历到期日，真实“贷款增加 10 亿”的单一路径不能下定论。**
- 核查 `src/stocks.cpp::buy_stock/sell_stock`：卖股只变动股票持仓、市场配额、银行存款及公司股东顺序；**不会直接增加 PlayerState.loan**。普通 loan 增长来源是 `GameSession::Impl::borrow`、AI 自动借款 `ai_bank_loan` 和命运 `apply_fortune_id(id==2)`。自动 AI 原始策略可能先归还旧贷款、后续再次借款；有无启用托管需要从真实会话辨别。
- **真实找到的高额资金漏洞**：`src/game.cpp::open_bank` 电脑访问银行时曾调用 `(Extended(total)*Extended(target)).integer()`（原版 signed32 FISTP），当 151 亿卖股收入被 AI 重新分配现金时结果可能落为 `INT32_MIN`，导致现金负值和银行余额虚增。现改 `integer64()`，计算两个余额后一起提交，维持原浮点语义。
- **额外修复的相关 32 位漏洞**：`src/events.cpp::income` 将旧 signed64 银行存款送入 signed32 `add`，收到新闻 23 的存款 10% 红利时会发生回绕，已改为 `mod::money_add`；命运 2“身份盗用新增贷款”的累计 loan 曾通过 signed32 `add` 截断，改为 checked signed64。
- **针对用户步骤的精确回归**：银行初始 9 亿，股价 1000 买 45 万股（4.5 亿），2000 买 22.5 万股（4.5 亿），10000 借 8 亿买 8 万股，20000 卖 75.5 万股，所得 `15,100,000,000`，贷款 `800,000,000` 不变，现金 `10,000,000` 不变，净资产 `14,310,000,000`；新 DAT+ECON-3 重新载入精确一致。附属回归验证 AI 在持有 151 亿银行存款时能将约 75% 调整为现金且资金总额保持不变，命运已有 30 亿贷款时再加 1 万不截断，以及新闻 23 对 151 亿存款奖励 15.1 亿后资金正确。
- **证据**：[修复提交 `8c55837c`](https://github.com/lly8666/richman4-remastered/commit/8c55837cef6b4255462ac3fb2d4a0e75876f50de)，[GitHub 核心 CI `38876053524`](https://github.com/lly8666/richman4-remastered/actions/runs/38876053524) **36/36 success**。仅 `mod` 修改，原版 `main` 不变。Android MOD APK 仍 pin `8bc6eb54`，本次修复未打入 APK 或经过真机验收，现有游戏存档异常尚未自动修复。


## 13. 2026-10-16 所有资金路径 signed64 专项全局审计（继 §12 后的开发）

本次源码静态扫查 `src/`、`include/rich4/`、`tools/screens/` 资金符号，综合用户反馈的贷款后股票交易、公司与月结路径。已提交代码截至 `24ace66441b5cfe0dea607fd0762513058de99f4`；[核心 CI 38879537998](https://github.com/lly8666/richman4-remastered/actions/runs/38879537998) **36/36 success**。**仅代表 CI 测试，不是 Android APK/真机结论。**

| 链路 | 审计发现 | 解决方案与验证 |
|---|---|---|
| 商业物业 `Commercial.funds/funds_change` | 原 32 位，`game::pay`、商店关门、新闻35盈利翻倍、分红均会窄化 | 两字段改为 int64，企业帐入支出以 `mod::money_add/sub` 受检结算；超界拒绝 |
| DAT + MOD 侧车 | 旧 DAT 商业记录 +40/+44 只能容纳 int32，ECON-3 仅有流通股无真实公司资金 | **ECON-4** 每企业额外存宽资金与变动，原 DAT 饱和预览；读取逐项核验并保持对旧 ECON-2/3 的兼容性，追加旧 ECON-3 载入与伪造资金拒绝回归 |
| 商店营业额与企业结算 | 原 `Shop.revenue`、`close_shop` 及卖卡/道具为 32 位收入 | 真实收入 64 位，收款前计算双字段，测试 176 亿并校验接近 `INT64_MAX` 原子拒绝 |
| 月中分红与展示 | 分红表 `DividendRow` 原为 int32；企业资金大额分红及逐个清算可能中途溢出 | 分红显示与股东到账改 signed64，并通过虚拟余额保留同一企业被二次上市时原版只分一次规则，预检所有企业/股东后清偿；测试 151 亿入账与溢出拒绝未改变公司余额 |
| 新闻（8–13、23、30–35） | 股票最多玩家股数累加 32 位，现金/房产/股票税 32 位，新闻奖励银行 10%/公司翻倍风险 | 64 位持股总数、税额、银行奖励、公司盈亏与 UI 新闻行金额；测试 500 亿现金税 25 亿、十亿级持股比较和 302 亿公司翻倍 |
| 命运、特殊角色和神明 | 盗贷叠加、存款抽取（10%/20%）、企业资金收缴、神明现金奖金可能窄化 | `EventCallbacks/ special_actors::Callbacks::pay` 改 signed64，x87 `integer64`，旧小金额特殊演员 oracle 按原 32 位低位比较；50b 存款偷款等高额回归 |
| 均富/均贫/查税卡 | `src/cards.cpp` 汇总现金 32 位相加，可能将 1600 亿资产算成负数 | 均值使用 128 位中间求和、64 位余额赋值，攻击倾向数值参数按原接口安全饱和；160b 均富、60b 均贫回归 |
| AI 资金判断 | `cards_ai`、`tools_ai` 32 位余额相加；`stocks_ai` 容易因每天配额已降到 16-bit 内而误入 32 位股票预算 | 64 位余额，128 位倍数比较，对当前持仓市值也启用宽交易路径；附小配额+高价大持仓回归 |
| 彩票公共奖池 | 购票 `public_fund`、中奖现金仍 32 位环绕 | 真实奖池+中奖金额改 signed64，先检测溢出再扣票价；151 亿奖池支付、`INT64_MAX` 拒绝测试 |
| 资金支付入口及银行 | `game::pay` 金额原 int32，转账若收款方超上限可能先扣款；AI 贷款还款 `repay(int32)` | 转账请求与收款预检 signed64，融资双字段预检，AI 可完整偿还大于 21 亿贷款，`run_rule` 的意外溢出恢复 World/RNG/事件快照 |
| 64 位基本算术 | `money_sub(a,b)` 原用 `money_add(a,-b)`，当 `b==INT64_MIN` 时负数取反未定义 | 改为 128 位直接求差再边界检查，补 `INT64_MIN` 回归 |

### 位宽例外与未完成验收

1. **原版兼容 ABI**：`TradeOffer.price:int32`、`TradeOffer.quantity:uint16`、原 DAT 商业记录的价格/营业数字节、`PlayerStatus.cumulative_income/spending:uint32`、点券 `uint16`、单笔数字键盘/骰子与事件代码。这些**不是**无限制 64 位交易能力；不能只粗暴改 C++ 字段而不更换 DAT/schema、UI 输入和协议。
2. **旧规则固定价/地价/过路费**：`multiply32` 还存在于地块设施原单次定价、过路费与 AI 阈值，此 MOD 物价指数固定 1 且单项基础价来自小型原字段。要调整单笔地产费用规模必须连同场景协议扩展与画面做专门改造，而不是在本轮任意改变原版定价。
3. **运行错误呈现**：在 `run_rule` 内遇到资金溢出会还原快照并将错误向上报告；尚无所有交互入口一致的用户可读“交易超出上限”弹窗/重试体验。未宣称对所有畸形 DAT、多人连续事件和极端负资产存档完全恢复。
4. **跨文件断电恢复**：`.DAT`、`.DAT.mod`、`.rng` 仍分开写入，无三文件事务原子性；新版附档不可丢。还须恢复测试不同版本对同一文件组混用时的保护。
5. **Android 打包/真机**：独立 `qairt-sdk-archive/main` 中 Android MOD 工作流仍 pin `8bc6eb54`（2026-10-16 的旧代码）。按用户命令本轮只提交源码 `mod`，没改另一仓库 `main`。没有重新 Android 编译、签名和真机验收，屏幕排版长金额仍需重点确认。原版源 `main` 没有修改。

**下一位 Agent：** 先在最新 `mod` 上重复 core CI，再做 Android/SDL3 全量构建与屏幕财务字段尺寸检查，针对 64 位交易报错改友好 UI，继续审计剩余老 ABI 窄化而不要破坏原 DAT。用户问题中贷款由约 8 亿变 18 亿仍未取得实际存档，不能据此断言原因已唯一定位。


## 14. 2026-10-17：嫁祸卡提示前置、大额银行 ATM 与 UI 紧凑金额

源码 HEAD `0a0dc1ecdb6beb29d11a4f160ccbb93cc0a7fdcf`，GitHub [core CTest #38934508620](https://github.com/lly8666/richman4-remastered/actions/runs/38934508620) **36/36 success**。关联 Android MOD builder [#38934629203](https://github.com/lly8666/qairt-sdk-archive/actions/runs/38934629203) pinned `0a0dc1ecdb6beb29d11a4f160ccbb93cc0a7fdcf`；完成/签名/产物请自行以工作流实际结论为准，不能把已触发说成已经成功。

1. **用户反馈嫁祸选择出现在事件发生之前。** `src/game.cpp::resolve_event` 之前 `screen_first` 遇到 `NeedChoice` 会清除事件展示并在回答后才 emit 新闻/命运公告，用户看不到事件原因就被迫选嫁祸对象。本次保留规则原子 replay，给相关 human `card_target` 插入仅展示的新闻/命运事件预告（按命运/新闻选卡 ID、照片与音效），之后选定嫁祸目标，再正式结算真实事件；非事件选择不受影响。`src/game.cpp::select_toll_payer` 对大额钱包仍使用 `add32(cash,bank_balance)` 判断过路费是否足以触发嫁祸，已改 `__int128` 比较并在真实高额过路费选择前插入收费原因提示。严格禁止在预告阶段扣卡、计数、转账或关状态；回归覆盖初始余额大、少额过路费不弹嫁祸、真实收费弹框先提示事件、拒绝后正常入账与命运关押目标场景。
2. **银行存取/还款真正 64 位**：`include/rich4/game.hpp::respond(option,int64)` 和 `tools/screens/finance.hpp` `NumberPad/BankActions` 等 UI 全链路改 signed64；ATM 滑杆用 `__int128` 内部乘法防溢出，19 位数字上限、MAX 一键全部、触屏放大键盘与十位以上数字独立精确文本。core 验证一次存 151 亿再一次取 231 亿且拒绝近 INT64_MAX 非原子转账。**注意** 新贷款 `borrow(int32)` 和董事长融资 `change_financing(int32)` 原版每笔上限仍 int32，但偿还已有贷款的 `repay(int64)` 现支持一次清偿超过 21 亿。主账户总资产当然不受这两个单笔交易协议宽度制约。
3. **中国金融单位、UI 防超格**：`include/rich4/mod_money_format.hpp` 为纯展示函数 `grouped_digits`（准确数字）与 `compact_money`（按万/亿/万亿/京简写、若近似加 ≈，支持 INT64_MIN/MAX）；`tools/native_ui.hpp::money` 和 `screens::financial` 统一，大额股票交易、企业、彩票、月结、资产、银行侧栏和新闻行不再使用十几位原数字强挤固定列，股票股数等**非金钱**量仍保留准确数，不误缩写为元。资金计算和 DAT/ECON-4 存档仍使用完整整数；已有纯显示视觉测试以 Android/SDL3 构建及真机为准。
4. **此轮源码 CI** [38932681378](https://github.com/lly8666/richman4-remastered/actions/runs/38932681378)、[38933168075](https://github.com/lly8666/richman4-remastered/actions/runs/38933168075)、[38933911818](https://github.com/lly8666/richman4-remastered/actions/runs/38933911818)、[38934508620](https://github.com/lly8666/richman4-remastered/actions/runs/38934508620) 均 success；早期实验 #38933673593 因新测试未放置监狱地图出口导致失败，已修正，**不是发布失败的遗留逻辑**。项目源码 `main` 不受影响。独立 builder 仓库原先旧 SHA 构建不是这版，不要混淆。

下轮重点：试机检查大额 ATM 全数字编辑框横向空间、手机触屏点按和金额近似字体支持；可考虑银行单次新增贷款与董事长融资协议进一步 64 位改造及跨三文件保存事务，但需联动业务范围和旧 DAT，不能粗暴扩 C++ 字段。保留真机不曾测过的诚实状态。


## 15. 2026-10-17 后续：原版动画播放顺序 + 固定签名密钥

- **原版行为比较**：见新增 [事件顺序审计](audit/MOD-EVENT-ORDER-2026-10-17.md)，逐类对照 `docs/ui/effects.md`、`docs/ui/messages.md` 与 Native UI。**已经确认 3 个结果先播问题**：`advance_day` 彩票开奖 `emit` 原排在 `lottery_draw` 电影前；`close_month`“月结完成”原排在 `month_report` 前；`bankrupt` 结果事件先于 Data[555] 电影。现均改为电影／表格先，结果后，`tests/game_tests.cpp` 新增电影与事件 `after_events` 顺序回归。15 日彩票与月底结算分两天测，误用同日导致首次新增测试失败后已修正。
- **影响所有被动卡的队列问题**：`tools/native_game.cpp::observe` 曾立刻调用 `items_.observe`，把本批次**尚未执行到的未来事件**卡片图片提前放入弹窗队列；如今只在 `present` 对应事件被实际消费时创建 popup，卡片弹窗加入 `presenting()` 阻塞条件，先完成卡片图片才可播其结果文字/音频和其他后续动画。需要 Android/SDL3 编译与真机再次检查。
- **原版例外**：掷骰点数提前决定是 Windows 原版定义，不应为了视觉而把规则 RNG 顺序重排；规则值在 FLC 动画前提交也是原版契约。现保留内存实时结果，纠正对用户的显示顺序。某些原版胜利 AVI 和特殊角色的精确逐帧时间仍 UNVERIFIED，不能说所有动画已完全等价。
- **签名**：前一成功 APK [38935496368](https://github.com/lly8666/qairt-sdk-archive/actions/runs/38935496368) 使用临时密钥证书 `8b0c6207...`，前前次是 `139486e4...`；不能恢复旧私钥。最新**新的固定** PKCS12（alias `rich4-local-test`，证书 SHA-256 `36778ecf6ef797072560d5cf75777ff508d67a30aa06e15b3e5fabb74e65577d`）保存在本会话用户可下载的私有文件和独立密码文件中，**未入公开 Git**。已在构建仓库 `main` 的仅 MOD APK 工作流加入 GitHub Actions Secrets 必填和公钥指纹核查，删除临时生成新密钥的退路。需要用户设置 `RICH4_KEYSTORE_B64`、`RICH4_KEYSTORE_PASSWORD`，此工具连接无法代填，没配置前的 Workflow 失败是预期安全行为。链接 [签名配置文档](ANDROID-MOD-SIGNING.md)。原版源码 `main` 不可动。
- **检查结果**：核心 CI [38940321502](https://github.com/lly8666/richman4-remastered/actions/runs/38940321502) 于彩票与破产顺序测试修正后全套通过；本轮最终 Native UI 队列改造在后续 [38940817651](https://github.com/lly8666/richman4-remastered/actions/runs/38940817651) **36/36 success**，需要独立 APK C++ 完整编译。2026-10-17 前后不同工作流的实际 SHA 要一一核对。


## 16. 2026-10-17 最新可执行交接：9億/3000萬 UI + 首版固定签名 APK

**权威后续记录**：[开发实录](history/MOD-DEVELOPMENT-SESSION-2026-10-17.md) 和 [可复制新 Agent 提示词](AGENT-TAKEOVER-PROMPT-2026-10-17.md)。本段**覆盖**此前 2026-10-16 的旧 workflow pin / Secret 尚未配置状态。源码代码 HEAD `70efe575fd9032b4ee440924cda9b1ed89d96740`，builder workflow pinned 同一 SHA。原版 `main=f87e8dcaaf6a3fc14c9489829162cf7d0bfeaf7c`，未变。

- `tools/screens/menus.cpp` 现在把开局资金 900000000 显示为 **9億**、30000000 显示为 **3000萬**，原有其余 6 档各用萬单位；金边米色八行列表和选中徽章。真实资金、原版 20 萬默认值都不变；`mod::setup_fund_label` 是纯展示。提交 [70efe575](https://github.com/lly8666/richman4-remastered/commit/70efe575fd9032b4ee440924cda9b1ed89d96740)；核心 [38997529462](https://github.com/lly8666/richman4-remastered/actions/runs/38997529462) **success**。
- 用户已在 builder 仓库加密 Secrets 配置固定 PKCS12；成功构建 [38997573631](https://github.com/lly8666/qairt-sdk-archive/actions/runs/38997573631) 日志**直接确认**加载保留的私钥、证书 SHA-256 `36778ecf6ef797072560d5cf75777ff508d67a30aa06e15b3e5fabb74e65577d`，`versionCode=11`，APK 资源校验成功。Artifact `Richman4-MOD-Android-arm64-test-signed-11` / ID `12087352928`，到期 `2026-10-20T03:12:16Z`。这是**第一版固定签名 APK**；早期临时签名版本不可直接覆盖。永不把私钥/密码内容提交 Git。
- 此前动画顺序改造（彩票动画/破产影片/月报在结果通知前，卡片 popup 按真实事件排队）经过核心 CI [38940817651](https://github.com/lly8666/richman4-remastered/actions/runs/38940817651) success，也被同一新版 APK 源码包含；没有 Android 真机逐帧验证。
- 本轮交接修改仅是文档，**不修改游戏业务规则也不重发 APK**；上面的核心/API 通过和 APK 成功分别对应文档前的代码 SHA。未来的 agent 切勿将 GitHub Actions 编译和签名成功等同于实机测试或旧坏档完全修复。

## 17. 2026-10-17 新接手 Agent：存档槽位完整性检查

- 接手时远端 `mod` HEAD `ca588ef5d7369d5a87e05aa18d163fe82aa7eea8` 是上一轮纯文档提交；上一次通过 Android 的**游戏代码**是 `70efe575fd9032b4ee440924cda9b1ed89d96740`，构建版本 11。不要把它与本轮新提交混淆。
- 新的 MOD 源码提交 [`16cc57cf`](https://github.com/lly8666/richman4-remastered/commit/16cc57cfced55785c52752b61af869ddd583d129) **只写 `mod`**。修复 Android 存档列表仅检查 DAT 头而可能将丢失/错配 ECON-4 侧车的槽位列为有效的问题。新增共享 `mod::decode_checked_mod_save`；列表、交互读档共享完整校验；F9 按文件时间顺序跳过不完整存档。原版低值 DAT 继续允许无附档读入，坏的 MOD 侧车坚决不静默忽略。
- `tests/game_tests.cpp::mod_save_load_slot_integrity` 追加：完整 90 亿现金恢复、缺 MOD 拒绝、篡改 MOD 拒绝、原版 DAT 无 MOD 回读、错误侧车拒绝。核心 [CI 39046429771](https://github.com/lly8666/richman4-remastered/actions/runs/39046429771) 的 CTest 36/36 success。Android SDK/SDL3 编译、真实触屏与断电恢复没有由此核心 CI 验收。
- **下一步**：仅将正确的独立 `qairt-sdk-archive/.github/workflows/build-richman4-android-mod.yml` pin 更新至包含上述修复的 `mod` SHA，确认 versionCode 递增、固定签名指纹、ARM64 库、游戏素材及完整 Android 编译；切勿改 main 版游戏代码或其他旧 MOD 工作流。
- **依然未解决**：三文件组 `.DAT/.DAT.mod/.rng` 跨文件原子替换；缺少真实用户异常贷款存档；真实 Android 存档槽位/触屏与全种类组合动画测试。不要将本轮“选档过滤”宣传为“跨文件事务已经修好”。

### §17 后续独立 ARM64 APK 实际成功（2026-10-17）

- 源码 `011bae0024df033be92622b98176216118867a0e`（含修复 `16cc57cf` 和测试与交接文档），构建仓库仅 MOD 工作流 pin 更新提交 `44a67274dc644a07299924d825938d0ab21692dc`；Android [#39046665169](https://github.com/lly8666/qairt-sdk-archive/actions/runs/39046665169) **success**，`versionCode=12`，签名公钥指纹仍是 `36778ecf6ef797072560d5cf75777ff508d67a30aa06e15b3e5fabb74e65577d`。Artifact ID `12106426495` / `Richman4-MOD-Android-arm64-test-signed-12`，到期 `2026-10-20T15:52:05Z`。
- 实际下载并核对 APK：包名 `io.divby0.rich4.mod`，APK 大小 174178431，SHA-256 `26aa2ee56979213afa6ba707553c86f964aaccf31356e4ed5faff5439aaf22ba` 与 `SHA256SUMS.txt` 匹配；10 项必需素材/ARM64 库和 25 MIDI。GitHub 构建记录含 V1/V2/V3 验签 success、持续性密钥加载 success 和准确指纹匹配。**签名密钥从未写入 Git 或报告。**
- 此后若只补充文字而不改源码，新的纯文档 HEAD 不要求重复打包；必须始终区分“APK 打包的源码 SHA”与“纯文档后续 SHA”。本次没有 Android 真机启动/覆盖安装测试，也没有三文件事务恢复验收。

## 18. 2026-10-18 专项接续：房屋电影闪回与语音正负极性

- 用户发现房屋提前显示最终状态，再倒回旧状态播完动画才再次变为最终状态，同时有正面道具／神明／事件发言似乎是负面。代码审计已定位新闻 4/5/15/20/21 和多目标导弹原先缺少目标旧状态的完整可视记录。
- `mod` 提交 `fbc7da722a3424cc48c4aadedc3e24236c345397`：`EffectEvent.previous_properties` 记录整组被电影修改的普通地与设施旧面貌；新闻在规则修改前排入电影以捕获旧面貌；导弹/核弹在面积毁损前保存每处旧面貌；主板及 EffectPlayer 统一在提交帧前恢复。随后测试修正 `d29c5bda`、`34e0ad99`，人物语音覆盖追加 `579463a0`。
- [CI #39141604097](https://github.com/lly8666/richman4-remastered/actions/runs/39141604097) 36/36 success，涵盖多建筑及设施、5 种新闻电影、导弹/核弹、奖励/损失的命运/新闻、人类拾道具、好神与坏神附身。两次早期 CI 因测试不完整失败，详见 [专项审计](audit/MOD-HOUSE-MOVIE-SPEECH-AUDIT-2026-10-18.md)。
- **已确认原版语音槽** 0–2 获益、3–5 厄运、6–8 收钱、9–11 失钱、22 坏神附身、23 坏神离开。上述样本均没有正负混用，因此本轮未擅改原版 voice IDs；仍需用户视频捕捉罕见错位和音轨时序，区分事件本身喜忧参半与真正错误。
- 新电影字段只用于实时展示，不修改游戏规则、DAT 布局、ECON-4、股份/银行或 RNG。复杂互换房屋、连续建拆、新闻18、命运0/1和真实安卓帧时刻仍待验证，不能说“所有房屋动画全修复”。下一步对照 Android 正确 MOD 构建 pin、versionCode 和证书指纹，设备复核房屋前后帧以及喜怒声线。
