# 《大富翁4》原版重构与 MOD：开发全历史和可核查索引
更新：2026-10-16。本记录收录 **GitHub 当前 `mod` 分支可达的全部 317 条提交记录**，由远端提交历史自动汇总；它不是声称能够复原没有落入 Git 的私人聊天、临时调试或丢失的构建产物。

## 一、项目与分支
- 源代码：[richman4-remastered](https://github.com/lly8666/richman4-remastered)；标准重构 `main` 用于尽力贴合 Windows 大富翁4 v3.11。
- 独立实验版 `mod` 从 2026-10-15 的原版修复提交 `f87e8dca` 分出；**全部改版规则只进 mod，不回写 main**。
- ARM64 原版测试包由 [qairt-sdk-archive](https://github.com/lly8666/qairt-sdk-archive) 中的 `build-richman4-android.yml` 构建；Android 每次重建是否能覆盖安装取决于是否使用同一把签名私钥。已加入配置 GitHub 加密 Secrets 的选项，不代表旧临时签名私钥可被恢复。
- 完整技术规格与反汇编依据参见 [迁移导航](../migrations/README.md)、[开发状态](../STATUS.md) 和 [原始资源说明](../../game-assets/README.md)（资源路径如文件不存在应按实际仓库目录核对）。

## 二、按主题的开发记录索引
1. **原版数据、逆向和数值保真**：00–10 阶段完成资源容器、原 DAT 定长区域、坐标投影、日历方向、卡片和道具基本库存、经济地块、地形、Watcom RNG、地图读写与模型校验。逐项技术研究见 [migrations/000..010](../migrations/README.md)。
2. **股市、金融和业务结算**：股票公司、交易限额、精度模拟、公开拍卖、股权/股东排序、AI 交易、玩家间交易、商业服务和红黑卡对应 [015](../migrations/015-stocks.md)、[023](../migrations/023-legacy-float.md)、[029](../migrations/029-stocks-ai.md)、[030](../migrations/030-player-trading.md)。
3. **游戏循环及卡片道具**：回合/滚骰/移动落地、神明附身/脱离、新闻命运、监狱/医院、乐透、小游戏、机器人工人/工程车、导弹/核弹、传送机/时光机、商品/设施研究和魔法屋，见 [012](../migrations/012-native-turns.md)、[014](../migrations/014-events.md)、[020](../migrations/020-cards.md)、[021](../migrations/021-tools.md)、[026](../migrations/026-gods.md)。
4. **Android/iOS 原生外观**：使用原始资源贴图/音效、投影、原版 640×480 页面、全屏/视窗、原版对话框、托管 AI、开局地图与资金、商店背包、设施选择以及研究所。见 [018](../migrations/018-native-app.md)、[027](../migrations/027-native-interaction-fixtures.md)、[031](../migrations/031-native-acceptance.md) 和 [UI 文档](../ui/main-screen.md)。
5. **2026-10-15 研究所**：修复研究项目选择、建造后提示、项目耗时与生产道具、存档研究字段、已研发格与按级解锁、按钮遮挡，以及研发图标相对背景错 16 px；屏幕 Back 可稍后研究。对应 [facility-build](../ui/facility-build.md)。
6. **2026-10-15 游戏 bug**：自用传送机落地触发购建/商店并消耗回合；神明 object ID 与 kind 混淆致土地公自有地不升级，修复后无主地免费取得/自有地正常加盖；菜单视窗布局、资产查询列表下错 17 px、托管 AI 选中分页面贴图。对应 [日期审计](../audit/road-card-animation-order-2026-10-15.md)。
7. **2026-10-15 动画校序**：导弹/核弹视频改在医院/救护车结算之前；机器人工人、魔法屋增建、怪兽卡、拆除卡、恶魔和工程车统一动画提交帧、在提交之前继续画旧房屋；神明动画/字幕之后才显示随身神明，补回福神/衰神获失卡片提示。参见 [特效审查](../audit/animation-order-2026-10-15.md)、[地格/卡片逐项审查](../audit/road-card-animation-order-2026-10-15.md)、[消息顺序](../ui/messages.md)。
8. **2026-10-15 掷骰澄清、撤回与更正**：曾误判为 AI 骰子电影复用了最后一帧并实施过一轮修复，随后已**整组撤回**；真正问题是反复读取同一 DAT 导致随机状态重复，改为交互式读档时按单调时钟重新播种，重播测试仍可用 `.rng` 保持可复现。见 [RNG](../migrations/008-random.md)、[回合修复](../migrations/012-native-turns.md)。
9. **2026-10-15 / 10-16 独立 MOD**：原先提出股票 999,999、道具 999、初始 30,000,000、物价永远 1。已另建 `mod` 分支，拓宽道具内存计数并增加原 DAT 旁的 `.mod` 侧车校验文件，隔离 Android `applicationId`。**2026-10-16 新需求**再加初始 900,000,000、股票流通/交易量 10,000 倍以及足够宽的持仓/总资产，需继续检测有符号溢出和存档兼容。这些新要求不应被历史记录误当成已完工。

## 三点一、2026-10-16 大额 MOD 资金与股票开发实录

- **需求与分支隔离**：原版 9999 股价、9 件工具、普通物价规则维持在 `main`；MOD 增加 999999 股价、每种道具 999、初始资金 30000000 和 900000000 两档，物价恒 1；资金菜单从六档变为八档，保留原本默认的 200000。
- **大股市约束**：原始市场已发行/流通的股数和交易配额字段为 16 bit，MOD 改成 uint32；新游戏的原始股市供应配置乘 **10000**，市场总容量每股 100000000，允许个人单股 1000000000 的受控持仓上限。不能在 16 bit 存档字段直接写大数。
- **风险检查与修复**：原始 `PlayerState.cash/bank_balance/loan/special_finance`、`StockHolding.amount`、公共基金及买卖结果原为 32 bit；MOD 改成有符号 **int64**，采用准确的 float32 报价乘股数整数运算并在扣款/增股前进行所有校验；`stocks::player_wealth` 现在真正返回 64 bit 资产净值，而非原先超过 INT32 上限后饱和。AI 购股大市场加独立的大额预算路径。
- **不破坏 DAT 的兼容格式**：DAT 留下传统有限位数的**饱和预览值**；同名 `.DAT.mod` 使用 `RICH4-MOD-ECON-2` 校验签名并保存原样 64-bit 玩家资金、股数、企业原始股额、市场当日交易量、公共资金及 999 件工具。读时通过尺寸、原 DAT 哈希、计数与逐字段饱和预览一致性测试，在全部成功后一次性提交；支持旧 MOD 工具侧车 `RICH4-MOD-TOOLS-1`。
- **实测结果**：修改过程中若干 CI 编译因原版与 MOD 的整数类型耦合出错，见下方真实提交记录；经过定位与回归修复，`38850154985` 与 `38850256892` 均完整 **success**。新增边界测试包括 900m 初始资金、100m 股市规模、5000 万股高价买卖资金守恒、资产高于 INT32、附档错配原子拒绝。
- **必须持续排查的项目**：大型企业账面资金、银行特殊融资、新闻/命运转账、股息分红原版 UI 中的 32 bit 展示与回合流程目前不宜宣布全面完美；需额外用 Android 的股票/资产显示和真钱包高值流程验证。请将此类事项放在开放问题中，不要把“此时 CI success”解释成永远不会有 bug。
- **交付构建路径**：原版独立 Android workflow 不应修改其源码 SHA；另建 `build-richman4-android-mod.yml` 独立打包 `io.divby0.rich4.mod`，并在构建成功后核对 APK。
- **近期改动**：后续若需更新历史附录，按 GitHub API 当前 `mod` 所有可达 commits 重建；此处正文保留历史决策以供追溯。

## 三、已知未完成与风险
- 上述修复有源码/CI 依据，但并非所有地格、卡片、新闻、神明特效都已在实体 Android 与原 Windows EXE 双画面对齐。换地换屋双目标飞卡/闪烁、核弹白化与某些设置页面仍有已记录缺口。
- 原 DAT 道具数量只有 8 bit，股票流通量含 16-bit 字段，资金/持仓和原 API 多为 32 bit。MOD 的宽额交易/资产必须对**内存、UI、数值计算、旧存档序列化、侧车一致性、回档、卖出股权和 AI 逻辑**一起实施；只改上限可能造成资产回绕为负数。
- 原始游戏点券 16 bit，MOD 已让大笔道具销售点券饱和于 65535；是否继续拓宽属于另一个独立兼容性决策。
- 此文档的附录提供所有可核查提交。分支继续演进时可按同一 GitHub API 列表重新生成附录，不要错误称其自动实时更新。

## 四、提交历史附录（截至文档本次更新，按新到旧）

以下包含本轮资金/股市增容、编译失败及随后的修正；每条均链接到不可变的完整 Git commit。

| 日期 (UTC) | 完整提交链接（短 SHA） | 变更标题 |
|---|---|---|
| 2026-10-16 | [`99712ca5c03f`](https://github.com/lly8666/richman4-remastered/commit/99712ca5c03fba222e8c4c26558b9f93b846485c) | MOD: document 900m tier, 10000x shares, 64-bit net worth, versioned financial sidecar and limitations |
| 2026-10-16 | [`3fc9158f3b68`](https://github.com/lly8666/richman4-remastered/commit/3fc9158f3b682dd741ecd2453a7952b95664ed85) | MOD: retain vanilla 200000 default after adding both 30m and 900m start funds |
| 2026-10-16 | [`393c2632805e`](https://github.com/lly8666/richman4-remastered/commit/393c2632805e8d0ccabbbe4c8bda4e55fdaaa2ae) | MOD CI: test every source and fixture commit to catch cross-module financial couplings |
| 2026-10-16 | [`b7da39399fa0`](https://github.com/lly8666/richman4-remastered/commit/b7da39399fa0db519548bd225e9e793d67105485) | MOD tests: project wide cash to original oracle's low32 value |
| 2026-10-16 | [`3b2f77cb4a91`](https://github.com/lly8666/richman4-remastered/commit/3b2f77cb4a91211077e698ea12dd960c87d4342f) | MOD tests: compare original low-32-bit oracle projections of widened finance fields |
| 2026-10-16 | [`078e5e625ac3`](https://github.com/lly8666/richman4-remastered/commit/078e5e625ac38f9acea5594a51f1595efb439d2a) | MOD tests: checksum-linked 64-bit DAT companion restores trillion-dollar money and 50m shares |
| 2026-10-16 | [`90984f94fabf`](https://github.com/lly8666/richman4-remastered/commit/90984f94fabf329c19a7beb8d55bc2549a37bf86) | MOD test: 900 million starting money and 10000x original stock float initialize exactly |
| 2026-10-16 | [`afe89349624e`](https://github.com/lly8666/richman4-remastered/commit/afe89349624e70073053a6635f63294a214ea49c) | MOD tests: prove 50m share trades, 64-bit balances and net wealth are exact and atomic |
| 2026-10-16 | [`b6725f1087f2`](https://github.com/lly8666/richman4-remastered/commit/b6725f1087f2c3869ee5a5c5cf452dc69a45d31c) | MOD: prevent stock AI 32-bit budget wraps by adding bounded 64-bit large-market planner |
| 2026-10-16 | [`d01dc22eb7fc`](https://github.com/lly8666/richman4-remastered/commit/d01dc22eb7fcf508daf654d0f2a97ef643cc69be) | MOD: block incomplete DAT reload when high stock or inventory state lost its sidecar |
| 2026-10-16 | [`edfa0f826604`](https://github.com/lly8666/richman4-remastered/commit/edfa0f8266049fd71d6be15c4d46e93787927b9d) | MOD: bound stock-AI buy quantities by widened quotas and per-player cap |
| 2026-10-16 | [`f8ec1b8baf6b`](https://github.com/lly8666/richman4-remastered/commit/f8ec1b8baf6be3c819ae9174548be3a1e8bd38f4) | MOD: preserve 64-bit money in stock transaction and bank event records |
| 2026-10-16 | [`991ca8290cca`](https://github.com/lly8666/richman4-remastered/commit/991ca8290ccae6b974a35753ae70e68d5fd570cd) | MOD: preserve 64-bit money in stock transaction and bank event records |
| 2026-10-16 | [`be0994b096b1`](https://github.com/lly8666/richman4-remastered/commit/be0994b096b119f138dc4e78f104cd25c510efa1) | MOD: aggregate monthly payouts in int64 and safely reject overflow stock trades |
| 2026-10-16 | [`79305a64c7d5`](https://github.com/lly8666/richman4-remastered/commit/79305a64c7d5574cec34145a2f6d21836170669c) | MOD: safely bound auction bid by 64-bit cash and 32-bit auction interface |
| 2026-10-16 | [`dae420379322`](https://github.com/lly8666/richman4-remastered/commit/dae4203793222c683785888a678f8edc2ec1f315) | MOD: sum aggregate bank/financing/share totals in 64 bits without overload ambiguity |
| 2026-10-16 | [`7a26376cdd65`](https://github.com/lly8666/richman4-remastered/commit/7a26376cdd6573cec1e4be03b0a379ccb63e8b0e) | MOD: preserve high-precision money through core payments, bank actions and larger primary trades |
| 2026-10-16 | [`0dd701ba3311`](https://github.com/lly8666/richman4-remastered/commit/0dd701ba3311e414201addf0924a00e6b95e0681) | MOD: centralize checked signed-64-bit financial arithmetic and legacy display projection |
| 2026-10-16 | [`9684a49af866`](https://github.com/lly8666/richman4-remastered/commit/9684a49af8662a483519644b67c30a5a8559ab8c) | MOD: fix mixed-width bank repayment maximum expression |
| 2026-10-16 | [`cec7cc4e476a`](https://github.com/lly8666/richman4-remastered/commit/cec7cc4e476ae6b40bb3a87a183f95ccc82e6784) | MOD CI: run full native regression suite on financial limits and sidecar changes |
| 2026-10-16 | [`badbdf8fde88`](https://github.com/lly8666/richman4-remastered/commit/badbdf8fde8844e4a42d8c3a47c20d1f907169d9) | MOD: versioned checksum-bound 64-bit financial and stock sidecar; transactional restore and v1 compatibility |
| 2026-10-16 | [`93dd1ef31cf0`](https://github.com/lly8666/richman4-remastered/commit/93dd1ef31cf0d89a44540b90baf514f1a8c97024) | MOD: calculate true signed-64-bit net assets and expand stock quota scaling |
| 2026-10-16 | [`cf9a55aa9c6f`](https://github.com/lly8666/richman4-remastered/commit/cf9a55aa9c6fc766829f55698e0251d0ec7fb1e0) | MOD: define one-billion share per-stock holding safety limit |
| 2026-10-16 | [`79e718035add`](https://github.com/lly8666/richman4-remastered/commit/79e718035add446864ea14dc660205c39fbb318a) | MOD: transact 100m shares at 999999 using exact checked 64-bit money and wide counters |
| 2026-10-16 | [`5eb3eb6e0c4e`](https://github.com/lly8666/richman4-remastered/commit/5eb3eb6e0c4e1a4e61efec13fedfc012f4c9b25f) | MOD: initialize 10000x larger stock market and serialize saturated legacy DAT financial previews |
| 2026-10-16 | [`05d772f334b2`](https://github.com/lly8666/richman4-remastered/commit/05d772f334b24a54d2c6e0979091bfded0c61fe1) | MOD: widen money, holdings and stock supply in runtime; track 10000x market shares |
| 2026-10-16 | [`d0f5f2a2aa4d`](https://github.com/lly8666/richman4-remastered/commit/d0f5f2a2aa4d74c6a4f387fc6674e3500d046815) | MOD: widen money, holdings and stock supply in runtime; track 10000x market shares |
| 2026-10-16 | [`71e25d4ee354`](https://github.com/lly8666/richman4-remastered/commit/71e25d4ee3540e163b3370dc707e70d23dd99bb3) | MOD: widen money, holdings and stock supply in runtime; track 10000x market shares |
| 2026-10-16 | [`f56602651cfb`](https://github.com/lly8666/richman4-remastered/commit/f56602651cfbb84d051492d6dbccc5011edf4418) | MOD: widen money, holdings and stock supply in runtime; track 10000x market shares |
| 2026-10-16 | [`114ee37a2022`](https://github.com/lly8666/richman4-remastered/commit/114ee37a20220a14a40647d8e771422e338fce02) | MOD: widen money, holdings and stock supply in runtime; track 10000x market shares |
| 2026-10-16 | [`b2e600ea22d4`](https://github.com/lly8666/richman4-remastered/commit/b2e600ea22d48e5ce138966dc8e059e98edd1ecc) | MOD: add 900000000 start capital as eighth fund tier and resize/select dropdown safely |
| 2026-10-16 | [`a7209514fc37`](https://github.com/lly8666/richman4-remastered/commit/a7209514fc37d79bd8ac6773896d95e84b1dcd6a) | MOD: archive all 285 reachable Git development commits and migration milestones |
| 2026-10-15 | [`a779b7f9d31e`](https://github.com/lly8666/richman4-remastered/commit/a779b7f9d31ec511967240d7c4e8fa125f96e5a3) | MOD: fit 2-3 digit inventory tool counts without covering tool sprites |
| 2026-10-15 | [`17461d0979db`](https://github.com/lly8666/richman4-remastered/commit/17461d0979db164b2b69ca178f61b7cfb1b81c55) | Document MOD economics, original DAT sidecar requirement, overflow guards and separate Android app |
| 2026-10-15 | [`f6f6a9ba2d73`](https://github.com/lly8666/richman4-remastered/commit/f6f6a9ba2d73c580230973cb0dcc193c2bc6fcea) | MOD: verify actual DAT-backed game sessions reset inflation from 99 to 1 |
| 2026-10-15 | [`b57fe88b3cb8`](https://github.com/lly8666/richman4-remastered/commit/b57fe88b3cb80abd572ef4b28962e7856bd50a01) | MOD: assert price-index-one purchase and saturated bulk-sale points |
| 2026-10-15 | [`d39f182337a5`](https://github.com/lly8666/richman4-remastered/commit/d39f182337a510440a3700c80c551d5afd96f2f0) | MOD: assert price-index-one purchase and saturated bulk-sale points |
| 2026-10-15 | [`d0b55f2604be`](https://github.com/lly8666/richman4-remastered/commit/d0b55f2604bef1aac9057dbd93d49bf0dbbcf5a1) | MOD regression: reject overflowing stock bank/share counters and 999999-priced bulk sales |
| 2026-10-15 | [`2e2f9388fbcf`](https://github.com/lly8666/richman4-remastered/commit/2e2f9388fbcf1c7fff2e923702ff427e535f9d17) | MOD: update existing regression boundaries for widened inventory and price domain |
| 2026-10-15 | [`0ec5597ab78f`](https://github.com/lly8666/richman4-remastered/commit/0ec5597ab78f93c85a37eaba307d3a40df2f5037) | MOD: update existing regression boundaries for widened inventory and price domain |
| 2026-10-15 | [`6145f4746ad4`](https://github.com/lly8666/richman4-remastered/commit/6145f4746ad4eb1c4f074ff2b8ef119c0eb9c1a3) | MOD: update existing regression boundaries for widened inventory and price domain |
| 2026-10-15 | [`293daba98bb5`](https://github.com/lly8666/richman4-remastered/commit/293daba98bb504dc95b2fa4cf0ad89d16d847b54) | MOD: fix inflation for real saves/new games without invalidating synthetic oracle test fixtures |
| 2026-10-15 | [`6dc6a3df796b`](https://github.com/lly8666/richman4-remastered/commit/6dc6a3df796b26c3a9fdaf90eb5c84fd6299560f) | MOD: distinguish actual DAT-backed worlds from synthetic test fixtures for inflation normalization |
| 2026-10-15 | [`cfe95416c08b`](https://github.com/lly8666/richman4-remastered/commit/cfe95416c08bbaf763c2c461a0daea7cec0b2e8c) | MOD: separate Android package and launcher label to coexist with vanilla game |
| 2026-10-15 | [`9a852cddd7e9`](https://github.com/lly8666/richman4-remastered/commit/9a852cddd7e9e064a311a90fac39117bdea96aac) | MOD: separate Android package and launcher label to coexist with vanilla game |
| 2026-10-15 | [`ea5787773b57`](https://github.com/lly8666/richman4-remastered/commit/ea5787773b57fde4dd6b687adcff11c34d01228e) | MOD: credit large bulk-sales into 16-bit points without wrapping |
| 2026-10-15 | [`bbaad0f75a85`](https://github.com/lly8666/richman4-remastered/commit/bbaad0f75a85978065408e27fab672da1f76213d) | MOD: credit large bulk-sales into 16-bit points without wrapping |
| 2026-10-15 | [`4d8d0a21debe`](https://github.com/lly8666/richman4-remastered/commit/4d8d0a21debe627493059d26946e20dd4d5d586b) | MOD: saturate coupon points on bulk tool sales instead of 16-bit wrap |
| 2026-10-15 | [`055b27f7a7f3`](https://github.com/lly8666/richman4-remastered/commit/055b27f7a7f3ed5cf8e5cec93076cbcff378cb69) | MOD regression tests for wide inventory sidecar integrity and fixed price index |
| 2026-10-15 | [`f1ea9d938bcc`](https://github.com/lly8666/richman4-remastered/commit/f1ea9d938bccbc88044f87efd340c5090e961590) | MOD: update oracle helpers and expected 999-tool / 999999-stock limits |
| 2026-10-15 | [`a8593d8895bb`](https://github.com/lly8666/richman4-remastered/commit/a8593d8895bbd64db835e84d88882e9c9b703703) | MOD: update oracle helpers and expected 999-tool / 999999-stock limits |
| 2026-10-15 | [`4bc0e06f2304`](https://github.com/lly8666/richman4-remastered/commit/4bc0e06f2304997ae4474dd5a6738eaf8139dfca) | MOD: update oracle helpers and expected 999-tool / 999999-stock limits |
| 2026-10-15 | [`f203ab6e023b`](https://github.com/lly8666/richman4-remastered/commit/f203ab6e023b78b0fcd863c5da04a8f470c396de) | MOD: cap all direct tool grants and item-shop selectors at 999 |
| 2026-10-15 | [`513c3911021e`](https://github.com/lly8666/richman4-remastered/commit/513c3911021e10ac6ab2476bbf243ea509fa6c51) | MOD: cap all direct tool grants and item-shop selectors at 999 |
| 2026-10-15 | [`8316f63b82f4`](https://github.com/lly8666/richman4-remastered/commit/8316f63b82f44eaaccb2b48342d83710bf7d690b) | MOD: cap all direct tool grants and item-shop selectors at 999 |
| 2026-10-15 | [`41b4845038b6`](https://github.com/lly8666/richman4-remastered/commit/41b4845038b6dfd05fb1a246395b96950f412bec) | MOD: cap all direct tool grants and item-shop selectors at 999 |
| 2026-10-15 | [`11e143929f37`](https://github.com/lly8666/richman4-remastered/commit/11e143929f37d312c3f642a0ec028ddbd087ff8d) | MOD: cap all direct tool grants and item-shop selectors at 999 |
| 2026-10-15 | [`9c1a02b7085f`](https://github.com/lly8666/richman4-remastered/commit/9c1a02b7085f6e987c403971b3c3bbc58e929a11) | MOD: cap all direct tool grants and item-shop selectors at 999 |
| 2026-10-15 | [`a7cc0b849da9`](https://github.com/lly8666/richman4-remastered/commit/a7cc0b849da939df080a56382ce7dae7723cb07e) | MOD: centralize 999 tool cap for transport return and event grants |
| 2026-10-15 | [`88b801a6b804`](https://github.com/lly8666/richman4-remastered/commit/88b801a6b80460ff630e0d25910872d470d3a543) | MOD: load and save checksum-linked wide tool inventories beside vanilla-format DAT |
| 2026-10-15 | [`eb83bb1861ae`](https://github.com/lly8666/richman4-remastered/commit/eb83bb1861ae2803ec17beba4337ceacf5e41385) | MOD: safely clamp DAT tool byte fields while keeping full 999 count in companion extension |
| 2026-10-15 | [`e9ee6b0b0d2e`](https://github.com/lly8666/richman4-remastered/commit/e9ee6b0b0d2e39c957b5e1d3e9beec288e6322c5) | MOD: lossless checksummed companion save for 999 tools without altering vanilla DAT layout |
| 2026-10-15 | [`91bc5bc656e5`](https://github.com/lly8666/richman4-remastered/commit/91bc5bc656e54c1570acb00b565b6d9c3e88456f) | Preserve 200000 default after adding MOD 30000000 start-fund option |
| 2026-10-15 | [`879ce3997875`](https://github.com/lly8666/richman4-remastered/commit/879ce3997875891deec4c0d73e6703cc110396cd) | MOD: add 30,000,000 initial-fund option and expand seven-entry start menu |
| 2026-10-15 | [`0e39ae30f375`](https://github.com/lly8666/richman4-remastered/commit/0e39ae30f375fbfb42100b36cfbc54f843330a0e) | MOD: prevent high-price share trades and wealth computations from wrapping negative balances |
| 2026-10-15 | [`1514efa2cb08`](https://github.com/lly8666/richman4-remastered/commit/1514efa2cb0819e47c372e7a7052197cd529e084) | MOD: normalize inflation index on loading and each monthly update |
| 2026-10-15 | [`043ab450630f`](https://github.com/lly8666/richman4-remastered/commit/043ab450630fa3f89c0922a9c19b47cc7adc2f3d) | MOD: support 999999 stock quotes and hold inflation index at 1 |
| 2026-10-15 | [`3f019602e5b5`](https://github.com/lly8666/richman4-remastered/commit/3f019602e5b5023b7f07ba088d90a8c3ae09b943) | MOD: support 999999 stock quotes and hold inflation index at 1 |
| 2026-10-15 | [`cd99dccdf989`](https://github.com/lly8666/richman4-remastered/commit/cd99dccdf98905f245492aa94eae72ca9a4850bb) | MOD: support 999999 stock quotes and hold inflation index at 1 |
| 2026-10-15 | [`5bb8ce90ff9f`](https://github.com/lly8666/richman4-remastered/commit/5bb8ce90ff9f0cc03cfa6c04f346fe08ec63dd9d) | MOD: support 999999 stock quotes and hold inflation index at 1 |
| 2026-10-15 | [`483afd758d35`](https://github.com/lly8666/richman4-remastered/commit/483afd758d35ab71b14a4604a64c0655a682a6a5) | MOD: widen tool counts to uint16 and enforce 999 cap in inventory/shopping |
| 2026-10-15 | [`272c657ba7e9`](https://github.com/lly8666/richman4-remastered/commit/272c657ba7e902451e98bbf079c6398acc03d7a8) | MOD: widen tool counts to uint16 and enforce 999 cap in inventory/shopping |
| 2026-10-15 | [`a1b0854e9343`](https://github.com/lly8666/richman4-remastered/commit/a1b0854e9343882e8216119a8e5549b9671474f7) | MOD: widen tool counts to uint16 and enforce 999 cap in inventory/shopping |
| 2026-10-15 | [`e2805a653609`](https://github.com/lly8666/richman4-remastered/commit/e2805a6536090c58f884171295d094159da9eff3) | MOD: widen tool counts to uint16 and enforce 999 cap in inventory/shopping |
| 2026-10-15 | [`660da96ac612`](https://github.com/lly8666/richman4-remastered/commit/660da96ac6127da5998f8dc58cbb45e38fdc79de) | MOD: widen tool counts to uint16 and enforce 999 cap in inventory/shopping |
| 2026-10-15 | [`81d12df38c05`](https://github.com/lly8666/richman4-remastered/commit/81d12df38c058f6b3026420dbee4cc8a2ba74a67) | Declare isolated MOD branch gameplay limits centrally |
| 2026-10-15 | [`f87e8dcaaf6a`](https://github.com/lly8666/richman4-remastered/commit/f87e8dcaaf6a3fc14c9489829162cf7d0bfeaf7c) | Document original god card gain/loss boxes and hidden carried sprite until movie completes |
| 2026-10-15 | [`4d7f8a615d56`](https://github.com/lly8666/richman4-remastered/commit/4d7f8a615d563cd35433877779d30aa13329e8a9) | Document corrected research icon centres aligned with Panel 11 selection cells |
| 2026-10-15 | [`17d0173ff98d`](https://github.com/lly8666/richman4-remastered/commit/17d0173ff98dcc639fcbf13a971e3245a58a7206) | Regression-test god intro movie, caption and actual received card-name notice ordering |
| 2026-10-15 | [`8ae20ed00c61`](https://github.com/lly8666/richman4-remastered/commit/8ae20ed00c619782e5c31015b25faf40613ab4b6) | Regress fortune/poverty gods show exact earned/lost cards after movie and caption |
| 2026-10-15 | [`fd1eb04bd4de`](https://github.com/lly8666/richman4-remastered/commit/fd1eb04bd4dedff5294e4081a10044d1abc7b6b8) | Hide incoming carried god even behind earlier queued animations |
| 2026-10-15 | [`17ee31400ead`](https://github.com/lly8666/richman4-remastered/commit/17ee31400eadef0d37eec0c3e39f189e9d7c4875) | Keep newly attached god visually absent through queued entrance movie and arrival subtitle |
| 2026-10-15 | [`078fd08ec5d6`](https://github.com/lly8666/richman4-remastered/commit/078fd08ec5d6ea67f6324b450c2e4ca64d6bab52) | Suppress newly attached god sprite throughout the entrance film and lingering caption |
| 2026-10-15 | [`d127ad995104`](https://github.com/lly8666/richman4-remastered/commit/d127ad995104a39428b20b0f2a45cebfa01c06db) | Expose arriving-god suppression in board and effect scene rendering |
| 2026-10-15 | [`920b5ecd67ba`](https://github.com/lly8666/richman4-remastered/commit/920b5ecd67ba0716a9e4e57d7752eff38ec697aa) | Use actual escaped newlines in original two-line god card message boxes |
| 2026-10-15 | [`9255281ad2e6`](https://github.com/lly8666/richman4-remastered/commit/9255281ad2e6465c015fce9fa98e1dd5ac90c62f) | Show original named card rewards and losses after fortune/poverty god intro |
| 2026-10-15 | [`69cf3f4a455e`](https://github.com/lly8666/richman4-remastered/commit/69cf3f4a455e76a1790116369a4bb8e4ef8a2ea8) | Queue original post-animation god card reward/loss dialogs before prize speech |
| 2026-10-15 | [`31b12a014270`](https://github.com/lly8666/richman4-remastered/commit/31b12a01427056f19a8c6154d69d85931c4f454b) | Add presentation-only original god inventory result announcement |
| 2026-10-15 | [`fb7621011d57`](https://github.com/lly8666/richman4-remastered/commit/fb7621011d57f0c35dfde8a79fa411b21a0f58cd) | Send precise card gain/loss notices for fortune/poverty/death gods after the arrival caption |
| 2026-10-15 | [`488e114dbcc5`](https://github.com/lly8666/richman4-remastered/commit/488e114dbcc52cb071f95e7950012d20f47bbbc2) | Expose god card gain/loss messages without changing inventory or RNG |
| 2026-10-15 | [`df42acafc94a`](https://github.com/lly8666/richman4-remastered/commit/df42acafc94aec1d14fb764a285512dc33da1283) | Center research tool sprites inside background selection cells instead of inventory count slots |
| 2026-10-15 | [`bddec53501a1`](https://github.com/lly8666/richman4-remastered/commit/bddec53501a19fcae19e31ab1e5d6f41268c9a66) | Document research picker overlap fix and separated touch-button coordinates |
| 2026-10-15 | [`df121b9083a1`](https://github.com/lly8666/richman4-remastered/commit/df121b9083a15b951429149335249f5700347df4) | Record and fix missing original magic-house building rise movie |
| 2026-10-15 | [`2990c5136ae6`](https://github.com/lly8666/richman4-remastered/commit/2990c5136ae6f2bab402288d85b8cf7c9a25f784) | Play original Data 553 build-rise at magic-house upgrade commit frame 44 |
| 2026-10-15 | [`2c38d9fd141a`](https://github.com/lly8666/richman4-remastered/commit/2c38d9fd141a4fb827269264638a5c4002330f54) | Document player-info 17px misalignment and inverted AI-settings selected-tab sprite |
| 2026-10-15 | [`4e745fde5e40`](https://github.com/lly8666/richman4-remastered/commit/4e745fde5e40d021d185b393e1953d935f443b1d) | Use original selected-tab sprite 77:2 instead of inverted normal tab in AI settings |
| 2026-10-15 | [`8faf10eb84cf`](https://github.com/lly8666/richman4-remastered/commit/8faf10eb84cfd1f6ce2749f86e3f33ecfacd5783) | Align player-info inventory icons and card text with original y=281/385 anchors |
| 2026-10-15 | [`b804e477f049`](https://github.com/lly8666/richman4-remastered/commit/b804e477f0494af8bf580b02f78d83b072e7c30b) | Audit all road squares and 30 cards against original presentation order and document remaining gaps |
| 2026-10-15 | [`b9c05c4b8ebd`](https://github.com/lly8666/richman4-remastered/commit/b9c05c4b8ebd2c472169983b7e6d32e5d963e794) | Regression test land god claims unowned plots and still upgrades own plots for remapped object IDs |
| 2026-10-15 | [`2f17a8f72a39`](https://github.com/lly8666/richman4-remastered/commit/2f17a8f72a39749f1ba9a5c52172aa856fa7d3ad) | Resolve land/angel god post-property effects by object kind, not inventory object ID |
| 2026-10-15 | [`af6c1d4f691c`](https://github.com/lly8666/richman4-remastered/commit/af6c1d4f691c633795369266ecd7c3d896880813) | Use attached god kind for toll-exemption reason label |
| 2026-10-15 | [`2ae3259d37a8`](https://github.com/lly8666/richman4-remastered/commit/2ae3259d37a81a2d3905f91bbcbdb0f309804655) | Test robot and demolition movies carry pre-upgrade levels and ownership for commit frames |
| 2026-10-15 | [`75bf5b7db426`](https://github.com/lly8666/richman4-remastered/commit/75bf5b7db426162e711d4f8e19d9b4475f19594a) | Hide pre-committed building changes during queued speeches and construction/demolition movies |
| 2026-10-15 | [`18ee7482d08f`](https://github.com/lly8666/richman4-remastered/commit/18ee7482d08f870438cad7a6e70ed5cf3d694e91) | Render movie-specific old building until commit frame even across multiple queued changes |
| 2026-10-15 | [`d9700fcad608`](https://github.com/lly8666/richman4-remastered/commit/d9700fcad6081d39627379fbdadf1d576d83f9a7) | Expose pre-commit property preview for pending and playing building FLCs |
| 2026-10-15 | [`9183b07fb98e`](https://github.com/lly8666/richman4-remastered/commit/9183b07fb98e70cf8fe7544db84ea2c5b6e5426b) | Capture old building state for robot construction, demolition cards, monsters and demon smoke |
| 2026-10-15 | [`ed518d680f97`](https://github.com/lly8666/richman4-remastered/commit/ed518d680f9711b56f1a13760727eb5fddc3eb04) | Record pre-animation building ownership and levels for commit-frame accurate display |
| 2026-10-15 | [`c2ee7d2d839d`](https://github.com/lly8666/richman4-remastered/commit/c2ee7d2d839dc894bd05e64fa7a8ffd9a5742b33) | Fix research icon tray overlap by moving touch controls to unused sidebar |
| 2026-10-15 | [`06b15cee7788`](https://github.com/lly8666/richman4-remastered/commit/06b15cee7788bb4b752e0501d64a60e6c97530c5) | Test land god on own property prompts upgrade even when object ID differs from kind |
| 2026-10-15 | [`5974ad7f4c69`](https://github.com/lly8666/richman4-remastered/commit/5974ad7f4c694efc9edfb8ed9c9b62cf39f19e0f) | Fix 土地公 own-property upgrade and resolve all god effects by object kind instead of ID |
| 2026-10-15 | [`f8d67b5ba844`](https://github.com/lly8666/richman4-remastered/commit/f8d67b5ba8449800951afa9c033e59204d92d3bd) | Document original GetTickCount reseeding on interactive reload and prior rollback |
| 2026-10-15 | [`c2241735664f`](https://github.com/lly8666/richman4-remastered/commit/c2241735664fd55d557920ac8536c39446de6d28) | Regress repeating same DAT load with fresh RNG stream while rules remain unchanged |
| 2026-10-15 | [`3139f528bb7f`](https://github.com/lly8666/richman4-remastered/commit/3139f528bb7f87a5b03b521802b768166cd6f7b1) | Test fresh reload clock seeding while retaining reproducible headless RNG |
| 2026-10-15 | [`0d682291ad34`](https://github.com/lly8666/richman4-remastered/commit/0d682291ad3456334adc2531c16b94f4019185cf) | Reseed interactive loaded games from monotonic clock as original GetTickCount; keep replay sidecars |
| 2026-10-15 | [`47e7b56d9894`](https://github.com/lly8666/richman4-remastered/commit/47e7b56d98948d00ae4dd11cb7026dfdd95ce85a) | Add original-style clock reseed sequence for interactive save loads |
| 2026-10-15 | [`456945871777`](https://github.com/lly8666/richman4-remastered/commit/4569458717776fa28883b4dc7b5bd61a276a4bc8) | Revert misdiagnosed dice animation fix; investigate repeated rolls on loading same save |
| 2026-10-15 | [`27ebeb90469e`](https://github.com/lly8666/richman4-remastered/commit/27ebeb90469e4924a70b42e285a404f8761ae70b) | Revert misdiagnosed dice animation fix; investigate repeated rolls on loading same save |
| 2026-10-15 | [`cfa19339176f`](https://github.com/lly8666/richman4-remastered/commit/cfa19339176f1a5e916de991efe45f470a4e2bce) | Revert misdiagnosed dice animation fix; investigate repeated rolls on loading same save |
| 2026-10-15 | [`ff29fe546bd2`](https://github.com/lly8666/richman4-remastered/commit/ff29fe546bd233fc4ccc11045c4db4bf631d739a) | Revert misdiagnosed dice animation fix; investigate repeated rolls on loading same save |
| 2026-10-15 | [`109cde5655f8`](https://github.com/lly8666/richman4-remastered/commit/109cde5655f8baff3b4b74ce76b54033553191db) | Revert misdiagnosed dice animation fix; investigate repeated rolls on loading same save |
| 2026-10-15 | [`67a3bae60f7a`](https://github.com/lly8666/richman4-remastered/commit/67a3bae60f7a246542f07dcc8468dab431e7b787) | Document actual loaded-game dice replay bug, RNG sidecar and multi-die behavior |
| 2026-10-15 | [`085dcdc033e4`](https://github.com/lly8666/richman4-remastered/commit/085dcdc033e4cc3d9b66c150ecf4ff89f80d7f3b) | Regression test loading original DAT retains three-die car rolls |
| 2026-10-15 | [`23f2095d9cd7`](https://github.com/lly8666/richman4-remastered/commit/23f2095d9cd724cbe911f7030da78cf148e2002d) | Allow coincidental equal first and last dice faces in loaded-turn regression |
| 2026-10-15 | [`9dd8f87426d1`](https://github.com/lly8666/richman4-remastered/commit/9dd8f87426d19ca571eb718d502b0629e2bb11ac) | Regress loaded-game multi-player dice rolls and event-local face snapshots |
| 2026-10-15 | [`4e88c17a2bee`](https://github.com/lly8666/richman4-remastered/commit/4e88c17a2bee7f2b6e5d2f2e4217ee06f426b784) | Play each dice animation with its own recorded result rather than latest roll |
| 2026-10-15 | [`e9ed1d0f17d1`](https://github.com/lly8666/richman4-remastered/commit/e9ed1d0f17d1dc83af058831001cc4c7890d7f64) | Attach original dice faces to their own events before subsequent turns |
| 2026-10-15 | [`327b85c61853`](https://github.com/lly8666/richman4-remastered/commit/327b85c61853ae491c4f2508c474d6be53fe957a) | Snapshot per-event dice results rather than relying on current session faces |
| 2026-10-15 | [`4202d7fa7b09`](https://github.com/lly8666/richman4-remastered/commit/4202d7fa7b097712f89f31581414e22c863e9d9b) | Audit original animation sequencing for every effect kind and document known visual gaps |
| 2026-10-15 | [`c2ffa1ad267d`](https://github.com/lly8666/richman4-remastered/commit/c2ffa1ad267da477dfbcaa9300c3426abb482c04) | Regression test missile and nuclear movie before ambulance/hospital presentation |
| 2026-10-15 | [`f9147d8cbc0d`](https://github.com/lly8666/richman4-remastered/commit/f9147d8cbc0de9496a4ebe17541a5f23a00c3849) | Queue missile and nuclear explosion before hospitalization settlement presentation |
| 2026-10-15 | [`c1a4b2e79222`](https://github.com/lly8666/richman4-remastered/commit/c1a4b2e7922250682ddc1e33fd2c4e95bb79693b) | Document self-teleport landing and roll-consumption behavior |
| 2026-10-15 | [`bbcb3041d6e7`](https://github.com/lly8666/richman4-remastered/commit/bbcb3041d6e74cc580f840d78093327225b17e4f) | Regress self teleport house upgrades, shop visits, other targets and time-machine history |
| 2026-10-15 | [`ab8e7f3d6ee5`](https://github.com/lly8666/richman4-remastered/commit/ab8e7f3d6ee5ebab5a214ac6bd80822efc834dab) | Resolve self-teleport as a completed roll with normal destination landing |
| 2026-10-15 | [`ac292ec39336`](https://github.com/lly8666/richman4-remastered/commit/ac292ec39336c8ded7a6ceff4569562e344b1dc6) | Document original-art institute research picker and level-lock behavior |
| 2026-10-15 | [`cd47e3cb558f`](https://github.com/lly8666/richman4-remastered/commit/cd47e3cb558f49bc85a66e07b34433c386345a65) | Clear shared facility/research selection for every new decision |
| 2026-10-15 | [`fc5222ef4b66`](https://github.com/lly8666/richman4-remastered/commit/fc5222ef4b669715d31580c9cf6cfdee8296b8a6) | Remove remaining retired research selection state after picker reuse |
| 2026-10-15 | [`2cdf3ea254c3`](https://github.com/lly8666/richman4-remastered/commit/2cdf3ea254c33ae7606ae417a3b8629cb79e62b1) | Replace research-only native modal with shared original-art facility picker and gallery fixtures |
| 2026-10-15 | [`36f924f2722f`](https://github.com/lly8666/richman4-remastered/commit/36f924f2722f3948c0c196f77d82a43adbd33ede) | Document shared original-style facility and research picker interactions |
| 2026-10-15 | [`cd61813ceaa5`](https://github.com/lly8666/richman4-remastered/commit/cd61813ceaa50058db688ed07eeb039ed9b9d001) | Render research choices with original facility picker board and tool sprites |
| 2026-10-15 | [`fa1002ac6035`](https://github.com/lly8666/richman4-remastered/commit/fa1002ac603597da39153502c00124fb03cba25e) | Apply and remember window layout from in-game and title settings |
| 2026-10-15 | [`084a686ae98a`](https://github.com/lly8666/richman4-remastered/commit/084a686ae98a417b1456d1231af639f8099d825e) | Allow options screen to select existing main-screen view modes |
| 2026-10-15 | [`22347648f4fd`](https://github.com/lly8666/richman4-remastered/commit/22347648f4fd61617bb0e3f7cd8fa71d3ffebb13) | Activate touch-friendly window-layout radio buttons in options menu |
| 2026-10-15 | [`33c128f3a5cd`](https://github.com/lly8666/richman4-remastered/commit/33c128f3a5cd84b05a9bc47ea7a9e2a78c26756d) | Expose original three window layouts in options state |
| 2026-10-15 | [`fca15be371dd`](https://github.com/lly8666/richman4-remastered/commit/fca15be371ddc7df0f2bf7977882980935796dbd) | fix(android): map system Back and Escape to postpone research instead of leaving modal |
| 2026-10-15 | [`c9a6ed3f6866`](https://github.com/lly8666/richman4-remastered/commit/c9a6ed3f68660c6f25e97963ab9a2dcdd84762fe) | test(research): serialize world snapshot without requiring a settled player turn |
| 2026-10-15 | [`6f188752d733`](https://github.com/lly8666/richman4-remastered/commit/6f188752d7339e1ac0e9990d394a0e73e06345bf) | test(research): pass SaveFile directly when checking persistence |
| 2026-10-15 | [`d01d46557188`](https://github.com/lly8666/richman4-remastered/commit/d01d4655718802c82a5cb5960dc2cf26446a11a2) | ui(research): show current project and days remaining in institute tile popup |
| 2026-10-15 | [`cc788cda911e`](https://github.com/lly8666/richman4-remastered/commit/cc788cda911e930da53ae8336828c5c5ec164d30) | ci: run portable Richman4 research and gameplay regression tests |
| 2026-10-15 | [`780a84b2cda2`](https://github.com/lly8666/richman4-remastered/commit/780a84b2cda2a3f4599597b0ccbf4dcf4ed6365d) | test(research): cover post-build prompt, level unlock, save roundtrip, five-day tool award |
| 2026-10-15 | [`233487f9263f`](https://github.com/lly8666/richman4-remastered/commit/233487f9263fccd75aa2dd4cdadeb8d3f3c384e4) | fix(android): add large two-tap research project picker with confirm button |
| 2026-10-15 | [`bc74b33c5e97`](https://github.com/lly8666/richman4-remastered/commit/bc74b33c5e979324861e3b42a539785e1fe42bcc) | fix(research): prompt for researched tools, advance 5-day projects, and award completed items |
| 2026-10-15 | [`c8d33212f5f5`](https://github.com/lly8666/richman4-remastered/commit/c8d33212f5f5dc583945faad0e679604820556e6) | fix(research): expose research-tool decision kind |
| 2026-10-15 | [`d11c7e257c08`](https://github.com/lly8666/richman4-remastered/commit/d11c7e257c0865dc7572d3b4a76c906766c85164) | fix(research): round-trip original research product byte in saves |
| 2026-10-15 | [`6c571181ca80`](https://github.com/lly8666/richman4-remastered/commit/6c571181ca80d6685a800c266a1615948d53cfed) | fix(research): decode and preserve research institute project field |
| 2026-10-06 | [`3f4209a7dfb5`](https://github.com/lly8666/richman4-remastered/commit/3f4209a7dfb5b2f20799bf54e6c03c5faa11c557) | docs: migration scale (158k lines of x86 assembly to ~32k lines of C++) |
| 2026-10-06 | [`8dbf0fe23f97`](https://github.com/lly8666/richman4-remastered/commit/8dbf0fe23f97bc965db948321d25696c99a35581) | Update README |
| 2026-10-06 | [`c590a26d544e`](https://github.com/lly8666/richman4-remastered/commit/c590a26d544e9e9bff36dbcc3f7f59f4e883a5e8) | docs: game-assets/README with placement and per-platform build steps, CONTRIBUTING, upstream acknowledgement |
| 2026-10-06 | [`f58a0140fb5c`](https://github.com/lly8666/richman4-remastered/commit/f58a0140fb5cf88e3260fbaee61867a44232e988) | docs: legal notice (Traditional Chinese + English), Steam link, complete third-party notices |
| 2026-10-06 | [`d00cb9acba46`](https://github.com/lly8666/richman4-remastered/commit/d00cb9acba466742bb9bffe490210b2533366554) | hospital/prison: original YES/NO strip and the game's EXIT button art |
| 2026-10-06 | [`1be730d15ab4`](https://github.com/lly8666/richman4-remastered/commit/1be730d15ab4956b5dd334eaa35b9736e3020781) | lottery: keep the bar up for 拜拜！祝您中獎！ after a pick, as the original |
| 2026-10-06 | [`6aec1a17c77c`](https://github.com/lly8666/richman4-remastered/commit/6aec1a17c77cbe0caf73636aa0bc8fe95bb989fa) | gods: original rise-and-spin animation when a god leaves (also when replaced) |
| 2026-10-06 | [`2adc147a2cbb`](https://github.com/lly8666/richman4-remastered/commit/2adc147a2cbb87235c93dda78325faf668a5967a) | text: decode original names as CP950 so 張恒市 (ETEN 0xF9DA) shows on Android |
| 2026-10-05 | [`b0f57eb58fef`](https://github.com/lly8666/richman4-remastered/commit/b0f57eb58fef10e56a5d080bbbdfa5b04ab30630) | 紅卡/黑卡: pick the company on the stock market page; computer uses show 對%s使用%s！ |
| 2026-10-05 | [`a3ebd885abe1`](https://github.com/lly8666/richman4-remastered/commit/a3ebd885abe132a53722f734a4d540a32d34ec32) | 搶奪卡: original card/tool grid picker for humans and 搶得 box for computers |
| 2026-10-05 | [`690cf206a364`](https://github.com/lly8666/richman4-remastered/commit/690cf206a364303f9f1ef71814aebb335825dfe5) | facility build: humans may build a 公園 (slot 0), as in the original picker |
| 2026-10-05 | [`4c84695eb003`](https://github.com/lly8666/richman4-remastered/commit/4c84695eb0033ad98fa5e276c32549446f1918da) | replays: fixed start date (--date, default 2026-10-05) keeps automated games reproducible |
| 2026-10-05 | [`8cef186409c1`](https://github.com/lly8666/richman4-remastered/commit/8cef186409c167a33f03a35b4b6d5d3e8abf5757) | Merge original facility-type picker for big lots and 改建卡 |
| 2026-10-05 | [`a1778788f8f2`](https://github.com/lly8666/richman4-remastered/commit/a1778788f8f2e24b26b7c75f142327c22148d4f4) | Merge player colour conversion fix, confined-owner plots and mini-game points box |
| 2026-10-05 | [`67e46023daa4`](https://github.com/lly8666/richman4-remastered/commit/67e46023daa43f2d388d13ec0d4c080ab7d961e6) | GO and turn-only controls wait until earlier turns have been shown |
| 2026-10-05 | [`0d99e0740f3d`](https://github.com/lly8666/richman4-remastered/commit/0d99e0740f3d3142ddf9119735b4b6429a45b708) | Original facility-type picker (Data 517:4 strip) for big-lot builds and 改建卡 |
| 2026-10-05 | [`072b7426eefe`](https://github.com/lly8666/richman4-remastered/commit/072b7426eefee8f389f8b1843cfb39d4d9e3557c) | Mini-game 得點券 box; player colours from the character dword everywhere |
| 2026-10-05 | [`6a74bb8ac3a4`](https://github.com/lly8666/richman4-remastered/commit/6a74bb8ac3a48af744c8ee08bf64939393bf20ad) | quit flow: in-game 結束遊戲 returns to the title; title EXIT quits the app on Android and iOS |
| 2026-10-05 | [`9106a220a60a`](https://github.com/lly8666/richman4-remastered/commit/9106a220a60a81f0a3279a5912273a394b9ef3cf) | monthly: bubble text offsets match the original (dx/dy were swapped) |
| 2026-10-05 | [`bd08d0ac4c1f`](https://github.com/lly8666/richman4-remastered/commit/bd08d0ac4c1fe6ccad753c668e0bbdbb923a02d0) | help: draw body text without the same-colour shadow (reads as a blurry double image when scaled) |
| 2026-10-05 | [`829225e3de40`](https://github.com/lly8666/richman4-remastered/commit/829225e3de40c7575d8d89ea9ab7c894acca395e) | computer tool use: original 使用%s announcement box before the tool runs |
| 2026-10-05 | [`2b8a3c07008f`](https://github.com/lly8666/richman4-remastered/commit/2b8a3c07008fb8ebd9079fa1f78be3716e6dd490) | Merge original landing pauses, god follow offsets, entrance caption, throw landing and hospital/jail ordering |
| 2026-10-05 | [`213a84473af5`](https://github.com/lly8666/richman4-remastered/commit/213a84473af592144435e631624c17cc0b5aab3f) | Presentation timing: landing pauses, trailing gods, god caption over the movie, hospital/jail order, throws |
| 2026-10-05 | [`d077c8d77c5f`](https://github.com/lly8666/richman4-remastered/commit/d077c8d77c5fb80c4ef920ca544f2ebfead0bb94) | Merge iOS app shell (SDL3, bundled assets, haptics, icon, build_ios.sh) |
| 2026-10-05 | [`b2c6bac01c86`](https://github.com/lly8666/richman4-remastered/commit/b2c6bac01c8642976176510c9666028866777bfd) | ios: CMake/Xcode app shell for iPhone and iPad (simulator by default, device with RICH4_IOS_TEAM) |
| 2026-10-05 | [`05c42e2ddce7`](https://github.com/lly8666/richman4-remastered/commit/05c42e2ddce7070a23c5fd95455e0b7b4852ede1) | Merge audio-thread mixing with a smooth limiter (fixes bursts and clicks) |
| 2026-10-05 | [`8d6ca5391b25`](https://github.com/lly8666/richman4-remastered/commit/8d6ca5391b25761ff932099219e7b6c8fa837891) | audio: mix on SDL's audio thread with a smooth limiter (fixes bursts/clicks) |
| 2026-10-05 | [`83363d06c56d`](https://github.com/lly8666/richman4-remastered/commit/83363d06c56d7c5ccc8fd22d6b67bc9735ff7e77) | audio: glitch counters (underruns, clipping, main-thread gaps) and dev WAV capture |
| 2026-10-05 | [`b834a6027d37`](https://github.com/lly8666/richman4-remastered/commit/b834a6027d37c3e5b46ba421c12090eca86d20b5) | haptics: light key-tap feedback on every button press (Android; hook for iOS) |
| 2026-10-05 | [`37968d56dce9`](https://github.com/lly8666/richman4-remastered/commit/37968d56dce906b4c79383d9c32ae4e7113492fe) | docs: status after asset-coverage audit fixes (gate 9a4e16f) |
| 2026-10-05 | [`9a4e16fb1d44`](https://github.com/lly8666/richman4-remastered/commit/9a4e16fb1d441659feda44a1f05e6cf8758a0860) | Merge original board movies: ambulance, police, dog, UFO/airplane, parachute, news, ZZZ, card throw, pickups, 工程車, gowns, holidays |
| 2026-10-05 | [`f0b0431458d5`](https://github.com/lly8666/richman4-remastered/commit/f0b0431458d5735777204eb99c0cca650ac3bbcf) | 託管: autosave before handing a waiting human to the AI |
| 2026-10-05 | [`efe2cdaa7358`](https://github.com/lly8666/richman4-remastered/commit/efe2cdaa735851c4d10d1ff0b742072b167701ee) | Merge magic-house cast, auction poses, remote-dice picker, frame-up chooser, NPC thanks and AI settings |
| 2026-10-05 | [`317f1a34f88c`](https://github.com/lly8666/richman4-remastered/commit/317f1a34f88c2ebaf2acc0559a65bb46bd481fb8) | audio: monthly draw/report and mini-game context music via the music controller |
| 2026-10-05 | [`d7e531a95092`](https://github.com/lly8666/richman4-remastered/commit/d7e531a950921b6ff352c59d157121397a33f804) | Original small screens: magic cast movie, auction poses, remote dice, 嫁禍 chooser, neon, NPC thanks, 託管AI |
| 2026-10-05 | [`3ac27a592a89`](https://github.com/lly8666/richman4-remastered/commit/3ac27a592a89f14670de2dfbab6cb7b46e43bba0) | Merge original context music, walk loops, clerk voices, toll-waived box and lot tints |
| 2026-10-05 | [`941fdfdaf593`](https://github.com/lly8666/richman4-remastered/commit/941fdfdaf59384e30ea30b0dac0aceccc705e3fc) | Replays: drive other screens while a board movie waits for the game screen |
| 2026-10-05 | [`04e4d0fd84ce`](https://github.com/lly8666/richman4-remastered/commit/04e4d0fd84ce35b2df66f33fee701c8e576d19bc) | Merge rule dead-end fixes, Traditional prompts with real names, and the tile info popup |
| 2026-10-05 | [`eac5b8bbc3e0`](https://github.com/lly8666/richman4-remastered/commit/eac5b8bbc3e0c6d671cafbbf0b010f6952ccf95d) | Merge the three original interactive mini-games |
| 2026-10-05 | [`71ed7ce9395d`](https://github.com/lly8666/richman4-remastered/commit/71ed7ce9395d9724acbaf4a4b55625c43c224837) | audio: walk/engine loops, context music stack, clerk voices, toll-waiver box, board cues, lot tint |
| 2026-10-05 | [`f977924d5a5a`](https://github.com/lly8666/richman4-remastered/commit/f977924d5a5a03baaa0a72a301bab37a5d7b2f75) | board: map-tile info popup (fcn_00417559) with board::unproject |
| 2026-10-05 | [`b901b25cdd2c`](https://github.com/lly8666/richman4-remastered/commit/b901b25cdd2c272923ecfcdffe20e71ce0fd2939) | Board movies: ambulance/police, dog, UFO/airplane, parachute, news, card throw, pickups, 工程車 demolish, holidays, ZZZ, gown/stripes |
| 2026-10-05 | [`adb3e794855a`](https://github.com/lly8666/richman4-remastered/commit/adb3e794855aa592751bd3fc4bdcf604c26fd94d) | Merge original end screens, Help window and festival calendar pictures |
| 2026-10-05 | [`fb332733dd90`](https://github.com/lly8666/richman4-remastered/commit/fb332733dd90279db8600fa5ab6f6dbe188d77ef) | Interactive mini-games: 企鵝挖寶, 七彩氣球 and 喜從天降 from small_games.asm |
| 2026-10-05 | [`a0f4dad45ff7`](https://github.com/lly8666/richman4-remastered/commit/a0f4dad45ff7eb1dd354385ec7936b70d75ab534) | End screens, Help window and festival calendar pictures from the original |
| 2026-10-05 | [`ba32fa6be1f6`](https://github.com/lly8666/richman4-remastered/commit/ba32fa6be1f6b48d906c0f71419b633c9c48dcdb) | Merge monthly dividend payout and original monthly report, dividend and lottery draw screens |
| 2026-10-05 | [`d45236096b50`](https://github.com/lly8666/richman4-remastered/commit/d45236096b50ad0efb76dad11e1ebfd7b8eec57b) | monthly: original dividend table, lottery draw show and month-end bank report |
| 2026-10-05 | [`74a06109e7a3`](https://github.com/lly8666/richman4-remastered/commit/74a06109e7a3bcbe036e4e02c84a838917051aba) | core: port the monthly dividend payout and emit monthly presentation effects |
| 2026-10-05 | [`d96bcbb90f7a`](https://github.com/lly8666/richman4-remastered/commit/d96bcbb90f7a1479719b103cc51d466889ec0082) | text: Traditional prompts, original card yes/no boxes and real target names |
| 2026-10-05 | [`10133da58929`](https://github.com/lly8666/richman4-remastered/commit/10133da58929b99a24a591b8d6c8a3cc8d6bf100) | rules: hospital/prison building tiles, news 15 eligibility, skip events that cannot run |
| 2026-10-05 | [`b82404ac6723`](https://github.com/lly8666/richman4-remastered/commit/b82404ac6723290dc03cff90c02bbffa6e759a3d) | docs: audit of reachable rule dead-ends, on-screen text and hover-only info |
| 2026-10-05 | [`c0a747add70c`](https://github.com/lly8666/richman4-remastered/commit/c0a747add70cfc797eb946e4fde4c982a98d4beb) | docs: asset coverage audits (Data, Panel/jump/help, audio/map) with ranked gaps |
| 2026-10-05 | [`e307b45d18e7`](https://github.com/lly8666/richman4-remastered/commit/e307b45d18e787440087b671818f81fc537772c9) | Merge god entrance order, original slot machine and cupid wheel spinners |
| 2026-10-05 | [`5ac4b5838cb7`](https://github.com/lly8666/richman4-remastered/commit/5ac4b5838cb7106e117501e811f4cc854bcf40e6) | gods/services: entrance before the spinner, original slot machine and cupid wheels |
| 2026-10-05 | [`d06321bf7af3`](https://github.com/lly8666/richman4-remastered/commit/d06321bf7af30d580650067baeb403eb2bf70346) | Merge original card and tool speech bubbles for users and victims |
| 2026-10-05 | [`791f27e4fe0e`](https://github.com/lly8666/richman4-remastered/commit/791f27e4fe0e6251b28264c840172e03d191ed55) | shops: show the picked item's help text on touch |
| 2026-10-05 | [`d61c5a43004d`](https://github.com/lly8666/richman4-remastered/commit/d61c5a43004d8bca5bbac429bc0e8802b93816a9) | Card and tool speech bubbles from the original card/tool string tables |
| 2026-10-05 | [`c8c82ed457bd`](https://github.com/lly8666/richman4-remastered/commit/c8c82ed457bd334da6731e4ecc0c76f8a53df06b) | Merge original new-game intro: plane door, jumps and parachutes |
| 2026-10-05 | [`0e53c00b2f9e`](https://github.com/lly8666/richman4-remastered/commit/0e53c00b2f9e653ed668111896f9d851734b0287) | core: optional RICH4_ASSET_TRACE log of decoded archive entries for coverage audits |
| 2026-10-05 | [`e17985dce177`](https://github.com/lly8666/richman4-remastered/commit/e17985dce1777a72e985e2df3b3ffe0b2e98df04) | native: original new-game airplane/parachute intro |
| 2026-10-05 | [`3010f948d8a8`](https://github.com/lly8666/richman4-remastered/commit/3010f948d8a85d0d639ceeeba589eb293e85e41f) | android: launcher icon from the original rich4.exe desktop icon |
| 2026-10-05 | [`5ba91cc213f5`](https://github.com/lly8666/richman4-remastered/commit/5ba91cc213f5406fd667932a0ec79a07e326efde) | docs: record restored original UI, presentation queue and Android APK status |
| 2026-10-05 | [`9cb951124b96`](https://github.com/lly8666/richman4-remastered/commit/9cb951124b966533a15eb364260cb416dcbc978a) | Merge original card, tool, target picking, shop, magic house and hospital/prison screens |
| 2026-10-05 | [`35375005c02b`](https://github.com/lly8666/richman4-remastered/commit/35375005c02bf7a16d4ed5739058cfc58e131e22) | Merge original menus, new game, bank, stocks, save slots, trading, auction, lottery |
| 2026-10-05 | [`9afbdb66ebd2`](https://github.com/lly8666/richman4-remastered/commit/9afbdb66ebd25b3890e609b802e24f855482f384) | native items: name replay target screenshots by card/tool id |
| 2026-10-05 | [`45645deec052`](https://github.com/lly8666/richman4-remastered/commit/45645deec052e5910ec27c177238b3908e8f591e) | Merge original character toss before the dice roll |
| 2026-10-05 | [`66115f75b719`](https://github.com/lly8666/richman4-remastered/commit/66115f75b7191cf9a9c0999821344460c683c1eb) | native: present events, dice and effects in one ordered queue |
| 2026-10-05 | [`9c624a29f931`](https://github.com/lly8666/richman4-remastered/commit/9c624a29f9310b507bf9a07125f32c55ea5809ed) | native: character toss pose before the dice roll |
| 2026-10-05 | [`bfa6bfb4a60b`](https://github.com/lly8666/richman4-remastered/commit/bfa6bfb4a60be5bef32fa95462d82767448369cc) | native: use std::numbers::pi in the shareholding pie |
| 2026-10-05 | [`32a807382d4b`](https://github.com/lly8666/richman4-remastered/commit/32a807382d4b2eff5e29d009c323488a111247eb) | native items: replay confirms the first option of a duplicated shop row |
| 2026-10-05 | [`8af26cfe91d7`](https://github.com/lly8666/richman4-remastered/commit/8af26cfe91d79268da5adba19abc93a97f0a6dde) | Merge branch 'main' into agent-afd188b28dbfaa40b |
| 2026-10-05 | [`5bf4f86ec21b`](https://github.com/lly8666/richman4-remastered/commit/5bf4f86ec21bf288df4822e67d5161e42917a33b) | Merge main (messages, effects, Android rename) into the item screens branch |
| 2026-10-05 | [`12a69fa48f31`](https://github.com/lly8666/richman4-remastered/commit/12a69fa48f31633e26430bfbf895c70bf693ce24) | native: finger-sized controls for the finance and menu screens |
| 2026-10-05 | [`ef67e1d88aa7`](https://github.com/lly8666/richman4-remastered/commit/ef67e1d88aa782aa9024419082528458ce170ac5) | android: use io.divby0.rich4; a tap anywhere on GO rolls |
| 2026-10-05 | [`d83081983fac`](https://github.com/lly8666/richman4-remastered/commit/d83081983facca1951280f3b4c6b547e9e2e3edf) | native: original trading board, lottery bar and auction room |
| 2026-10-05 | [`91739de97cc9`](https://github.com/lly8666/richman4-remastered/commit/91739de97cc9552f5d3a4039097fc059367766d7) | Merge original speech bubbles, message boxes, yes/no strip and news screens |
| 2026-10-05 | [`78d3e9f94488`](https://github.com/lly8666/richman4-remastered/commit/78d3e9f944882af4827654c87584c09fb55ad997) | native items: player picks the magic group on the original icons; dev --items-gallery |
| 2026-10-05 | [`1dd7aa204609`](https://github.com/lly8666/richman4-remastered/commit/1dd7aa20460975d671d00a51c45c0aa3f1016b88) | Merge FLC decoder and original board effects |
| 2026-10-05 | [`1326e9d22028`](https://github.com/lly8666/richman4-remastered/commit/1326e9d22028b1691d32317fb6cc839def5fc374) | native: --play-effects dev flag plays effects in replays and dumps frames |
| 2026-10-05 | [`74be2c1b176a`](https://github.com/lly8666/richman4-remastered/commit/74be2c1b176a4153c01f979958e58507efb1ef21) | tests: core effect events for roadblock throw, missile and demolish card |
| 2026-10-05 | [`d1ad354447d2`](https://github.com/lly8666/richman4-remastered/commit/d1ad354447d2b050c4ae732886973e81fac2a619) | Merge branch 'main' into agent-a74b30a3e98006085 |
| 2026-10-05 | [`fd400f5f5e0d`](https://github.com/lly8666/richman4-remastered/commit/fd400f5f5e0d4bc8989721d4dcc953f56ab27326) | native: centre the board on the player while a god arrival caption shows |
| 2026-10-05 | [`df6921975b04`](https://github.com/lly8666/richman4-remastered/commit/df6921975b04a17166a780e0ea744af0915de142) | Merge branch 'main' into agent-afd188b28dbfaa40b |
| 2026-10-05 | [`846be6b72930`](https://github.com/lly8666/richman4-remastered/commit/846be6b729302ce28b32b60d282d73e615d07c8a) | Merge main (main-screen chrome) into effects branch |
| 2026-10-05 | [`36b269b4a185`](https://github.com/lly8666/richman4-remastered/commit/36b269b4a185bb1a0d4e1cff09329c00c65c41b8) | Merge branch 'main' into agent-af4d3d5016eea90ce |
| 2026-10-05 | [`0d00e4903eb6`](https://github.com/lly8666/richman4-remastered/commit/0d00e4903eb6699daff262a17b363f401d44db13) | native: play original board effect movies (missile, explosion, gods, ...) |
| 2026-10-05 | [`8a520c3db90a`](https://github.com/lly8666/richman4-remastered/commit/8a520c3db90ac16f5c74d687c9d3b8f2af318e89) | native: original menu, new-game, bank, stock, save/load, options and info screens |
| 2026-10-05 | [`bff06466e0dd`](https://github.com/lly8666/richman4-remastered/commit/bff06466e0dd74ca3d21c6f4d7679812633af473) | native: original card/tool grids, card-art popup, map target cursors, shop, magic house and bail screens |
| 2026-10-05 | [`dde26ee0d9ce`](https://github.com/lly8666/richman4-remastered/commit/dde26ee0d9ce17a7e255f257c9aa1f2ac57d6e42) | Merge original main-screen chrome: toolbar, info tabs, minimap, GO button, dice |
| 2026-10-05 | [`19c44e377b07`](https://github.com/lly8666/richman4-remastered/commit/19c44e377b07052af8f2260a9daac576d3cc2aae) | chore: ignore game-assets symlinks in worktrees |
| 2026-10-05 | [`831924cbe3bf`](https://github.com/lly8666/richman4-remastered/commit/831924cbe3bffd1c8ca87f0cb74aff7ee2f20e80) | native: --overlay-gallery dev flag renders sample overlays for review; silent skip taps |
| 2026-10-05 | [`3ed4b6ad0af0`](https://github.com/lly8666/richman4-remastered/commit/3ed4b6ad0af0ec8de4a5f90686b050afbb3a111f) | native: original main-screen chrome (toolbar, info tabs, minimap, GO, dice) |
| 2026-10-05 | [`64f609533cc9`](https://github.com/lly8666/richman4-remastered/commit/64f609533cc9c28c66f28457395e60f41719fe0e) | native ui: ink bounds and aliased glyphs for original-style text |
| 2026-10-05 | [`66793db7747a`](https://github.com/lly8666/richman4-remastered/commit/66793db7747a3f8a8221daf6d75215bdd3d8645b) | native: original speech bubbles, message boxes, yes/no strip and news/fortune screens |
| 2026-10-05 | [`17f27a236086`](https://github.com/lly8666/richman4-remastered/commit/17f27a236086192b09e73657a681b0ba760c24ae) | core: additive EffectEvent queue for original board animations |
| 2026-10-05 | [`fce0e279b095`](https://github.com/lly8666/richman4-remastered/commit/fce0e279b0958652f894caee2d870ec6ffce120c) | core: FLC movie decoder for Data/Panel/Jump effect animations |
| 2026-10-05 | [`d2f8a94f9aa9`](https://github.com/lly8666/richman4-remastered/commit/d2f8a94f9aa9b603e156f8c1539078e8a4a47e5e) | core: report the used card id on GameEvent for the card-art popup |
| 2026-10-05 | [`592ac341d998`](https://github.com/lly8666/richman4-remastered/commit/592ac341d998d98fb3b956f7e391a4f4dac062e1) | core: expose original announcement data for message boxes, news, fortune and gods |
| 2026-10-05 | [`11a33664ae62`](https://github.com/lly8666/richman4-remastered/commit/11a33664ae62cc5a6aff3a5e4787dec99629dc68) | docs: original board effects and animation spec |
| 2026-10-05 | [`911a96bc6df1`](https://github.com/lly8666/richman4-remastered/commit/911a96bc6df19dfae02972035a765718d0509507) | docs: original UI spec for card, tool and shop screens |
| 2026-10-05 | [`0eb58487927b`](https://github.com/lly8666/richman4-remastered/commit/0eb58487927baf2a090ceff81244662423949734) | docs: original UI specs for main screen, messages, finance and menus |
| 2026-10-05 | [`75545cd2a5db`](https://github.com/lly8666/richman4-remastered/commit/75545cd2a5dbce41967f5f6b4ae1bb617c508d4a) | native: continue from the newer of Save1 and Autosave |
| 2026-10-05 | [`8b6ce241c5f7`](https://github.com/lly8666/richman4-remastered/commit/8b6ce241c5f7d5d5a465996612ee9feae695c89b) | android: background save writes Autosave.DAT instead of the manual slot |
| 2026-10-05 | [`a4850b5e4f44`](https://github.com/lly8666/richman4-remastered/commit/a4850b5e4f44e1b328bfa6be9a79085bc8837169) | Merge Android APK packaging spike |
| 2026-10-05 | [`08de73e49e9d`](https://github.com/lly8666/richman4-remastered/commit/08de73e49e9de9514f29441726b7ca696cb6054f) | android: spike debug APK running the native game via SDL3 |
| 2026-10-05 | [`80f98d15f8a6`](https://github.com/lly8666/richman4-remastered/commit/80f98d15f8a67779501ec4ba3c9f499a1e3d9b19) | refactor: share native drawing kit via native_frame.hpp and track pointer state |
| 2026-10-05 | [`914a6de4cda3`](https://github.com/lly8666/richman4-remastered/commit/914a6de4cda32147ca3fc6b2592a09ced471ff9f) | tools: add rich4-contact-sheet for locating original UI artwork |
| 2026-10-05 | [`b28b0fe6443e`](https://github.com/lly8666/richman4-remastered/commit/b28b0fe6443e2b96fcbe5b3d6c519ed3a65372bf) | chore: keep original game assets in gitignored game-assets/ |
| 2026-10-05 | [`d910fc91762c`](https://github.com/lly8666/richman4-remastered/commit/d910fc91762cc3589d67c376886e9a56aca964ba) | docs: pause migration and record UI gaps and exact validation state |
| 2026-10-05 | [`7d7705594daf`](https://github.com/lly8666/richman4-remastered/commit/7d7705594daf81dd2bfc74815d8de8acdf097673) | test: require targeted card 17 coverage in native acceptance |
| 2026-10-05 | [`79418209ad40`](https://github.com/lly8666/richman4-remastered/commit/79418209ad405c1ac207d3a36460b5df15d8cbe7) | style: make portable C++ rules and tests consistently readable |
| 2026-10-05 | [`17e5c84b138f`](https://github.com/lly8666/richman4-remastered/commit/17e5c84b138f9fa3da9913cb8e681acd2a78cc79) | feat: finish native trading UI and resilient gameplay verification |
| 2026-10-05 | [`f089d6b2b505`](https://github.com/lly8666/richman4-remastered/commit/f089d6b2b505428558eb3b5a93be7ad347546eeb) | Validate legacy world semantics before playable session admission |
| 2026-10-05 | [`8939d12ec9e9`](https://github.com/lly8666/richman4-remastered/commit/8939d12ec9e9157c77dd3c76f1bc74d9745efe17) | test: add isolated native acceptance matrix and trading fixture |
| 2026-10-05 | [`5a0c24ad0b55`](https://github.com/lly8666/richman4-remastered/commit/5a0c24ad0b557402f58ad1238c35de393a9aca57) | feat: integrate direct trades and original flight attacks |
| 2026-10-05 | [`2b391cbecabc`](https://github.com/lly8666/richman4-remastered/commit/2b391cbecabcbb8db12a004e8be4f17fc63b5452) | feat: preserve player market trading and original computer sale rules |
| 2026-10-05 | [`4a3e8d3ac51d`](https://github.com/lly8666/richman4-remastered/commit/4a3e8d3ac51d4e27bee821f43012f520ebd56140) | feat: complete native turn lifecycle and financial settlement |
| 2026-10-05 | [`5934ed68849f`](https://github.com/lly8666/richman4-remastered/commit/5934ed68849fb557d948d9696a4a14a02fe53a2f) | Port original computer stock trading with complete-visit oracles |
| 2026-10-05 | [`40a421309b95`](https://github.com/lly8666/richman4-remastered/commit/40a421309b954fce6e2c4392221fb21a8dfdba5e) | fix: preserve controlled die inventory in turn history |
| 2026-10-05 | [`ffbe5530900f`](https://github.com/lly8666/richman4-remastered/commit/ffbe5530900f7a02c6bfed1dfc8a969f0060afe2) | Port special actor turns and verify original routes and interactions |
| 2026-10-05 | [`7a604b7b0ff3`](https://github.com/lly8666/richman4-remastered/commit/7a604b7b0ff3f71cfdde70866fc5d1d769352ae6) | Persist original month counter and stock investment profile |
| 2026-10-05 | [`4bc6335b2e45`](https://github.com/lly8666/richman4-remastered/commit/4bc6335b2e45af712c8c61c290614489b1678149) | test: preserve object pools and exercise native victory flow |
| 2026-10-05 | [`0fc73c178d77`](https://github.com/lly8666/richman4-remastered/commit/0fc73c178d77d6ce0ad4dd71c1f58f0b51a64cfe) | docs: document native launch, replay evidence and migrated modules |
| 2026-10-05 | [`075d592ffd40`](https://github.com/lly8666/richman4-remastered/commit/075d592ffd40f3a7c41fdf3a9ffca6e6a61e0819) | feat: add playable Mac SDL application and isolated interaction replay |
| 2026-10-05 | [`f1c2d2d806ae`](https://github.com/lly8666/richman4-remastered/commit/f1c2d2d806ae88c7ba9e3a42d02f98fd008e0f19) | Port original computer tool decisions and verify 2080 predicates |
| 2026-10-05 | [`cef7c2da0ebd`](https://github.com/lly8666/richman4-remastered/commit/cef7c2da0ebd63d44e0cb2ae0fee23ec7fe53be1) | test: add synthetic native inventory interaction saves |
| 2026-10-05 | [`3254356521a8`](https://github.com/lly8666/richman4-remastered/commit/3254356521a81cc0f6c33bb09e419c2de105708a) | feat: integrate native actions, financial rules and god encounters |
| 2026-10-05 | [`156ecac60ad9`](https://github.com/lly8666/richman4-remastered/commit/156ecac60ad94cded0fc404d13962dd609f3b8ba) | feat: render boats, sleepwalking, injuries and current company ownership |
| 2026-10-05 | [`e95b7bfc0e8d`](https://github.com/lly8666/richman4-remastered/commit/e95b7bfc0e8db9e955f2a5abf94c76290f21c4a2) | Port god encounters and original decimal spinner state machine |
| 2026-10-05 | [`b4eb03dcce9b`](https://github.com/lly8666/richman4-remastered/commit/b4eb03dcce9b65e2701e16de792c73d82a247ea4) | Preserve x87 stock tax arithmetic and indexed event voice draws |
| 2026-10-05 | [`ac5cab4e8bde`](https://github.com/lly8666/richman4-remastered/commit/ac5cab4e8bde88074c4acebd4885da3e2b9fae57) | Port original computer card strategy and x87 card arithmetic |
| 2026-10-05 | [`111cc1f89ccf`](https://github.com/lly8666/richman4-remastered/commit/111cc1f89ccfb1841dd73a46c2b0870a5df03c55) | Share portable x87 precision arithmetic and validate independent instructions |
| 2026-10-05 | [`c7b7f253b641`](https://github.com/lly8666/richman4-remastered/commit/c7b7f253b641763f2a0807792e49007278001833) | feat: render special actors and robot with original directional art |
| 2026-10-05 | [`274b7dc40b28`](https://github.com/lly8666/richman4-remastered/commit/274b7dc40b280cbc7da23dd18d2440853a5e67a1) | Port daily stock market and purchases with exact RNG and x87 rounding |
| 2026-10-05 | [`6e26769c61c5`](https://github.com/lly8666/richman4-remastered/commit/6e26769c61c5424f2b163c174e9b47a44a9e2976) | Port active card effects and target validation to native C++ |
| 2026-10-05 | [`9cb9d1b8b2cb`](https://github.com/lly8666/richman4-remastered/commit/9cb9d1b8b2cba1a30a27d7663baf481eb457acaf) | Port original holiday tables and loan due dates with bounded lookup |
| 2026-10-05 | [`34972ccaa0f2`](https://github.com/lly8666/richman4-remastered/commit/34972ccaa0f28a8a40127fa640e6c19faeb71fb2) | Port lottery settlement and persist runtime object state |
| 2026-10-05 | [`1cdb02449f8d`](https://github.com/lly8666/richman4-remastered/commit/1cdb02449f8d218e45ffcf6753c71ebe02796507) | feat: migrate equipment, obstacle placement and teleport tools |
| 2026-10-05 | [`f3d4e54fc82a`](https://github.com/lly8666/richman4-remastered/commit/f3d4e54fc82a18192d1a738822cc9061ff8d5885) | Port stock sales, forced prices, and wealth with precision oracles |
| 2026-10-05 | [`e79a58eb6cc6`](https://github.com/lly8666/richman4-remastered/commit/e79a58eb6cc6ce76726c73415633ee57a3571a10) | feat: add portable turn engine and native gameplay decisions |
| 2026-10-05 | [`feaf2347488b`](https://github.com/lly8666/richman4-remastered/commit/feaf2347488bd651ec60a68a6a9cf57ecfa9a104) | feat: port shop trading and magic house choices to portable rules |
| 2026-10-05 | [`2ae0bbf83bd4`](https://github.com/lly8666/richman4-remastered/commit/2ae0bbf83bd4488f63c186f81e84f8d21b5d4317) | feat: port news and fortune rules with persistent event state |
| 2026-10-05 | [`0f7d395b8d8f`](https://github.com/lly8666/richman4-remastered/commit/0f7d395b8d8fed0abbf8a27f38b7059a7c8bc1a6) | Port property auctions to a resumable native C++ session |
| 2026-10-05 | [`418454c88fa1`](https://github.com/lly8666/richman4-remastered/commit/418454c88fa137820397c0dabd20a4c0a3b686d2) | feat: mix original music and character audio through native SDL |
| 2026-10-05 | [`66c11e58c4e9`](https://github.com/lly8666/richman4-remastered/commit/66c11e58c4e9604e88347eeebc157e369bf2d07b) | feat: render original boards and characters in portable C++ |
| 2026-10-05 | [`14829113fd07`](https://github.com/lly8666/richman4-remastered/commit/14829113fd077970694b00c0f2d2b5e590ed078a) | feat: restore portable world state and create original-map games |
| 2026-10-05 | [`c8d4a2964951`](https://github.com/lly8666/richman4-remastered/commit/c8d4a29649519ecda5e05f36196166cf80bc8a8d) | test: verify real game turns with isolated windowless Wine hooks |
| 2026-10-05 | [`643f033cb819`](https://github.com/lly8666/richman4-remastered/commit/643f033cb8195f8bdbc89d59d3095a797ca5b794) | test: preserve verified windowless GDI and DirectDraw probes |
| 2026-10-05 | [`7c429a89efbf`](https://github.com/lly8666/richman4-remastered/commit/7c429a89efbf585a312dd356a03c06a7f5073188) | test: compare original gameplay save captures with portable state decoding |
| 2026-10-05 | [`66ac5fd82b4b`](https://github.com/lly8666/richman4-remastered/commit/66ac5fd82b4b6f9556bae89dfeb92b0d2f9fbfff) | docs: record migration boundaries and native validation evidence |
| 2026-10-05 | [`2e5539f4981e`](https://github.com/lly8666/richman4-remastered/commit/2e5539f4981ed1adff07ff06a5669d7a396406da) | test: validate portable core and complete assets on Android ARM64 |
| 2026-10-05 | [`9301e69b99e0`](https://github.com/lly8666/richman4-remastered/commit/9301e69b99e0ac3153972931a66e7f0834a2dbb8) | test: add self-checked original EXE differential verification |
| 2026-10-05 | [`9a799a11d8be`](https://github.com/lly8666/richman4-remastered/commit/9a799a11d8be7506f5b33dd550362c1910333ed8) | feat: migrate GND terrain parsing and fixed-point tile rasterization |
| 2026-10-05 | [`86209e78f6c8`](https://github.com/lly8666/richman4-remastered/commit/86209e78f6c87806bd138dfffbb1e614375f07d2) | fix: preserve composited sprite masks and require complete RNG vectors |
| 2026-10-05 | [`7c217ed6d8f6`](https://github.com/lly8666/richman4-remastered/commit/7c217ed6d8f6a827d88ac5cf6853c28afe7cbb39) | feat: reproduce original Watcom random stream for deterministic replay |
| 2026-10-05 | [`4ece76b9c205`](https://github.com/lly8666/richman4-remastered/commit/4ece76b9c2051f43a9d0e393eeb308ea45162ff0) | feat: port card inventories and land toll rules with original behavior |
| 2026-10-05 | [`9cf835ea4e19`](https://github.com/lly8666/richman4-remastered/commit/9cf835ea4e1974a5b3939d28b54e5f0458c3d8ac) | feat: replace date and direction assembly with verified C++ rules |
| 2026-10-05 | [`142ca68b4f6e`](https://github.com/lly8666/richman4-remastered/commit/142ca68b4f6e11ab92c673caead8e48e256e296c) | feat: add portable sprite drawing and native SDL3 resource viewer |
| 2026-10-05 | [`812c6ff14a42`](https://github.com/lly8666/richman4-remastered/commit/812c6ff14a4200255b70892219b40e01fd75a64a) | feat: preserve original save files with an explicit portable codec |
| 2026-10-05 | [`257cabcf7754`](https://github.com/lly8666/richman4-remastered/commit/257cabcf775448600bedc8d61acca1065805811b) | feat: migrate MKF decoding to portable C++20 with bounded parsing |
| 2026-10-05 | [`38d0ef93772e`](https://github.com/lly8666/richman4-remastered/commit/38d0ef93772e47cb886e583bc62b4fbd3bc83fed) | feat: configure sandboxed Mac runtime with original MingLiU font |
| 2026-10-05 | [`5ed146f083df`](https://github.com/lly8666/richman4-remastered/commit/5ed146f083dfe84059e1feeffbce780ccbf072a2) | feat: add asset audit and reproducible Windows build on Mac |
| 2026-10-05 | [`5d0b427c8952`](https://github.com/lly8666/richman4-remastered/commit/5d0b427c895243fb729a6557dedeb52f827cae10) | chore: keep game assets and local runtime outside version control |
