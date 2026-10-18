# MOD 开发实录与验证交接 · 2026-10-16—17

> 记录截止 **2026-10-17**，包含已提交且可查的变更、失败重试、CI 和 APK 构建证据。**最新游戏代码 HEAD：`70efe575fd9032b4ee440924cda9b1ed89d96740`**（本文作为后续纯文档提交，不改变此版本源码）。接手时务必重新查询两个仓库 HEAD、CI 和 APK SHA，不能把这里当实时状态。

## 0. 边界和两个仓库

- 游戏源码：https://github.com/lly8666/richman4-remastered，仅在 `mod` 开发，原版 `main` 为 `f87e8dcaaf6a3fc14c9489829162cf7d0bfeaf7c`，**未变动**；切勿将 MOD 经济规则合入 `main`。
- APK 构建仓库：https://github.com/lly8666/qairt-sdk-archive，正确的工作流是 `.github/workflows/build-richman4-android-mod.yml`，其 `RICH4_UPSTREAM_SHA` 在本次验收时为 `70efe575fd9032b4ee440924cda9b1ed89d96740`。不要调用过时同名工作流或修改原版 APK 的构建路径。
- Android ID：`io.divby0.rich4.mod`，与原版 `io.divby0.rich4` 分开。APK 产物包含非公开原始游戏资源，放在临时 GitHub Actions Artifacts 中，不应随意提交入公开源码仓库。
- 只写入 **公钥与指纹**；任何 `.p12`、Base64 私钥、密码或 GitHub Secret 值**绝不能提交到公开 Git/交接文本**。

## 1. 按真实问题驱动的开发流水

| 用户遇到/提出的情况 | 已落地的处理与重要源码 | 回归要点 |
|---|---|---|
| 钱多后股票卖出、贷款和净资产出现异常甚至负值；示例：股价 1000/2000 各买约 4.5 亿，10000 借约 8 亿买，20000 多卖股后出现 18 亿贷款 | `src/game.cpp` 的 AI 银行现金配比 `Extended::integer()` 32-bit 截断改为 64-bit；`src/stocks.cpp`、`src/trading.cpp` 防止大额股单和卖方到账的 32-bit 回绕；`src/events.cpp` 收益/命运盗贷扩宽；`src/game.cpp` 大额银行贷款计算与付费防溢出 | 模拟 9 亿先购 45 万股+22.5 万股，再借 8 亿购 8 万股，涨到 20000 卖 75.5 万股，151 亿银行进账、8 亿贷款原值保留，并做 DAT+MOD 侧车回档。**真实用户故障存档未提供，不能断言 18 亿贷款只由某处造成或旧坏档已自动修复** |
| 卡片重复占 15 个槽；道路商店 8 件道具库存售罄 | `CardSlots` 保留至多 15 个不同卡种、每种堆叠 999，背包/商店/夺取 UI 加金边 `×n`；商店补货不再按共享供货字节零值禁止购买，每类道具上限 999。保留原版卡牌语义和已有股东赠卡行为 | 卡数回档、售出/使用/夺取、库存达上限拒绝，不损失点券；DAT 旧式卡片预览+侧车真实数量 |
| 64 位资金连锁审计 | `Commercial.funds/funds_change`、分红、彩票池、特殊角色盗款、命运新闻税额、均富/均贫卡、AI 资产比较、商店企业收入均拓宽或以 128-bit 中间值运算；转账预检和 `run_rule` 溢出回滚；`mod::money_sub(INT64_MIN)` 规避取负 UB | 极值 `INT64_MIN/MAX`、151 亿彩票/分红、500 亿税、1600 亿均富/均贫、15+ 亿企业账本、企业收益结算原子性 |
| DAT 与长金额存档安全 | `include/rich4/mod_save.hpp` 升级 **RICH4-MOD-ECON-4**，存 64-bit 玩家现金/存款/贷款/企业资金、宽股数、卡片堆叠与道具；原 DAT 保留饱和 32-bit 预览和版本陪伴标记；完整侧车哈希核验和 ECON-2/3 历史回读 | 真正的跨文件原子保存还没有做成：`.DAT`、`.DAT.mod`、`.rng` 必须一组三文件一起备份，不得只留 DAT |
| 嫁祸卡在事件发生前问目标 | 命运/新闻/收费在嫁祸目标选择之前显示 **非结算性质** 的触发预告；选择后只真正处理一次；高额钱包收费可负担性判断从 32-bit 改为安全 128-bit 比较 | 预告不扣钱不消费卡，是否提供关押位置以及无目标事件的边界均有回归 |
| 银行存取一次不能超过 21 亿 | `GameSession::respond`、`BankScreen`、`NumberPad` 和 ATM 滑条接通 signed64；精确值键盘支持 19 位；实际累计存款和贷款偿还不会压到 INT32 | 一次存 **151 亿**、取 **231 亿** 和账户接近 `INT64_MAX` 时拒绝而不部分扣款。**新申请贷款和董事长融资单笔协议仍有 32-bit 上限** |
| 高额金额显示挤出格子 | `include/rich4/mod_money_format.hpp` 将普通金额写作万/亿/万亿/京、隐藏精度时标 `≈`，金融运算仍用精确整数；ATM 编辑框显示完整数字；股票、月结、企业、交易/竞拍金额等界面缩写 | 格子大小与旧 640×480 绘图/字库绑定，**核心测试不是手机上截图视觉验收** |
| 事件直接显示终局，随后才播动画 | 重排 `src/game.cpp` 彩票开奖、月结完成、破产通知在相关电影/表格之后；`tools/native_game.cpp` 不再提前为**未来**事件启动卡片弹图，只在事件真正呈现时启动，卡片弹图阻塞后续结果音画 | `EffectEvent.after_events` 验证破产电影、15 日彩票与月底结算顺序；详见 `docs/audit/MOD-EVENT-ORDER-2026-10-17.md`；其他组合和逐帧仍待真机 |
| 开局 3000 万、9 亿显示很难看 | `tools/screens/menus.cpp` 将旧 `30000000/900000000` 文字改成 **3000萬 / 9億**，其它 6 档统一 **30萬/20萬/10萬/5萬/3萬/1萬**；选中档浅金色徽章、下拉列表整块米金底色、两个高资金档金色侧条；`mod::setup_fund_label` 同时支持胜利目标金额 | 原有实际八档金额不变，默认 **20 萬** 不变。代码：[`70efe575`](https://github.com/lly8666/richman4-remastered/commit/70efe575fd9032b4ee440924cda9b1ed89d96740)；核心 CI [`38997529462`](https://github.com/lly8666/richman4-remastered/actions/runs/38997529462) 成功 |

## 2. APK 构建与固定签名：这次确实有成功产物

| 事项 | 已核实数据 |
|---|---|
| 源码 `mod` SHA | `70efe575fd9032b4ee440924cda9b1ed89d96740` |
| 源码核心 CI | [38997529462](https://github.com/lly8666/richman4-remastered/actions/runs/38997529462)，**success**；C++/无素材核心测试完整通过 |
| APK 工作流 | [38997573631](https://github.com/lly8666/qairt-sdk-archive/actions/runs/38997573631)，**success**；builder 的 commit `c62a26c57eee6006e19883c64bae0d9e3f60e587` |
| Artifact | `Richman4-MOD-Android-arm64-test-signed-11`，artifact ID `12087352928`，ZIP 大约 174 MB，过期 **2026-10-20T03:12:16Z**（过期后可能无法再下载） |
| 安装信息 | `io.divby0.rich4.mod`，ARM64，`versionCode=11`；构建日志确认 `10` 个要求的主资源、`25` 个 MIDI、APK `174170239` 字节 |
| 持久证书 SHA-256 | `36778ecf6ef797072560d5cf75777ff508d67a30aa06e15b3e5fabb74e65577d`，构建日志确实输出 `Using retained private key from GitHub Actions encrypted Secrets.` 和 `Persistent MOD certificate verified` |
| 兼容安装 | **此版本才开始采用固定签名**；之前由临时密钥 `139486e4…` 或 `8b0c6207…` 签名的 APK，签名不同，不能直接覆盖升级。以后必须重用相同 p12/密码、包名且 `versionCode` 递增 |
| **未验证** | 尚未在真实 Android 设备检验新版开局页面美观、长时间动画与音效、银行触屏精度、升级安装及旧存档迁移 |

工作流现从 `RICH4_KEYSTORE_B64`、`RICH4_KEYSTORE_PASSWORD` **加密 Secrets** 取得固定私钥（用户已配置并由成功签名日志证实），校验公开指纹，`versionCode` 由递增的 GitHub Actions `run_number` 生成；如果 Secret 缺失会拒绝签名，不应回退成临时证书。公开证书只位于 `https://github.com/lly8666/qairt-sdk-archive/tree/main/signing`。

## 3. 可复现构建、关键路径与交接清单

- 先阅读根目录 `AGENTS.md`、原始交接 `docs/AGENT-HANDOFF-2026-10-16.md`（**历史快照，§1 旧 SHA/旧 APK 状态已过期**）、`docs/mod-README.md`、本日志、`docs/history/COMPLETE-DEVELOPMENT-LOG-2026-10-16.md` 和 `docs/audit/MOD-EVENT-ORDER-2026-10-17.md`。
- 对纯核心改动：`cmake -S . -B build/ci -DCMAKE_BUILD_TYPE=Release -DRICH4_BUILD_VIEWER=OFF -DRICH4_BUILD_GAME=OFF`；`cmake --build build/ci --parallel`；`ctest --test-dir build/ci --output-on-failure`。在 GitHub Actions 还应检查实际 test job 的成功，不凭工作流启动状态宣称通过。
- 对界面改动：**核心 CI 不会编译完整 SDL3/Android Native UI**，需复现 builder 的正确 MOD 工作流。只修改 `RICH4_UPSTREAM_SHA` 指向**已通过代码 CI 的最新 mod commit**；不要改动主线 APK 工作流。
- 新版本签名必须稳定：不要提交私钥、Base64、密码到 Git；公钥/指纹安全可提交；如签名核验失败必须终止构建。APK 安装仍需真机操作；不得把 CI SUCCESS 等同于已运行通过。
- 金融业务/存档改动要先走 `src/game.cpp`、`src/stocks.cpp`、`src/trading.cpp`、`src/events.cpp`、`src/services.cpp`、`include/rich4/mod_rules.hpp` 与 `include/rich4/mod_save.hpp` 的全链路；不能在 DAT 原位置直接存 signed64。
- 游戏本体安装包中的签名私钥属于所有者秘密；**只汇报证书 SHA-256，不要在任何协作文档或日志中输出密钥材料**。

## 4. 开放风险：下一个 Agent 不应误报完成

1. 用户所述真实 **贷款从 8 亿变成 18 亿** 的具体旧存档还没拿到。可以证明构造模拟算术守恒，不可以宣称“已恢复用户此前坏档”；若用户上传存档，需对照当时价格、托管开关、贷款到期、还款/再借事件以及时间线进行因果复盘。
2. `RICH4-MOD-ECON-4` 使用三文件配套保存，`tools/native_game.cpp` 仍分次写 `.DAT`、`.DAT.mod`、`.rng`，**未实现跨文件崩溃/断电原子事务**。
3. 旧固定 ABI，如 `TradeOffer.price:int32`、单笔银行新贷款/董事融资、旧地图收费字段、部分原版数值统计的 16/32 bit，属历史协议兼容限制；要扩宽必须连同场景/存档/UI 改版。
4. 视觉：2026-10-17 的 9億/3000萬 金色选项、卡片 ×n、过长 64-bit 输入、新闻/命运预告、破产/乐透/月底影片等**没有真实 Android 截图或逐帧对照原版的验收**。
5. 长组合测试待加：多个玩家连续嫁祸/免罪、彩票开奖与月结、神明/导弹/核弹/监狱、多个卡片弹窗在同一事件循环中排队；音频/画面顺序须结合视频实测。
6. 签名：务必离线安全备份固定 `.p12` 与密码。Artifact 会过期；如果未来丢掉证书对应私钥，后续就不能直接覆盖安装这版固定签名 APK。

## 5. 新 agent 启动

请直接复制仓库中的 [全新 Agent 接手提示词](../AGENT-TAKEOVER-PROMPT-2026-10-17.md) 作为首条任务。该文件是可独立运行的交接指令；它要求重新验证最新 SHA、CI、签名、读完资料再开发，且不会让接手者依赖本聊天上下文。

## 6. 2026-10-17 后续开发：完整校验存档槽位，避免坏 Autosave 冒充可用存档

本轮先严格依 AGENTS、2026-10-16 交接、完整开发历史、动画审计及固定签名文档核查远端。发现源代码 `mod=ca588ef5` 是后续纯文档提交，Android 专用构建仓库 `main=a1d7a54d` 的 MOD 工作流仍绑 `70efe575`。上次核心 #38997529462、Android #38997573631 为 success，versionCode 11，证书为已认可公开指纹。

- **问题**：`tools/native_game.cpp::read_slots` 只验证 SaveFile DAT 头部，列表可能展示丢失/错配 `.DAT.mod` 的“有效存档”；F9 曾优先加载最新日期的损坏 Autosave，而不尝试旧的完好槽位。
- **修改**：提交 [`16cc57cfced55785c52752b61af869ddd583d129`](https://github.com/lly8666/richman4-remastered/commit/16cc57cfced55785c52752b61af869ddd583d129) 新增 `include/rich4/mod_save_load.hpp`；更新 `tools/native_game.cpp` 存档槽位检查、正式读档入口以及 F9 回退；更新 `tests/game_tests.cpp` 增加合法旧 DAT、MOD 完整加载、损坏与缺失侧车拒绝回归。只更新 `mod`，未动原版 `main` 或股东赠卡语义。
- **已验证**：[核心 CI #39046429771](https://github.com/lly8666/richman4-remastered/actions/runs/39046429771) 的 `test` job 成功，36/36 CTest 通过。**核心测试未覆盖 SDL3 完整 UI 编译、Android 触屏，也没有真机恢复测试**。
- **风险**：选档严格校验不等于三文件事务；RNG 在交互读档时有意重新播种，自动回放才读取 `.rng`；缺失旧 MOD 侧车时不能通过饱和 DAT 预览可靠恢复宽资金。用户仍须成套备份 DAT/MOD/RNG。

## 7. 2026-10-17 同轮最终构建、APK 下载及签名/资源二次核验

- 核心 CI [39046429771](https://github.com/lly8666/richman4-remastered/actions/runs/39046429771)：源码代码 `16cc57cf`，36/36 CTest passed。补充交接文档后的源码 SHA `011bae0024df033be92622b98176216118867a0e`，只有文档差异。
- Android 构建工作流提交 `44a67274dc644a07299924d825938d0ab21692dc` 只改了 MOD 正确工作流的 `RICH4_UPSTREAM_SHA`；新 Android [run 39046665169](https://github.com/lly8666/qairt-sdk-archive/actions/runs/39046665169) **success**，`versionCode=12`（11→12）。完整编译、资产审计、签名、APK 内容检查、Artifact 上传全部成功。该工作流成功时下载的 ZIP artifact 是 `Richman4-MOD-Android-arm64-test-signed-12` / ID `12106426495`，到期 `2026-10-20T15:52:05Z`。
- 真实下载 APK 174178431 bytes，SHA-256 `26aa2ee56979213afa6ba707553c86f964aaccf31356e4ed5faff5439aaf22ba`，与 Artifact 内 `SHA256SUMS.txt` 一致。独立解码 APK binary AndroidManifest 验证包名 `io.divby0.rich4.mod`、`versionCode=12`，ZIP 检查 10 项必需数据/ARM64 .so 和 25 MIDI。构建日志分别在对应 API 范围用 apksigner 校验 V1、V2、V3 均 true；固定公开签名证书 SHA-256 `36778ecf6ef797072560d5cf75777ff508d67a30aa06e15b3e5fabb74e65577d` 匹配。没有输出或提交秘密值。
- 本轮修复仅是正确识别缺失/失配附档并过滤不可加载的存档槽、F9 跳过坏 Autosave；**不能自动恢复已经写坏的财务存档**，没有 DAT/MOD/RNG 多文件原子事务。实机 F9、读档界面、Android 安装升级、动画序列与触屏仍待用户实测。下一任 Agent 应以本段源码 SHA / APK SHA 为比对基线，继续实现可恢复的跨文件事务备份和多连续事件回归。

## 7. 2026-10-18 新一轮房屋/人物声音审计

本轮针对用户最新反馈复查全部 `EffectKind` 房屋动画、新闻区域毁损、道具和好/坏神、正负命运/新闻话语。详见 [逐项矩阵](../audit/MOD-HOUSE-MOVIE-SPEECH-AUDIT-2026-10-18.md)。

1. 源码 `mod` 提交 [`fbc7da72`](https://github.com/lly8666/richman4-remastered/commit/fbc7da722a3424cc48c4aadedc3e24236c345397) 修改 `include/rich4/game.hpp`、`src/game.cpp`、`src/events.cpp`、`tools/screens/effects.cpp`、`tools/native_game.cpp`、`tests/game_tests.cpp`：为新闻／导弹批量住宅与设施保存纯视觉的多目标旧状态，news 电影 callback 在规则破坏之前捕获；电影提交帧正确显示最终建筑。规则/RNG/存档字段无变化。
2. 新测试初次 [CI 39141161728](https://github.com/lly8666/richman4-remastered/actions/runs/39141161728) 因错误假设新闻只选普通地失败；[`d29c5bda`](https://github.com/lly8666/richman4-remastered/commit/d29c5bdaf6b2f14da30f57f617e0ebdbb9f9e6c2) 改为同时覆盖设施。第二次 [CI 39141340512](https://github.com/lly8666/richman4-remastered/actions/runs/39141340512) 因测试爆炸受伤后缺医院出口失败；[`34e0ad99`](https://github.com/lly8666/richman4-remastered/commit/34e0ad99a5697dd620eb6bda516ebd2d498de7df) 完善测试地图，随后 [CI 39141460036](https://github.com/lly8666/richman4-remastered/actions/runs/39141460036) 36/36 通过。
3. [`579463a0`](https://github.com/lly8666/richman4-remastered/commit/579463a0f8ae06aff60b5734e98ca82776b03409) 继续验证路边正面礼盒、好神3/4、坏神7/8/15、奖励新闻8、毁房新闻5、正负命运金钱25/17；核对原版槽分配，未发现此组代码逻辑存在正负反用。最终核心 [CI 39141604097](https://github.com/lly8666/richman4-remastered/actions/runs/39141604097) **36/36 success**。
4. 未验收：完整 SDL3/Android NDK 打包与证书对照；手机逐帧观看；具体未知角色的语言错位；换地换屋的历史不完整脉冲；连续多个区域电影交错公告。只向源 `mod` 提交，不更动源 `main`。

### 2026-10-18 Android ARM64 APK v13 实际构建和固定签名证据

- 游戏 **实际打包代码/文档 SHA**：`92a2541e696263ff4db3c1b7d14cfdacadce5a77`（代码最后一次修改为 `579463a0f8ae06aff60b5734e98ca82776b03409`）；构建仓库 MOD 专用 workflow 提交 [`b3ea3f92`](https://github.com/lly8666/qairt-sdk-archive/commit/b3ea3f92797794d6cb8889b2b08e6f9acac0f78f)。**仅更改 MOD 工作流 pin，原版 main 和主线工作流未修改**。
- [Android MOD #39141949665](https://github.com/lly8666/qairt-sdk-archive/actions/runs/39141949665) **success**。工作流在日志确认 `MOD Android versionCode = 13`、`BUILD SUCCESSFUL`、从加密 Secrets 使用保留私钥（**没有生成新证书**），v1/v2/v3 三个 SDK 范围验证成功，公开证书 SHA-256 `36778ecf6ef797072560d5cf75777ff508d67a30aa06e15b3e5fabb74e65577d` 匹配；包名来源 `android/app/build.gradle.kts` 的 `io.divby0.rich4.mod`，ABI `arm64-v8a`。
- **Artifact**：`Richman4-MOD-Android-arm64-test-signed-13`，ID `12141743970`，预期过期 `2026-10-21T15:49:24Z`；独立取出 APK `174178431` 字节，10 项必备文件（包括 `libmain.so`、`libSDL3.so`）及 25 首 MIDI 齐全；其 SHA-256 `b653e32d67927626db98f547f6a4f910944f515c107fe9d4b449fc0f3d91772a` 与构建包的 `SHA256SUMS.txt` 一致。
- **验收边界**：核心 CTest 36/36、完整 Android 编译、APK 资源完整与固定签名**已经验证**；真实 Android 手机上每个房屋闪回、神明/新闻人物声线、触屏、连续多事件和原版 Windows 逐帧对照**均未验收**。此包与 v12 共用固定证书且 versionCode 13 > 12，满足 Android 同包名原地更新的签名版本条件；旧临时签名的历史 APK 仍不可直接覆盖。备份 `.DAT`、`.DAT.mod`、`.rng`。
